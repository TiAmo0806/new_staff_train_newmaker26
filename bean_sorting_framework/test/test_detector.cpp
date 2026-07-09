// test/test_detector.cpp
  // 离线测试: 用图片调检测器参数, 不需要相机
  // 对标飞镖 test/test_green_detect.cpp
  //
  // 用法: ./test_detector <图片1> <图片2> ...

  #include <iostream>
  #include <vector>
  #include <opencv2/opencv.hpp>
  #include "Detector/BeanDetector.h"
  
  int main(int argc, char** argv) {
      if (argc < 2) {
          std::cerr << "用法: " << argv[0]
                    << " <图片路径> [图片路径...]" << std::endl;
          return -1;
      }

      // 加载图片
      std::vector<cv::Mat> frames;
      for (int i = 1; i < argc; ++i) {
          cv::Mat img = cv::imread(argv[i]);
          if (img.empty()) {
              std::cerr << "无法读取: " << argv[i] << std::endl;
          } else {
              frames.push_back(img);
              std::cout << "已加载: " << argv[i] << " ("
                        << img.cols << "x" << img.rows << ")" << std::endl;
          }
      }
      if (frames.empty()) {
          std::cerr << "没有可用图片" << std::endl;
          return -1;
      }

      // 加载模型
      BeanDetector detector;
      const char* model_path = "models/bean_detect.onnx";
      if (!detector.loadModel(model_path)) {
          std::cerr << "模型加载失败: " << model_path << std::endl;
          return -1;
      }

      // ---- GUI ----
      cv::namedWindow("Controls", cv::WINDOW_AUTOSIZE);
      cv::Mat panel = cv::Mat::zeros(1, 512, CV_8UC3);
      cv::imshow("Controls", panel);

      int conf_x100  = 70;   // conf_threshold * 100
      int nms_x100   = 45;   // nms_threshold * 100

      cv::createTrackbar("Conf x100", "Controls", &conf_x100, 100);
      cv::createTrackbar("NMS x100",  "Controls", &nms_x100,  100);

      std::cout << "\n按键: ESC=退出  空格=下一张\n" << std::endl;

      int frame_idx = 0;
      while (true) {
          cv::Mat image = frames[frame_idx % frames.size()].clone();

          detector.setConfThreshold(conf_x100 / 100.0f);
          detector.setNmsThreshold(nms_x100 / 100.0f);

          auto results = detector.detect(image);
          cv::Mat debug = detector.drawResults(image, results);

          // 信息叠加
          std::string info;
          if (!results.empty()) {
              auto& best = results[0];
              info = bean_sorting::bean_type_name(best.bean_type);
              info += " " + std::to_string((int)(best.confidence * 100)) + "%";
              info += " [" + std::to_string(frame_idx % (int)frames.size() + 1)
                    + "/" + std::to_string(frames.size()) + "]";
          } else {
              info = "Not detected";
          }
          cv::putText(debug, info, cv::Point(10, 30),
                      cv::FONT_HERSHEY_SIMPLEX, 0.8,
                      cv::Scalar(0, 255, 255), 2);

          cv::imshow("Detection Result", debug);

          int key = cv::waitKey(100);
          if (key == 27) break;         // ESC
          if (key == 32) {               // Space
              ++frame_idx;
              std::cout << "切换到第 "
                        << (frame_idx % frames.size() + 1) << " 张"
                        << std::endl;
          }
      }

      cv::destroyAllWindows();
      return 0;
  }