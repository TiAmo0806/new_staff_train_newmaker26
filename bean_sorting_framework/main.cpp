#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <algorithm>
#include <opencv2/opencv.hpp>
#include <yaml-cpp/yaml.h>
#include "Communication/BeanProtocol.h"
#include "Communication/BeanSerial.h"
#include "Detector/BeanDetector.h"

#ifdef HAS_MV_CAMERA
#include "CameraDriver/MvCamera.h"
#endif

using namespace bean_sorting;

static std::atomic<bool> g_running(true);
void sigint_handler(int) { g_running = false; }

class FrameDecoder {
public:
    struct Frame {
        bool is_vision;
        VisionData  vision;
        ControlData control;
    };
    std::vector<Frame> feed(const std::vector<uint8_t>& raw) {
        std::vector<Frame> out;
        buf_.insert(buf_.end(), raw.begin(), raw.end());
        while (buf_.size() >= 2) {
            uint8_t first = buf_[0];
            if (first == HEADER_VISION && buf_.size() >= VISION_FRAME) {
                out.push_back({true, decode_vision(buf_.data(), buf_.size()), {}});
                buf_.erase(buf_.begin(), buf_.begin() + VISION_FRAME);
            } else if (first == HEADER_CONTROL && buf_.size() >= CONTROL_FRAME) {
                out.push_back({false, {}, decode_control(buf_.data(), buf_.size())});
                buf_.erase(buf_.begin(), buf_.begin() + CONTROL_FRAME);
            } else if (first != HEADER_VISION && first != HEADER_CONTROL) {
                buf_.erase(buf_.begin());
            } else { break; }
        }
        return out;
    }
    void reset() { buf_.clear(); }
private:
    std::vector<uint8_t> buf_;
};

uint8_t map_bean_to_box(BeanType type) {
    switch (type) {
        case BeanType::SOYBEAN:     return 1;
        case BeanType::MUNG_BEAN:   return 2;
        case BeanType::KIDNEY_BEAN: return 3;
        default:                    return 0;
    }
}

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

