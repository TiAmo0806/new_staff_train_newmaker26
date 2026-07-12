#include "BeanDetector.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// ===================================================================
//  DetectorPreprocessor
// ===================================================================

cv::Mat DetectorPreprocessor::run(const cv::Mat& src) {
    int tw = inputW_, th = inputH_;
    scale_ = std::min((float)tw / src.cols, (float)th / src.rows);
    int nw = (int)(src.cols * scale_), nh = (int)(src.rows * scale_);
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(nw, nh));
    padX_ = (float)(tw - nw) / 2.0f;
    padY_ = (float)(th - nh) / 2.0f;
    cv::Mat lb;
    cv::copyMakeBorder(resized, lb,
                       (int)padY_, (int)(th - nh - padY_),
                       (int)padX_, (int)(tw - nw - padX_),
                       cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
    return cv::dnn::blobFromImage(lb, 1.0 / 255.0, cv::Size(tw, th),
                                  cv::Scalar(), true, false);
}

// ===================================================================
//  DetectorPostprocessor
// ===================================================================

static float sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }

std::vector<DetectorPostprocessor::Detection>
DetectorPostprocessor::decode(const float* dd, int D, int N,
                              float invScale, float padX, float padY,
                              int imgW, int imgH) {
    std::vector<Detection> rs;
    int NC = N - 4;

    int gw0 = inputW_ / 8,  gh0 = inputH_ / 8;
    int gw1 = inputW_ / 16, gh1 = inputH_ / 16;
    int gw2 = inputW_ / 32, gh2 = inputH_ / 32;
    int cnt0 = gw0 * gh0, cnt1 = gw1 * gh1, cnt2 = gw2 * gh2;
    int ST[3] = {8, 16, 32};
    int GW[3] = {gw0, gw1, gw2};
    int OFF[4] = {0, cnt0, cnt0 + cnt1, cnt0 + cnt1 + cnt2};

    float thr = confThr_;
    std::vector<float>    sc;
    std::vector<int>      ci;
    std::vector<cv::Rect> bx;

    for (int x = 0; x < D; ++x) {
        float bs = 0.0f; int bc = -1;
        for (int c = 0; c < NC; ++c) {
            float s = sigmoid(dd[(4 + c) * D + x]);
            if (s > bs) { bs = s; bc = c; }
        }
        if (bs < thr) continue;

        float cx = dd[0 * D + x];
        float cy = dd[1 * D + x];
        float w  = dd[2 * D + x];
        float h_ = dd[3 * D + x];

        float x1 = (cx - w  / 2.0f - padX) * invScale;
        float y1 = (cy - h_ / 2.0f - padY) * invScale;
        float x2 = (cx + w  / 2.0f - padX) * invScale;
        float y2 = (cy + h_ / 2.0f - padY) * invScale;

        int L = (int)(x1 + 0.5f), T = (int)(y1 + 0.5f);
        int R = (int)(x2 + 0.5f), B = (int)(y2 + 0.5f);
        if (L < 0) L = 0; if (T < 0) T = 0;
        if (R > imgW) R = imgW; if (B > imgH) B = imgH;
        int W = R - L, H = B - T;
        if (W < 5 || H < 5) continue;

        sc.push_back(bs); ci.push_back(bc);
        bx.push_back(cv::Rect(L, T, W, H));
    }

    if (bx.empty()) return rs;

    std::vector<int> ndx;
    std::vector<std::vector<int> > cls_idx(NC);
    for (int i = 0; i < (int)sc.size(); ++i) {
        int c = ci[i]; if (c >= 0 && c < NC) cls_idx[c].push_back(i);
    }
    for (int c = 0; c < NC; ++c) {
        if (cls_idx[c].empty()) continue;
        std::vector<cv::Rect> cbx; std::vector<float> csc;
        for (int idx : cls_idx[c]) { cbx.push_back(bx[idx]); csc.push_back(sc[idx]); }
        std::vector<int> cndx;
        cv::dnn::NMSBoxes(cbx, csc, thr, nmsThr_, cndx);
        for (int k : cndx) ndx.push_back(cls_idx[c][k]);
    }
    if (ndx.empty()) {
        int hi = 0;
        for (int i = 1; i < (int)sc.size(); ++i) if (sc[i] > sc[hi]) hi = i;
        ndx.push_back(hi);
    }
    for (int i : ndx) {
        Detection r; int id = ci[i];
        if (id < 0 || id >= NC) id = NC - 1;
        r.bean_type  = static_cast<bean_sorting::BeanType>(id);
        r.box        = bx[i];
        r.center     = cv::Point2f((float)bx[i].x + (float)bx[i].width  / 2.0f,
                                   (float)bx[i].y + (float)bx[i].height / 2.0f);
        r.confidence = sc[i];
        rs.push_back(r);
    }
    std::sort(rs.begin(), rs.end());
    return rs;
}

