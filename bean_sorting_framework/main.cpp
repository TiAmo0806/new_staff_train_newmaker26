#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <algorithm>
#include <string>
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
void sigint_handler(int) { g_running = false; }// Ctrl+C处理
//豆子类型映射，规定豆子类别放在哪个箱子
uint8_t map_bean_to_box(BeanType type) {
    switch (type) {
        case BeanType::SOYBEAN:     return 1;
        case BeanType::MUNG_BEAN:   return 2;
        case BeanType::KIDNEY_BEAN: return 3;
        default:                    return 0;
    }
}
//枚举转字符串，返回类型为指针  模型标签名指向我定义的名字
//static限制函数只在当前文件使用
static const char* type_label(BeanType t) {
    switch (t) {
        case BeanType::SOYBEAN:     return "SOYBEAN";
        case BeanType::MUNG_BEAN:   return "MUNG";
        case BeanType::KIDNEY_BEAN: return "KIDNEY";
        case BeanType::DATA_1:      return "1";
        case BeanType::DATA_2:      return "2";
        case BeanType::DATA_3:      return "3";
        case BeanType::DATA_4:      return "4";
        case BeanType::DATA_5:      return "5";
        case BeanType::UNKNOWN:     return "UNKNOWN";
        case BeanType::ERROR:       return "ERROR";
        default:                    return "?";
    }
}
//状态机设置，对标电控
enum class State { IDLE, BEAN_SENT, COLLECT, BATCH_SENT };

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  sigint_handler);
    std::signal(SIGTERM, sigint_handler);
//调用串口
    std::string serial_port = "/dev/ttyACM*";
    bool show_tx   = true;
    bool watchdog  = false;

    if (argc >= 2) serial_port = argv[1];

    // 自动定位项目根目录 (解决从 build/ 运行时相对路径漂移)
