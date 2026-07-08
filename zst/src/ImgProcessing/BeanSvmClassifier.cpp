#include "/home/zst/zst/include/ImgProcessing/BeanSvmClassifier.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <opencv2/imgproc.hpp>

bool BeanSvmClassifier::load(const std::string &modelPath)
{
    try
    {
        svm_ = cv::Algorithm::load<cv::ml::SVM>(modelPath);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[BeanSvm] load failed: " << e.what() << std::endl;
        return false;
    }
    std::cout << "[BeanSvm] model loaded: " << modelPath << std::endl;
    return isReady();
}

bool BeanSvmClassifier::isReady() const
{
    return !svm_.empty() && svm_->isTrained();
}

BeanType BeanSvmClassifier::predict(const cv::Mat &roi) const
{
    // SVM 只负责豆子 ROI 分类，不负责找框。
    // roi 来自 YOLO 检测框裁剪出来的小图。
    // 如果 YOLO 框不准，SVM 也会受影响，所以它只是辅助，不是万能纠错。
    if (!isReady() || roi.empty()) return BeanType::Unknown;
    cv::Mat feature = extractFeature(roi);
    int id = static_cast<int>(svm_->predict(feature));
    if (id == 0) return BeanType::Soybean;
    if (id == 1) return BeanType::MungBean;
    if (id == 2) return BeanType::WhiteKidneyBean;
    return BeanType::Unknown;
}

cv::Mat BeanSvmClassifier::extractFeature(const cv::Mat &roi) const
{
    // 统一尺寸，降低距离远近对特征的影响。
    // resize 到 96x96 后，不同大小的豆子框会得到同维度特征。
    cv::Mat bgr;
    if (roi.channels() == 3) bgr = roi.clone();
    else cv::cvtColor(roi, bgr, cv::COLOR_GRAY2BGR);

    cv::resize(bgr, bgr, cv::Size(96, 96));
    cv::Mat hsv, lab, gray;

    // HSV 对颜色类别更直观：
    //   H 表示色相，绿豆/黄豆差异明显；
    //   S 表示饱和度；
    //   V 表示亮度。
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);

    // Lab 对光照变化比 BGR 更稳一点。
    // 白芸豆这种偏白目标，Lab 的亮度/色差有帮助。
    cv::cvtColor(bgr, lab, cv::COLOR_BGR2Lab);
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    std::vector<float> feat;
    auto addMeanStd = [&](const cv::Mat &img) {
        // 均值/方差表达整体颜色和明暗。
        // 均值告诉模型“整体偏黄/偏绿/偏白”；
        // 方差告诉模型“颜色是否均匀、是否有复杂背景”。
        cv::Scalar mean, stddev;
        cv::meanStdDev(img, mean, stddev);
        for (int i = 0; i < img.channels(); ++i)
        {
            feat.push_back(static_cast<float>(mean[i] / 255.0));
            feat.push_back(static_cast<float>(stddev[i] / 255.0));
        }
    };
    addMeanStd(hsv);
    addMeanStd(lab);

    // HSV 直方图用于区分黄豆、绿豆、白芸豆的颜色分布。
    // 直方图比单个平均值更细：
    // 例如同样偏黄，黄豆堆和背景纸箱的色相分布可能不同。
    std::vector<cv::Mat> hsvCh;
    cv::split(hsv, hsvCh);
    const int bins = 16;
    const float hRange[] = {0, 180};
    const float svRange[] = {0, 256};
    const float *ranges[] = {hRange};
    cv::Mat hist;
    int histSize[] = {bins};
    int channels[] = {0};
    cv::calcHist(&hsvCh[0], 1, channels, cv::Mat(), hist, 1, histSize, ranges);
    hist /= std::max(1.0, cv::sum(hist)[0]);
    for (int i = 0; i < bins; ++i) feat.push_back(hist.at<float>(i));

    ranges[0] = svRange;
    cv::calcHist(&hsvCh[1], 1, channels, cv::Mat(), hist, 1, histSize, ranges);
    hist /= std::max(1.0, cv::sum(hist)[0]);
    for (int i = 0; i < bins; ++i) feat.push_back(hist.at<float>(i));

    cv::Mat row(1, static_cast<int>(feat.size()), CV_32F);
    std::memcpy(row.ptr<float>(), feat.data(), feat.size() * sizeof(float));
    return row;
}
