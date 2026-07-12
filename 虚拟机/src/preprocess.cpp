#include "/home/hu/mvs_openvino_demo/include/preprocess.hpp"

cv::Mat preprocess(const cv::Mat& frame) {
    // 1. BGR → RGB
    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);

    // 2. Resize → 640×640
    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(640, 640));

    // 3. uint8 [0,255] → float32 [0,1]
    cv::Mat blob;
    resized.convertTo(blob, CV_32F, 1.0 / 255.0);

    return blob;
}

ov::Tensor blob_to_tensor(const cv::Mat& blob) {
    int h = blob.rows;
    int w = blob.cols;
    int c = blob.channels();

    // NCHW: [1, C, H, W]
    ov::Shape shape = {1, static_cast<size_t>(c),
                       static_cast<size_t>(h), static_cast<size_t>(w)};
    ov::Tensor tensor(ov::element::f32, shape);
    float* data = tensor.data<float>();

    // HWC → CHW
    for (int ch = 0; ch < c; ++ch) {
        for (int row = 0; row < h; ++row) {
            const float* src = blob.ptr<float>(row) + ch;
            float* dst = data + ch * h * w + row * w;
            for (int col = 0; col < w; ++col) {
                dst[col] = *src;
                src += c;
            }
        }
    }

    return tensor;
}
