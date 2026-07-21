#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <algorithm>
#include <string>
#include <set>
#include <unordered_set>
#include <opencv2/opencv.hpp>
#include <yaml-cpp/yaml.h>
#include "Communication/BeanProtocol.h"
#include "Communication/BeanSerial.h"
#include "Detector/BeanDetector.h"

#ifdef __linux__
#include <unistd.h>
#include <libgen.h>
#include <climits>
#endif

#ifdef HAS_MV_CAMERA
#include "CameraDriver/MvCamera.h"
#endif

using namespace bean_sorting;

static std::atomic<bool> g_running(true);
void sigint_handler(int) { g_running = false; }

// class_id → 显示名
static const char* class_name(int id) {
    switch (id) {
        case 0:  return "黄豆";
        case 1:  return "绿豆";
        case 2:  return "白芸豆";
        case 3:  return "1";
        case 4:  return "2";
        case 5:  return "3";
        case 6:  return "4";
        case 7:  return "5";
        default: return "?";
    }
}

enum class CommPhase {
    SCANNING_DIGITS,   // 扫描数字 (150帧≈5秒)
    DIGIT_GAP,         // 数字包已发, 间隔等待 (30帧≈1秒)
    SCANNING_BEANS,    // 扫描豆子 (120帧≈4秒)
    DONE               // 双包已发, 通信结束
};

static constexpr int DIGIT_SCAN_FRAMES = 150;
static constexpr int BEAN_SCAN_FRAMES  = 120;
static constexpr int GAP_FRAMES        = 30;

static const char* phaseName(CommPhase p) {
    switch (p) {
        case CommPhase::SCANNING_DIGITS: return "SCAN_DIGITS";
        case CommPhase::DIGIT_GAP:       return "GAP";
        case CommPhase::SCANNING_BEANS:  return "SCAN_BEANS";
        case CommPhase::DONE:            return "DONE";
    }
    return "???";
}

// ---- 辅助: 从排序后的检测结果提取去重 class_id ----
static std::vector<int> extractUnique(const std::vector<BeanDetector::Detection>& sorted) {
    std::vector<int> ids;
    std::unordered_set<int> seen;
    for (const auto& d : sorted) {
        if (seen.insert(static_cast<int>(d.bean_type)).second)
            ids.push_back(static_cast<int>(d.bean_type));
    }
    return ids;
}

// ---- 辅助: 按 X 排序 ----
static void sortByX(std::vector<BeanDetector::Detection>& dets) {
    std::sort(dets.begin(), dets.end(),
              [](const BeanDetector::Detection& a, const BeanDetector::Detection& b) {
                  return a.center.x < b.center.x;
              });
}

// ---- 辅助: 找缺失数字 ----
static int findMissingDigit(const std::vector<int>& present) {
    std::set<int> have(present.begin(), present.end());
    for (int d : ALL_DIGIT_CLASSES)
        if (have.find(d) == have.end()) return d;
    return -1;
}

// ---- 辅助: 找缺失豆子 ----
static int findMissingBean(const std::vector<int>& present) {
    std::set<int> have(present.begin(), present.end());
    for (int b : ALL_BEAN_CLASSES)
        if (have.find(b) == have.end()) return b;
    return -1;
}

// ---- 辅助: 过滤豆子 ----
static std::vector<BeanDetector::Detection> filterBeans(const std::vector<BeanDetector::Detection>& dets) {
    std::vector<BeanDetector::Detection> out;
    for (const auto& d : dets)
        if (isBeanClass(static_cast<int>(d.bean_type))) out.push_back(d);
    return out;
}

// ---- 辅助: 过滤数字 ----
static std::vector<BeanDetector::Detection> filterDigits(const std::vector<BeanDetector::Detection>& dets) {
    std::vector<BeanDetector::Detection> out;
    for (const auto& d : dets)
        if (isDigitClass(static_cast<int>(d.bean_type))) out.push_back(d);
    return out;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  sigint_handler);
    std::signal(SIGTERM, sigint_handler);

    std::string serial_port = "/dev/ttyACM*";
    bool show_tx   = true;
    bool watchdog  = false;

    if (argc >= 2) serial_port = argv[1];

#ifdef __linux__
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        std::string exe_dir = dirname(exe_path);
        if (exe_dir.size() >= 6 && exe_dir.substr(exe_dir.size() - 6) == "/build")
            chdir((exe_dir + "/..").c_str());
        else
            chdir(exe_dir.c_str());
    }
#endif

#ifdef HAS_MV_CAMERA
    MvCamera camera;
    camera.open("config/bean_sorting.yaml");
