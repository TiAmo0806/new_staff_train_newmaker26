#ifndef BEAN_DETECTOR_H
  #define BEAN_DETECTOR_H

  #include <string>
  #include <vector>
  #include <cstdint>
  #include <opencv2/opencv.hpp>
  #include <opencv2/dnn.hpp>
  #include <onnxruntime_cxx_api.h>
  #include "Communication/BeanProtocol.h"

  class DetectorPreprocessor {
  public:
      void setInputSize(int w, int h) { inputW_ = w; inputH_ = h; }
      int  inputW() const { return inputW_; }
      int  inputH() const { return inputH_; }
      float padX()  const { return padX_; }
      float padY()  const { return padY_; }
      float scale() const { return scale_; }
      cv::Mat run(const cv::Mat& src);
  private:
      int   inputW_ = 640, inputH_ = 640;
      float padX_ = 0, padY_ = 0, scale_ = 1;
  };

  class DetectorPostprocessor {
  public:
      struct Detection {
          bean_sorting::BeanType bean_type;
          cv::Rect   box;
          cv::Point2f center;
          float      confidence = 0;
          bool operator<(const Detection& o) const { return confidence > o.confidence; }
      };
      void setConfThreshold(float v) { confThr_ = v; }
      void setNmsThreshold(float v)  { nmsThr_  = v; }
      void setInputSize(int w, int h) { inputW_ = w; inputH_ = h; }
      void setNumClasses(int n) { numCls_ = n; }
      std::vector<Detection> decode(const float* data, int D, int N,
                                    float invScale, float padX, float padY,
                                    int imgW, int imgH);
  private:
      float confThr_ = 0.2f, nmsThr_ = 0.2f;
      int   numCls_ = 0, inputW_ = 640, inputH_ = 640;
  };

  class DetectorVisualizer {
  public:
      cv::Mat debug() const { return debug_.clone(); }
      void draw(cv::Mat& img,
                const std::vector<DetectorPostprocessor::Detection>& dets,
                float confThr, float nmsThr, int numCls);
  private:
      cv::Mat debug_;
  };

  class BeanDetector {
  public:
      using Detection = DetectorPostprocessor::Detection;
      typedef Detection DetectResult;

      BeanDetector();
      ~BeanDetector();

      bool loadModel(const std::string& mp);
      void setConfThreshold(float v);
      void setNmsThreshold(float v);
      void setInputSize(int w, int h);
      bool isLoaded() const;
      cv::Mat getDebugImage() const;
      std::vector<Detection> detect(const cv::Mat& img);
      cv::Mat& drawResults(cv::Mat& img, const std::vector<Detection>& rs);
      bean_sorting::VisionData toVisionData(const Detection& b, uint32_t fid, uint8_t bx) const;

  private:
      DetectorPreprocessor  preproc_;
      DetectorPostprocessor postproc_;
      DetectorVisualizer    visual_;
      Ort::Env*             env_     = nullptr;
      Ort::Session*         session_ = nullptr;
      Ort::AllocatorWithDefaultOptions allocator_;
      std::vector<char*>               input_names_;
      std::vector<char*>               output_names_;
      std::vector<std::vector<int64_t>> output_shapes_;
      float confThreshold_ = 0.2f, nmsThreshold_ = 0.2f;
      bool  loaded_      = false;
      int   inputWidth_  = 640, inputHeight_ = 640;
      int   num_classes_ = 0;
  };

  #endif