enum class State { IDLE, BEAN_SENT, COLLECT, BATCH_SENT };

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  sigint_handler);
    std::signal(SIGTERM, sigint_handler);

    std::string serial_port = "/dev/ttyACM0";
    bool show_tx   = true;
    bool watchdog  = false;

    if (argc >= 2) serial_port = argv[1];

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
    detector.setConfThreshold(0.65f);
    detector.setNmsThreshold(0.05f);
    if (!detector.loadModel("models/best_opset11.onnx")) {
        std::cerr << "[Main] 模型未加载, 仅通信测试" << std::endl;
    }

    cv::namedWindow("Bean Sorting", cv::WINDOW_AUTOSIZE);

    FrameDecoder decoder;
    cv::Mat image;

    static constexpr int kDigitFirst = 3;
    static constexpr int kDigitCount = 5;
    static constexpr int kMinCollect = 4;

    BeanDetector::Detection digit_buf[kDigitCount];
    int digit_ok[kDigitCount] = {0,0,0,0,0};

    State state             = State::IDLE;
    bool  bean_waiting_ack  = false;
    bool  batch_waiting_ack = false;
    int   last_bean_class   = -1;
    auto  t_last_log        = std::chrono::steady_clock::now();
    bool  collect_started   = false;

    std::cout << "[Main] 主循环启动 (精简通信)" << std::endl;

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

        std::vector<BeanDetector::Detection> beans, numbers;
        for (const auto& r : results) {
            int cls = static_cast<int>(r.bean_type);
            if (cls >= 0 && cls <= 2) beans.push_back(r);
            else if (cls >= kDigitFirst && cls < kDigitFirst + kDigitCount)
                numbers.push_back(r);
        }

        // ---- 收 ack ----
        auto raw = serial.recvRaw(10);
        if (!raw.empty()) {
            for (auto& f : decoder.feed(raw)) {
                if (!f.is_vision && f.control.need_next) {
                    bean_waiting_ack  = false;
                    batch_waiting_ack = false;
                    if (state == State::BEAN_SENT) {
                        collect_started = false;
                        state = State::COLLECT;
                    } else if (state == State::BATCH_SENT) {
                        for (int i = 0; i < kDigitCount; ++i) digit_ok[i] = 0;
                        last_bean_class = -1;
                        state = State::IDLE;
                    }
                }
            }
        }

        if (serial.IsSimulated()) {
            if (state == State::BEAN_SENT && bean_waiting_ack) {
                bean_waiting_ack = false;
                collect_started = false;
                state = State::COLLECT;
            }
            if (state == State::BATCH_SENT && batch_waiting_ack) {
                batch_waiting_ack = false;
                for (int i = 0; i < kDigitCount; ++i) digit_ok[i] = 0;
                last_bean_class = -1;
                state = State::IDLE;
            }
        }

        // ---- IDLE ----
        if (state == State::IDLE) {
            if (!beans.empty()) {
                int best = 0;
                for (size_t i = 1; i < beans.size(); ++i)
                    if (beans[i].confidence > beans[best].confidence) best = (int)i;

                int cur_class = static_cast<int>(beans[best].bean_type);
                if (cur_class != last_bean_class) {
                    for (int i = 0; i < kDigitCount; ++i) digit_ok[i] = 0;
                    last_bean_class = cur_class;
                    std::cout << "[TX] 豆子->" << type_label(beans[best].bean_type)
                              << " 箱" << (int)map_bean_to_box(beans[best].bean_type)
                              << std::endl;
                }

                auto v = detector.toVisionData(beans[best],
                    map_bean_to_box(beans[best].bean_type));
                serial.sendVision(v);
                bean_waiting_ack = true;
                state = State::BEAN_SENT;
            }
        }

        // ---- COLLECT ----
        if (state == State::COLLECT) {
            if (!beans.empty()) {
                int best = 0;
                for (size_t i = 1; i < beans.size(); ++i)
                    if (beans[i].confidence > beans[best].confidence) best = (int)i;
                int cur_class = static_cast<int>(beans[best].bean_type);
                if (cur_class != last_bean_class) {
                    for (int i = 0; i < kDigitCount; ++i) digit_ok[i] = 0;
                    last_bean_class = cur_class;
                    collect_started = false;
                    std::cout << "[TX] 收集中断! 豆子变更->"
                              << type_label(beans[best].bean_type) << std::endl;
                }
            }

            if (!collect_started) {
                std::cout << "[TX] === 收集 ===" << std::endl;
                collect_started = true;
            }

            int before = 0;
            for (int i = 0; i < kDigitCount; ++i) if (digit_ok[i]) ++before;

            for (const auto& n : numbers) {
                int s = static_cast<int>(n.bean_type) - kDigitFirst;
                if (s < 0 || s >= kDigitCount) continue;
                if (!digit_ok[s] || n.confidence > digit_buf[s].confidence) {
                    bool is_new = !digit_ok[s];
                    digit_buf[s] = n;
                    digit_ok[s]  = 1;
                    if (is_new) {
                        std::cout << "[TX]   新: " << type_label(n.bean_type)
                                  << " (" << (before+1) << "/" << kMinCollect << ")"
                                  << std::endl;
                    }
                }
            }

            int collected = 0;
            for (int i = 0; i < kDigitCount; ++i) if (digit_ok[i]) ++collected;

            if (collected >= kMinCollect) {
                int miss_slot = -1;
                for (int i = 0; i < kDigitCount; ++i)
                    if (!digit_ok[i]) { miss_slot = i; break; }
                int miss_cls = (miss_slot >= 0) ? (kDigitFirst + miss_slot) : -1;

                struct { int slot; float x; } items[kDigitCount];
                int n = 0;
                for (int i = 0; i < kDigitCount; ++i)
                    if (digit_ok[i]) { items[n].slot = i; items[n].x = digit_buf[i].center.x; ++n; }

                for (int a = 0; a < n-1; ++a)
                    for (int b = a+1; b < n; ++b)
                        if (items[a].x > items[b].x) {
                            auto t = items[a]; items[a] = items[b]; items[b] = t;
                        }

                std::cout << "[TX] === 打包 ===" << std::endl;
                for (int i = 0; i < n; ++i) {
                    int cls = kDigitFirst + items[i].slot;
                    uint8_t bx = (uint8_t)(4 + i);
                    auto v = detector.toVisionData(digit_buf[items[i].slot], bx);
                    serial.sendVision(v);
                    std::cout << "[TX]   箱" << (int)bx
                              << " <- " << type_label(static_cast<BeanType>(cls))
                              << std::endl;
                }

                if (miss_slot >= 0) {
                    BeanDetector::Detection inf;
                    inf.bean_type  = static_cast<BeanType>(miss_cls);
                    inf.confidence = 0.0f;
                    inf.center     = cv::Point2f(0, 0);
                    inf.box        = cv::Rect(0, 0, 0, 0);

                    auto v = detector.toVisionData(inf, 0);
                    if (miss_cls >= 6) v.bean_type = BeanType::ERROR;
                    v.detected = false;
                    serial.sendVision(v);
                    std::cout << "[TX]   [推断缺失] "
                              << type_label(static_cast<BeanType>(miss_cls))
                              << std::endl;
                }

                for (int i = 0; i < kDigitCount; ++i) digit_ok[i] = 0;
                batch_waiting_ack = true;
                state = State::BATCH_SENT;
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - t_last_log).count() >= 1.0) {
            t_last_log = now;
            switch (state) {
            case State::IDLE:       std::cout << "[TX] IDLE" << std::endl; break;
            case State::BEAN_SENT:  std::cout << "[TX] BEAN_SENT" << std::endl; break;
            case State::COLLECT: { int cur = 0;
                for (int i=0;i<kDigitCount;++i) if(digit_ok[i]) ++cur;
                std::cout << "[TX] COLLECT " << cur << "/" << kMinCollect << std::endl; break; }
            case State::BATCH_SENT: std::cout << "[TX] BATCH_SENT" << std::endl; break;
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
