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
              } else {
                  break;
              }
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
          case BeanType::DATA_1:      return 4;
          case BeanType::DATA_2:      return 5;
          case BeanType::DATA_3:      return 6;
          case BeanType::DATA_4:      return 7;
          case BeanType::DATA_5:      return 8;
          default:                    return 0;
      }
  }

  int main(int argc, char* argv[]) {
      std::signal(SIGINT,  sigint_handler);
      std::signal(SIGTERM, sigint_handler);

      std::string serial_port = "/dev/ttyACM0";
      bool show_tx = false;
      bool watchdog = false;
      float conf_thresh  = 0.66f;    
      float nms_thresh   = 0.05f;    // 之前默认 0.3，调低减少重叠框

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
      detector.setConfThreshold(conf_thresh);
      detector.setNmsThreshold(nms_thresh);
      if (!detector.loadModel("models/best_opset11.onnx")) {
          std::cerr << "[Main] 模型未加载, 仅通信测试" << std::endl;
      }

      cv::namedWindow("Bean Sorting", cv::WINDOW_AUTOSIZE);

      FrameDecoder decoder;
      cv::Mat image;
      uint32_t frame_id = 0;
      bool waiting_ack = false;
      auto t_last_send = std::chrono::steady_clock::now();

      std::cout << "[Main] 主循环启动" << std::endl;

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
                                             : std::vector<BeanDetector::DetectResult>{};
          if (detector.isLoaded()) detector.drawResults(image, results);

          if (!results.empty() && !waiting_ack) {
              const auto& best = results[0];
              uint8_t box = map_bean_to_box(best.bean_type);
              VisionData v = detector.toVisionData(best, ++frame_id, box);
              serial.sendVision(v);
              waiting_ack = true;
              t_last_send = std::chrono::steady_clock::now();
          }

          auto raw = serial.recvRaw(10);
          if (!raw.empty()) {
              for (auto& f : decoder.feed(raw)) {
                  if (!f.is_vision && f.control.need_next) {
                      waiting_ack = false;
                  }
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