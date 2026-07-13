/**
 * detection.cpp —— YOLOv8 输出解析实现
 */

#include "detection.hpp"
#include "config.hpp"

#include <algorithm>

std::vector<Detection> parseYOLOv8Output(const float* data,
                                          int numClasses, int numAnchors,
                                          int imgW, int imgH,
                                          float scale, int dw, int dh)
{
    auto& cfg = Config::get();
    std::vector<Detection> dets;

    for (int a = 0; a < numAnchors; ++a) {
        // 找到最大得分的类别
        int   bestClass = -1;
        float bestScore = -1.0f;

        for (int c = 0; c < numClasses; ++c) {
            float s = data[(4 + c) * numAnchors + a];
            if (s > bestScore) {
                bestScore = s;
                bestClass = c;
            }
        }

        if (bestScore < cfg.confidenceThreshold) continue;

        // 解析坐标 (NCHW 布局: channel * numAnchors + anchor)
        float cx = data[0 * numAnchors + a];
        float cy = data[1 * numAnchors + a];
        float w  = data[2 * numAnchors + a];
        float h  = data[3 * numAnchors + a];

        // 还原到填充后的 640×640 坐标
        float x1 = cx - w * 0.5f;
        float y1 = cy - h * 0.5f;
        float x2 = cx + w * 0.5f;
        float y2 = cy + h * 0.5f;

        // 去除 letterbox 填充偏移，再还原到原始尺寸
        x1 = (x1 - dw) / scale;
        y1 = (y1 - dh) / scale;
        x2 = (x2 - dw) / scale;
        y2 = (y2 - dh) / scale;

        // 裁剪到图像边界
        x1 = std::max(0.0f, std::min(x1, static_cast<float>(imgW) - 1.0f));
        y1 = std::max(0.0f, std::min(y1, static_cast<float>(imgH) - 1.0f));
        x2 = std::max(0.0f, std::min(x2, static_cast<float>(imgW) - 1.0f));
        y2 = std::max(0.0f, std::min(y2, static_cast<float>(imgH) - 1.0f));

        Detection det;
        det.classId    = bestClass;
        det.confidence = bestScore;
        det.box        = cv::Rect2f(x1, y1, x2 - x1, y2 - y1);
        dets.push_back(det);
    }

    // ---- NMS ----
    std::vector<Detection> nmsResults;
    if (dets.empty()) return nmsResults;

    std::sort(dets.begin(), dets.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<bool> suppressed(dets.size(), false);
    for (size_t i = 0; i < dets.size(); ++i) {
        if (suppressed[i]) continue;
        nmsResults.push_back(dets[i]);
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (suppressed[j]) continue;
            if (dets[i].classId != dets[j].classId) continue;
            float interX1 = std::max(dets[i].box.x, dets[j].box.x);
            float interY1 = std::max(dets[i].box.y, dets[j].box.y);
            float interX2 = std::min(dets[i].box.x + dets[i].box.width,
                                     dets[j].box.x + dets[j].box.width);
            float interY2 = std::min(dets[i].box.y + dets[i].box.height,
                                     dets[j].box.y + dets[j].box.height);
            if (interX1 >= interX2 || interY1 >= interY2) continue;
            float interArea = (interX2 - interX1) * (interY2 - interY1);
            float areaI = dets[i].box.width  * dets[i].box.height;
            float areaJ = dets[j].box.width  * dets[j].box.height;
            float iou = interArea / (areaI + areaJ - interArea);
            if (iou > cfg.nmsThreshold) {
                suppressed[j] = true;
            }
        }
    }
    return nmsResults;
}