#endif

    BeanSerial serial(serial_port);
    serial.SetTxLogEnabled(show_tx);
    if (!serial.Open()) {
        if (watchdog) return 1;
        std::cerr << "[Main] 串口未打开, 切换模拟模式" << std::endl;
        serial.SetSimulated(true);
    }

    BeanDetector detector;
    detector.setConfThreshold(0.66f);
    detector.setNmsThreshold(0.05);
    if (!detector.loadModel("models/best7.onnx")) {
        std::cerr << "[Main] 模型未加载, 仅通信测试" << std::endl;
    }
    detector.setDebugEnabled(true);

    cv::namedWindow("Bean Sorting", cv::WINDOW_AUTOSIZE);

    cv::Mat image;

    // ---- 通信状态机 ----
    CommPhase phase = CommPhase::SCANNING_DIGITS;
    int frameCounter = 0;
    bool digitSent = false;
    bool beanSent  = false;

    auto t_last_log = std::chrono::steady_clock::now();

    std::cout << "[Main] 双包通信 (数字→豆子, 对齐 mvs 协议)" << std::endl;
    std::cout << "[Main]   Phase1: 扫数字 " << DIGIT_SCAN_FRAMES << "帧 → 发数字包(0xA5+5digits+CRC16=8B)" << std::endl;
    std::cout << "[Main]   Phase2: 间隔 " << GAP_FRAMES << "帧" << std::endl;
    std::cout << "[Main]   Phase3: 扫豆子 " << BEAN_SCAN_FRAMES << "帧 → 发豆子包(0xA5+3beans+CRC16=6B)" << std::endl;

    while (g_running) {
#ifdef HAS_MV_CAMERA
        if (!camera.grab(image) || image.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
#else
        image = cv::Mat::zeros(480, 640, CV_8UC3);
        cv::putText(image, "No Camera - Simulated",
                    cv::Point(50, 240), cv::FONT_HERSHEY_SIMPLEX,
                    1.0, cv::Scalar(0, 255, 0), 2);
#endif

        auto results = detector.isLoaded() ? detector.detect(image)
                                           : std::vector<BeanDetector::Detection>{};
        if (detector.isLoaded()) detector.drawResults(image, results);
        sortByX(results);  // 按 X 坐标从左到右排序

        // ============================================================
        //  通信状态机 (对齐 mvs_openvino_demo)
        // ============================================================
        switch (phase) {

        case CommPhase::SCANNING_DIGITS: {
            auto digitDets = filterDigits(results);  // 忽略豆子
            auto digitIds  = extractUnique(digitDets);

            frameCounter++;
            if (digitIds.size() >= 4) {
                // 取前4个 → 推断第5个
                std::vector<int> packet(digitIds.begin(), digitIds.begin() + 4);
                int missing = findMissingDigit(packet);
                if (missing >= 0) packet.push_back(missing);

                if (serial.sendDigitPacket(packet)) {
                    digitSent = true;
                    std::cout << "[TX] 数字包已发:";
                    for (int id : packet) std::cout << " " << class_name(id);
                    std::cout << "  (帧=" << frameCounter << ")" << std::endl;
                    phase = CommPhase::DIGIT_GAP;
                    frameCounter = 0;
                }
                break;
            }

            // 不超时, 一直扫直到收集够
            break;
        }

        case CommPhase::DIGIT_GAP: {
            frameCounter++;
            if (frameCounter >= GAP_FRAMES) {
                std::cout << "[TX] → 开始扫描豆子" << std::endl;
                phase = CommPhase::SCANNING_BEANS;
                frameCounter = 0;
            }
            break;
        }

        case CommPhase::SCANNING_BEANS: {
            auto beanDets = filterBeans(results);  // 忽略数字
            auto beanIds  = extractUnique(beanDets);

            frameCounter++;
            if (beanIds.size() >= 2) {
                // 取前2个 → 推断第3个
                std::vector<int> packet(beanIds.begin(), beanIds.begin() + 2);
                int missing = findMissingBean(packet);
                if (missing >= 0) packet.push_back(missing);

                if (serial.sendBeanPacket(packet)) {
                    beanSent = true;
                    std::cout << "[TX] 豆子包已发:";
                    for (int id : packet) std::cout << " " << class_name(id);
                    std::cout << "  (帧=" << frameCounter << ")" << std::endl;
                    phase = CommPhase::DONE;
                    std::cout << "[TX] 双包发送完毕" << std::endl;
                }
                break;
            }

            if (frameCounter >= BEAN_SCAN_FRAMES) {
                std::cout << "[TX] 豆子扫描超时, 跳过" << std::endl;
                phase = CommPhase::DONE;
            }
            break;
        }

        case CommPhase::DONE:
            break;  // 通信结束, 继续显示画面
        }

        // ---- 状态日志(每5秒) ----
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - t_last_log).count() >= 5.0) {
            t_last_log = now;
            std::cout << "[TX] " << phaseName(phase)
                      << " frame=" << frameCounter
                      << (digitSent ? " DIGIT:SENT" : " DIGIT:----")
                      << (beanSent  ? " BEAN:SENT"  : " BEAN:----")
                      << " det=" << results.size()
                      << std::endl;
        }

        cv::imshow("Bean Sorting", image);
        if (cv::waitKey(1) == 27) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    serial.Close();
    cv::destroyAllWindows();
    return 0;
}
