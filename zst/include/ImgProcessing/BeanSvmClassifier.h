#ifndef BEAN_SVM_CLASSIFIER_H
#define BEAN_SVM_CLASSIFIER_H

#include "ImgProcessing/VisionTypes.h"
#include <opencv2/core.hpp>
#include <opencv2/ml.hpp>
#include <string>

class BeanSvmClassifier
{
public:
    BeanSvmClassifier() = default;

    // 加载离线训练好的 bean_svm.yml。
    // 这个模型由 OpenCV SVM 训练得到，不是 ONNX 模型。
    bool load(const std::string &modelPath);

    // 判断 SVM 是否已经可用。
    bool isReady() const;

    // 输入 YOLO 裁出的豆子 ROI，输出豆子类别。
    // 注意：SVM 只在 YOLO 已经找到豆子框后使用。
    // 它不负责找豆子位置，只负责复核类别。
    BeanType predict(const cv::Mat &roi) const;

private:
    // 提取颜色直方图和均值方差特征。
    // 黄豆/绿豆/白芸豆主要靠颜色区分；
    // 后续如果效果不够，可以在这里继续加纹理、形状特征。
    cv::Mat extractFeature(const cv::Mat &roi) const;
    cv::Ptr<cv::ml::SVM> svm_;
};

#endif // BEAN_SVM_CLASSIFIER_H