// ===================================================================
//  DetectorVisualizer
// ===================================================================

static const char* kClassName(int id) {
    switch (id) {
        case 0: return "soybean"; case 1: return "mung";
        case 2: return "kidney";  case 3: return "1";
        case 4: return "2";       case 5: return "3";
        case 6: return "4";       case 7: return "5";
        default: return "?";
    }
}

void DetectorVisualizer::draw(
        cv::Mat& img,
        const std::vector<DetectorPostprocessor::Detection>& rs,
        float confThr, float nmsThr, int numCls) {
    debug_ = img.clone();
    cv::Scalar CO[8] = {
        cv::Scalar(0, 255, 255), cv::Scalar(0, 255, 0),
        cv::Scalar(255, 255, 255), cv::Scalar(255, 0, 0),
        cv::Scalar(0, 200, 200), cv::Scalar(200, 0, 200),
        cv::Scalar(0, 0, 255), cv::Scalar(100, 100, 100)
    };
    for (size_t i = 0; i < rs.size(); ++i) {
        const DetectorPostprocessor::Detection& r = rs[i];
        int idx = (int)r.bean_type;
        if (idx < 0 || idx > 7) idx = 7;
        cv::rectangle(img, r.box, CO[idx], 2);
        cv::circle(img, r.center, 3, cv::Scalar(0, 0, 255), -1);
        char buf[128];
        snprintf(buf, sizeof(buf), "%s %.0f%% %dx%d",
                 kClassName(idx), r.confidence * 100.0f,
                 r.box.width, r.box.height);
        std::string label(buf);
        cv::Point lp(r.box.x, r.box.y - 5);
        if (lp.y < 10) lp.y = r.box.y + r.box.height + 20;
        cv::Size ts = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.45, 2, NULL);
        cv::rectangle(img, cv::Point(lp.x - 2, lp.y - ts.height - 2),
                      cv::Point(lp.x + ts.width + 2, lp.y + 2), CO[idx], cv::FILLED);
        cv::putText(img, label, lp, cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(0, 0, 0), 2);
    }
    char st[128];
    snprintf(st, sizeof(st), "Beans: %zu  thr=%.2f  nms=%.2f  NC=%d",
             rs.size(), confThr, nmsThr, numCls);
    cv::putText(img, st, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2);
}

// ===================================================================
//  BeanDetector
// ===================================================================

BeanDetector::BeanDetector() {}
BeanDetector::~BeanDetector() {
    if (session_) { delete session_; session_ = NULL; }
    if (env_)     { delete env_;     env_     = NULL; }
    for (size_t i = 0; i < input_names_.size(); ++i)  free((void*)input_names_[i]);
    for (size_t i = 0; i < output_names_.size(); ++i) free((void*)output_names_[i]);
}

void BeanDetector::setConfThreshold(float v) { confThreshold_ = v; postproc_.setConfThreshold(v); }
void BeanDetector::setNmsThreshold(float v)  { nmsThreshold_  = v; postproc_.setNmsThreshold(v); }
void BeanDetector::setInputSize(int w, int h) {
    inputWidth_ = w; inputHeight_ = h;
    preproc_.setInputSize(w, h); postproc_.setInputSize(w, h);
}
bool BeanDetector::isLoaded() const { return loaded_; }
cv::Mat BeanDetector::getDebugImage() const { return visual_.debug(); }

