#include "ImgProcessing/BeanSvmClassifier.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <opencv2/imgproc.hpp>

bool BeanSvmClassifier::load(const std::string &modelPath)
{
    try
    {
        svm_ = cv::Algorithm::load<cv::ml::SVM>(modelPath);   // 加载 OpenCV SVM 模型文件
    }
    catch (const std::exception &e)
    {
        std::cerr << "[BeanSvm] load failed: " << e.what() << std::endl;
        return false;                                       // 加载失败
    }
    std::cout << "[BeanSvm] model loaded: " << modelPath << std::endl;
    return isReady();                                       // 验证模型是否可用
}

bool BeanSvmClassifier::isReady() const
{
    return !svm_.empty() && svm_->isTrained();              // SVM 指针非空且已训练
}

BeanType BeanSvmClassifier::predict(const cv::Mat &roi) const
{
    // SVM 只负责豆子 ROI 分类，不负责找框。
    // roi 来自 YOLO 检测框裁剪出来的小图。
    // 如果 YOLO 框不准，SVM 也会受影响，所以它只是辅助，不是万能纠错。
    if (!isReady() || roi.empty()) return BeanType::Unknown;   // 模型未就绪或输入为空
    cv::Mat feature = extractFeature(roi);                      // 提取特征向量
    int id = static_cast<int>(svm_->predict(feature));          // SVM 预测类别编号
    if (id == 0) return BeanType::Soybean;                      // 类别 0 -> 黄豆
    if (id == 1) return BeanType::MungBean;                     // 类别 1 -> 绿豆
    if (id == 2) return BeanType::WhiteKidneyBean;              // 类别 2 -> 白芸豆
    return BeanType::Unknown;                                    // 未知类别
}

cv::Mat BeanSvmClassifier::extractFeature(const cv::Mat &roi) const
{
    // 统一尺寸，降低距离远近对特征的影响。
    // resize 到 96x96 后，不同大小的豆子框会得到同维度特征。
    cv::Mat bgr;
    if (roi.channels() == 3) bgr = roi.clone();             // 已经是 BGR，直接克隆
    else cv::cvtColor(roi, bgr, cv::COLOR_GRAY2BGR);        // 灰度转 BGR

    cv::resize(bgr, bgr, cv::Size(96, 96));                 // 统一缩放到 96x96
    cv::Mat hsv, lab, gray;

    // HSV 对颜色类别更直观：
    //   H 表示色相，绿豆/黄豆差异明显；
    //   S 表示饱和度；
    //   V 表示亮度。
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);              // BGR -> HSV

    // Lab 对光照变化比 BGR 更稳一点。
    // 白芸豆这种偏白目标，Lab 的亮度/色差有帮助。
    cv::cvtColor(bgr, lab, cv::COLOR_BGR2Lab);              // BGR -> Lab
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);            // BGR -> 灰度

    std::vector<float> feat;                                // 特征向量
    auto addMeanStd = [&](const cv::Mat &img) {
        // 均值/方差表达整体颜色和明暗。
        // 均值告诉模型”整体偏黄/偏绿/偏白”；
        // 方差告诉模型”颜色是否均匀、是否有复杂背景”。
        cv::Scalar mean, stddev;
        cv::meanStdDev(img, mean, stddev);                  // 计算均值和标准差
        for (int i = 0; i < img.channels(); ++i)
        {
            feat.push_back(static_cast<float>(mean[i] / 255.0));   // 归一化均值
            feat.push_back(static_cast<float>(stddev[i] / 255.0)); // 归一化标准差
        }
    };
    addMeanStd(hsv);                                        // HSV 三通道均值+方差 -> 6 维
    addMeanStd(lab);                                        // Lab 三通道均值+方差 -> 6 维

    // HSV 直方图用于区分黄豆、绿豆、白芸豆的颜色分布。
    // 直方图比单个平均值更细：
    // 例如同样偏黄，黄豆堆和背景纸箱的色相分布可能不同。
    std::vector<cv::Mat> hsvCh;
    cv::split(hsv, hsvCh);                                  // 拆分为 H、S、V 三个通道
    const int bins = 16;                                    // 直方图 bin 数
    const float hRange[] = {0, 180};                        // H 通道范围
    const float svRange[] = {0, 256};                       // S/V 通道范围
    const float *ranges[] = {hRange};
    cv::Mat hist;
    int histSize[] = {bins};
    int channels[] = {0};
    cv::calcHist(&hsvCh[0], 1, channels, cv::Mat(), hist, 1, histSize, ranges); // H 通道直方图
    hist /= std::max(1.0, cv::sum(hist)[0]);                // 归一化直方图
    for (int i = 0; i < bins; ++i) feat.push_back(hist.at<float>(i)); // 16 维 H 直方图

    ranges[0] = svRange;
    cv::calcHist(&hsvCh[1], 1, channels, cv::Mat(), hist, 1, histSize, ranges); // S 通道直方图
    hist /= std::max(1.0, cv::sum(hist)[0]);                // 归一化直方图
    for (int i = 0; i < bins; ++i) feat.push_back(hist.at<float>(i)); // 16 维 S 直方图

    cv::Mat row(1, static_cast<int>(feat.size()), CV_32F);  // 创建单行特征矩阵
    std::memcpy(row.ptr<float>(), feat.data(), feat.size() * sizeof(float)); // 拷贝特征数据
    return row;
}