#ifdef __linux__
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        std::string exe_dir = dirname(exe_path);
        // 如果从 build/ 运行, 上一级就是项目根; 否则就是 exe 所在目录本身
        std::string bin_name = exe_dir;
        if (bin_name.size() >= 6 && bin_name.substr(bin_name.size() - 6) == "/build")
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
    // 比赛时设 false：跳过每帧 debug_ 克隆, 省 ~3.75MB/帧
    detector.setDebugEnabled(true);

    cv::namedWindow("Bean Sorting", cv::WINDOW_AUTOSIZE);

    cv::Mat image;

    static constexpr int kDigitFirst = 3;//定义数字标签从3开始
    static constexpr int kDigitCount = 5;//五个数字标签
    static constexpr int kMinCollect = 4;//最少收集四个数字批量发送
    BeanDetector::Detection digit_buf[kDigitCount];//存储推理结果
    int digit_ok[kDigitCount] = {0,0,0,0,0};//预留位置看五个数是否收齐

    State state             = State::IDLE;
    int   last_bean_class   = -1;//上次识别豆子类型-1无
    auto  t_last_log        = std::chrono::steady_clock::now();//日志计时器

    // 单向通信延迟: 发完帧后等N帧再切状态, 给电控处理时间
    static constexpr int kSendDelay = 15;   // 0.5秒 15帧
    int send_cooldown             = 0;//发送冷却为0,一直发

    std::cout << "[Main] 主循环启动 (单向通信, 无ACK)" << std::endl;

    while (g_running) {
#ifdef HAS_MV_CAMERA
        if (!camera.grab(image) || image.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;//等待下一帧
        }
#else
        image = cv::Mat::zeros(480, 640, CV_8UC3);
        cv::putText(image, "No Camera - Simulated",
                    cv::Point(50, 240), cv::FONT_HERSHEY_SIMPLEX,
                    1.0, cv::Scalar(0, 255, 0), 2);
#endif
//==============模型检测部分======================
        auto results = detector.isLoaded() ? detector.detect(image)
                                           : std::vector<BeanDetector::Detection>{};
        if (detector.isLoaded()) detector.drawResults(image, results);
//对识别目标结果进行分类
        std::vector<BeanDetector::Detection> beans, numbers;
        for (const auto& r : results) {
            int cls = static_cast<int>(r.bean_type);
            if (cls >= 0 && cls <= 2) beans.push_back(r);//豆子
            else if (cls >= kDigitFirst && cls < kDigitFirst + kDigitCount)
                numbers.push_back(r);//数字
        }

        // 冷却递减
        if (send_cooldown > 0) --send_cooldown;//每帧send_cooldown-1直到为零开始下一论发送

        // ---- IDLE状态检测豆子, 发送Vision帧 ----
        if (state == State::IDLE) {
            if (!beans.empty()) {
                int best = 0;//取置信度最高结果
                for (size_t i = 1; i < beans.size(); ++i)
                    if (beans[i].confidence > beans[best].confidence) best = (int)i;

                int cur_class = static_cast<int>(beans[best].bean_type);
                if (cur_class != last_bean_class) {
                    for (int i = 0; i < kDigitCount; ++i) digit_ok[i] = 0;
                    last_bean_class = cur_class;
                }
                //发送vision帧到单片机

                auto v = detector.toVisionData(beans[best],
                    map_bean_to_box(beans[best].bean_type));
                serial.sendVision(v);
                std::cout << "[TX] 豆子->" << type_label(beans[best].bean_type)
                          << " 箱" << (int)map_bean_to_box(beans[best].bean_type)
                          << "  (单向发送, " << kSendDelay << "帧后自动切COLLECT)" << std::endl;

                state = State::BEAN_SENT;
                send_cooldown = kSendDelay;
            }
        }

        // ---- BEAN_SENT: 等待冷却, 然后切COLLECT ----
        if (state == State::BEAN_SENT) {
            if (send_cooldown == 0) {
                state = State::COLLECT;
                std::cout << "[TX] === 开始收集数字 ===" << std::endl;
            }
        }

        // ---- COLLECT: 收集数字, 满4个打包发送Batch帧 ----
        if (state == State::COLLECT) {
            // 如果画面中又出现了豆子(置信度更高的), 重新发送
            if (!beans.empty()) {
                int best = 0;
                for (size_t i = 1; i < beans.size(); ++i)
                    if (beans[i].confidence > beans[best].confidence) best = (int)i;
                int cur_class = static_cast<int>(beans[best].bean_type);
                if (cur_class != last_bean_class) {
                    // 豆子变了, 重置所有状态
                    for (int i = 0; i < kDigitCount; ++i) digit_ok[i] = 0;
                    last_bean_class = cur_class;

                    auto v = detector.toVisionData(beans[best],
                        map_bean_to_box(beans[best].bean_type));
                    serial.sendVision(v);
                    std::cout << "[TX] 豆子变更->" << type_label(beans[best].bean_type)
                              << " 箱" << (int)map_bean_to_box(beans[best].bean_type)
                              << "  (重新发送, 重置收集)" << std::endl;
                    state = State::BEAN_SENT;
                    send_cooldown = kSendDelay;
                }
            }

            // 收集数字 (只在COLLECT状态才执行)
            if (state == State::COLLECT) {
                for (const auto& n : numbers) {
                    int s = static_cast<int>(n.bean_type) - kDigitFirst;
                    if (s < 0 || s >= kDigitCount) continue;
                    if (!digit_ok[s] || n.confidence > digit_buf[s].confidence) {
                        bool is_new = !digit_ok[s];
                        digit_buf[s] = n;
                        digit_ok[s]  = 1;
                        if (is_new) {
                            int collected = 0;
                            for (int i = 0; i < kDigitCount; ++i) if (digit_ok[i]) ++collected;
                            std::cout << "[TX]   新: " << type_label(n.bean_type)
                                      << " (" << collected << "/" << kMinCollect << ")"
                                      << std::endl;
                        }
                    }
                }

                int collected = 0;
                for (int i = 0; i < kDigitCount; ++i) if (digit_ok[i]) ++collected;

                if (collected >= kMinCollect) {
                    // ---- 找缺失槽位 ----
                    int miss_slot = -1;
                    for (int i = 0; i < kDigitCount; ++i)
                        if (!digit_ok[i]) { miss_slot = i; break; }
                    int miss_cls = (miss_slot >= 0) ? (kDigitFirst + miss_slot) : -1;

                    // ---- 按画面 x 坐标从左到右排序 ----
                    struct { int slot; float x; } items[kDigitCount];
                    int n = 0;
                    for (int i = 0; i < kDigitCount; ++i)
                        if (digit_ok[i]) { items[n].slot = i; items[n].x = digit_buf[i].center.x; ++n; }

                    for (int a = 0; a < n-1; ++a)
                        for (int b = a+1; b < n; ++b)
                            if (items[a].x > items[b].x) {
                                auto t = items[a]; items[a] = items[b]; items[b] = t;
                            }

                    // ---- 构建批量帧 ----
                    std::vector<bean_sorting::BatchEntry> batch;

                    std::cout << "[TX] === 打包批量帧 ===" << std::endl;
                    for (int i = 0; i < n; ++i) {
                        int cls = kDigitFirst + items[i].slot;
                        bean_sorting::BatchEntry e;
                        BeanType bt = static_cast<BeanType>(cls);
                        if (bt == BeanType::DATA_4 || bt == BeanType::DATA_5)
                            bt = BeanType::ERROR;
                        e.bean_type  = bt;
                        e.target_box = (uint8_t)(4 + i);   // 目标箱号4/5/6/7
                        e.detected   = true;
                        batch.push_back(e);
                        std::cout << "[TX]   箱" << (int)e.target_box
                                  << " <- " << type_label(static_cast<BeanType>(cls))
                                  << " x=" << (int)items[i].x << std::endl;
                    }
                    if (miss_slot >= 0) {
                        bean_sorting::BatchEntry e;
                        BeanType bt = static_cast<BeanType>(miss_cls);
                        if (bt == BeanType::DATA_4 || bt == BeanType::DATA_5)
                            bt = BeanType::ERROR;
                        e.bean_type  = bt;
                        e.target_box = 0;          // 缺失标记
                        e.detected   = false;
                        batch.push_back(e);
                        std::cout << "[TX]   [推断缺失] "
                                  << type_label(static_cast<BeanType>(miss_cls)) << std::endl;
                    }

                    serial.sendBatch(batch);
                    std::cout << "[TX] 批量帧已发送 (单向), 回到IDLE" << std::endl;

                    // 重置, 直接回IDLE
                    for (int i = 0; i < kDigitCount; ++i) digit_ok[i] = 0;
                    last_bean_class = -1;
                    state = State::IDLE;
                }
            }
        }

        // ---- BATCH_SENT: 单向模式下仅在冷却时停留, 然后回IDLE ----
        if (state == State::BATCH_SENT) {
            if (send_cooldown == 0) {
                for (int i = 0; i < kDigitCount; ++i) digit_ok[i] = 0;
                last_bean_class = -1;
                state = State::IDLE;
            }
        }

        // 每5秒一次状态日志 (旧版每秒刷, 树莓派终端I/O开销不小)
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - t_last_log).count() >= 5.0) {
            t_last_log = now;
            switch (state) {
            case State::IDLE:       std::cout << "[TX] IDLE" << std::endl; break;
            case State::BEAN_SENT:  std::cout << "[TX] BEAN_SENT (冷却:" << send_cooldown << ")" << std::endl; break;
            case State::COLLECT: { int cur = 0;
                for (int i=0;i<kDigitCount;++i) if(digit_ok[i]) ++cur;
                std::cout << "[TX] COLLECT " << cur << "/" << kMinCollect << std::endl; break; }
            case State::BATCH_SENT: std::cout << "[TX] BATCH_SENT (冷却:" << send_cooldown << ")" << std::endl; break;
            }
        }

        cv::imshow("Bean Sorting", image);
        if (cv::waitKey(1) == 27) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    serial.Close();
    cv::destroyAllWindows();
    return 0;
}
