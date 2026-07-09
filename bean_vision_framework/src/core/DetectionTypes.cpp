#include "core/DetectionTypes.h"

/**
 * @brief 计算检测框中心点。
 * @return 检测框中心点，单位为图像像素。
 */
cv::Point Detection::center() const {
    return cv::Point(box.x + box.width / 2, box.y + box.height / 2);
}
