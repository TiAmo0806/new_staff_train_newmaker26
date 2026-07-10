/**
 * preprocess.cpp —— 图像预处理实现
 */

#include "preprocess.hpp"
#include "config.hpp"

cv::Mat preprocess(const cv::Mat& src, int& dw, int& dh, float& scale)
{
    auto& cfg = Config::get();
    int w = src.cols, h = src.rows;
    scale = std::min(static_cast<float>(cfg.inputWidth)  / w,
                     static_cast<float>(cfg.inputHeight) / h);
    int newW = static_cast<int>(w * scale);
    int newH = static_cast<int>(h * scale);
    dw = (cfg.inputWidth  - newW) / 2;
    dh = (cfg.inputHeight - newH) / 2;

    cv::Mat resized, padded;
    cv::resize(src, resized, cv::Size(newW, newH));
    cv::copyMakeBorder(resized, padded, dh, dh, dw, dw,
                       cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));

    // HWC -> CHW, BGR -> RGB, /255.0
    cv::Mat blob = cv::dnn::blobFromImage(padded, 1.0 / 255.0,
                                          cv::Size(cfg.inputWidth, cfg.inputHeight),
                                          cv::Scalar(), true, false);
    return blob;
}
