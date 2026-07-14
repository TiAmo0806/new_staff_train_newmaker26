/**
 * visualization.cpp —— 绘制检测结果与统计面板实现
 */

#include "visualization.hpp"
#include "config.hpp"

#include <cstdio>
#include <string>

void drawDetections(cv::Mat& frame, const std::vector<Detection>& dets,
                    const std::vector<cv::Scalar>& colors,
                    const std::vector<std::string>& classNames,
                    const Config& cfg)
{
    constexpr int FONT = cv::FONT_HERSHEY_SIMPLEX;

    for (const auto& d : dets) {
        const auto& color = colors[d.classId % colors.size()];
        const auto& name  = (d.classId < static_cast<int>(classNames.size()))
                                ? classNames[d.classId]
                                : "unknown";

        cv::Rect rect(static_cast<int>(d.box.x),
                      static_cast<int>(d.box.y),
                      static_cast<int>(d.box.width),
                      static_cast<int>(d.box.height));

        cv::rectangle(frame, rect, color, cfg.lineThickness);

        char label[128];
        snprintf(label, sizeof(label), "%s %.2f", name.c_str(), d.confidence);
        int baseline = 0;
        cv::Size textSz = cv::getTextSize(label, FONT, cfg.fontScale,
                                          cfg.fontThickness, &baseline);

        cv::Point labelTopLeft(rect.x, rect.y - textSz.height - 6);
        cv::Point labelBotRight(rect.x + textSz.width + 4, rect.y);
        if (labelTopLeft.y < 0) {
            labelTopLeft.y = rect.y;
            labelBotRight.y = rect.y + textSz.height + 4;
        }
        cv::rectangle(frame, labelTopLeft, labelBotRight, color, cv::FILLED);
        cv::putText(frame, label,
                    cv::Point(rect.x + 2, labelBotRight.y - baseline - 2),
                    FONT, cfg.fontScale, cv::Scalar(255, 255, 255),
                    cfg.fontThickness, cv::LINE_AA);
    }
}

// ---- 豆类别名称简写 ----
static const char* beanName(uint8_t cls) {
    switch (cls) {
        case 0: return "Soybean";
        case 1: return "Mung";
        case 2: return "WhiteKidney";
        default: return "None";
    }
}

void drawBeanPositions(cv::Mat& frame, const BeanPositionResult& pos,
                       const Config& cfg)
{
    constexpr int FONT = cv::FONT_HERSHEY_SIMPLEX;
    const int fh = frame.rows;
    const int fw = frame.cols;

    // ---- 画三厢分隔线 ----
    const int x1 = fw / 3;
    const int x2 = fw * 2 / 3;
    cv::line(frame, cv::Point(x1, 0), cv::Point(x1, fh),
             cv::Scalar(100, 100, 100), 1, cv::LINE_AA);
    cv::line(frame, cv::Point(x2, 0), cv::Point(x2, fh),
             cv::Scalar(100, 100, 100), 1, cv::LINE_AA);

    // ---- 在底部绘制位置标签和对应箱子 ----
    const int barH = 60;
    const int barY = fh - barH;

    // 半透明底栏
    cv::Mat overlay = frame.clone();
    cv::rectangle(overlay, cv::Point(0, barY), cv::Point(fw, fh),
                  cv::Scalar(30, 30, 30), cv::FILLED);
    cv::addWeighted(overlay, 0.7, frame, 0.3, 0, frame);

    struct PosInfo {
        std::string posName;
        uint8_t    beanClass;
        int        zoneCx;
    };

    PosInfo infos[3] = {
        {"LEFT",  pos.leftBean,  x1 / 2},
        {"MID",   pos.midBean,   (x1 + x2) / 2},
        {"RIGHT", pos.rightBean, (x2 + fw) / 2}
    };

    for (const auto& info : infos) {
        int boxNum = (info.beanClass <= 2 && info.beanClass < static_cast<int>(cfg.beanToBox.size()))
                         ? cfg.beanToBox[info.beanClass]
                         : 0;
        const char* bName = beanName(info.beanClass);

        char line1[64], line2[64];
        snprintf(line1, sizeof(line1), "%s", info.posName.c_str());
        snprintf(line2, sizeof(line2), "%s -> Box%d", bName, boxNum);

        cv::Scalar posColor = (info.beanClass <= 2)
            ? cv::Scalar(0, 255, 0)
            : cv::Scalar(100, 100, 255);

        int tx1 = info.zoneCx - 40;
        cv::putText(frame, line1, cv::Point(tx1, barY + 28),
                    FONT, 0.7, posColor, 2, cv::LINE_AA);
        cv::putText(frame, line2, cv::Point(tx1, barY + 52),
                    FONT, 0.55, cv::Scalar(220, 220, 220), 1, cv::LINE_AA);
    }
}

void drawStats(cv::Mat& frame, int beanCount, int numberCount,
               double fps, double latencyMs)
{
    constexpr int FONT = cv::FONT_HERSHEY_SIMPLEX;
    char buf[256];
    int y = 28;
    auto put = [&](const std::string& text, cv::Scalar color) {
        cv::putText(frame, text, cv::Point(10, y), FONT, 0.7, color, 2, cv::LINE_AA);
        y += 28;
    };

    snprintf(buf, sizeof(buf), "Beans : %d", beanCount);
    put(buf, cv::Scalar(0, 255, 0));
    snprintf(buf, sizeof(buf), "Numbers: %d", numberCount);
    put(buf, cv::Scalar(255, 0, 0));
    snprintf(buf, sizeof(buf), "FPS   : %.1f", fps);
    put(buf, cv::Scalar(255, 255, 255));
    snprintf(buf, sizeof(buf), "Latency: %.1f ms", latencyMs);
    put(buf, cv::Scalar(200, 200, 200));
}