bool BeanDetector::loadModel(const std::string& mp) {
    try {
        env_ = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "BD");
        Ort::SessionOptions o; o.SetIntraOpNumThreads(2);
        o.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_ = new Ort::Session(*env_, mp.c_str(), o);
        for (size_t i = 0; i < session_->GetInputCount(); ++i) {
            auto nm = session_->GetInputNameAllocated(i, allocator_);
            input_names_.push_back(strdup(nm.get()));
            auto type_info  = session_->GetInputTypeInfo(i);
            auto shape_info = type_info.GetTensorTypeAndShapeInfo();
            std::vector<int64_t> sh = shape_info.GetShape();
            for (auto& d : sh) if (d < 0) d = 1;
            if (i == 0 && sh.size() >= 3) { inputWidth_ = (int)sh[3]; inputHeight_ = (int)sh[2]; }
        }
        for (size_t i = 0; i < session_->GetOutputCount(); ++i) {
            auto nm = session_->GetOutputNameAllocated(i, allocator_);
            output_names_.push_back(strdup(nm.get()));
            auto type_info  = session_->GetOutputTypeInfo(i);
            auto shape_info = type_info.GetTensorTypeAndShapeInfo();
            std::vector<int64_t> sh = shape_info.GetShape();
            output_shapes_.push_back(std::vector<int64_t>(sh.begin(), sh.end()));
        }
        int N = (int)output_shapes_[0][1], D = (int)output_shapes_[0][2];
        num_classes_ = N - 4;
        preproc_.setInputSize(inputWidth_, inputHeight_);
        postproc_.setInputSize(inputWidth_, inputHeight_);
        postproc_.setNumClasses(num_classes_);
        std::cout << "[BD] loaded " << inputWidth_ << "x" << inputHeight_
                  << "  output[1," << N << "," << D << "]  classes=" << num_classes_ << std::endl;
        loaded_ = true; return true;
    } catch (const std::exception& e) { std::cerr << "[BD] " << e.what() << std::endl; return false; }
}

std::vector<BeanDetector::Detection> BeanDetector::detect(const cv::Mat& img) {
    std::vector<Detection> rs;
    if (!loaded_ || img.empty()) return rs;
    try {
        cv::Mat blob = preproc_.run(img);
        auto type_info  = session_->GetInputTypeInfo(0);
        auto shape_info = type_info.GetTensorTypeAndShapeInfo();
        std::vector<int64_t> sh = shape_info.GetShape();
        for (auto& d : sh) if (d < 0) d = 1;
        Ort::MemoryInfo mi = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        int64_t total = 1; for (auto d : sh) total *= d;
        Ort::Value it = Ort::Value::CreateTensor<float>(mi, (float*)blob.ptr<float>(), total, sh.data(), sh.size());
        auto ot = session_->Run(Ort::RunOptions{nullptr}, input_names_.data(), &it, 1,
                                output_names_.data(), output_names_.size());
        float* dd = ot[0].GetTensorMutableData<float>();
        rs = postproc_.decode(dd, (int)output_shapes_[0][2], (int)output_shapes_[0][1],
                              1.0f / preproc_.scale(), preproc_.padX(), preproc_.padY(),
                              img.cols, img.rows);
    } catch (const std::exception& e) { std::cerr << "[BD] " << e.what() << std::endl; }
    return rs;
}

cv::Mat& BeanDetector::drawResults(cv::Mat& img, const std::vector<Detection>& rs) {
    visual_.draw(img, rs, confThreshold_, nmsThreshold_, num_classes_);
    return img;
}

bean_sorting::VisionData BeanDetector::toVisionData(const Detection& b, uint8_t bx) const {
    bean_sorting::VisionData v;

    if (b.bean_type == bean_sorting::BeanType::DATA_4 ||
        b.bean_type == bean_sorting::BeanType::DATA_5) {
        v.bean_type = bean_sorting::BeanType::ERROR;
    } else {
        v.bean_type = b.bean_type;
    }

    v.target_box = bx;
    v.detected   = true;
    return v;
}
