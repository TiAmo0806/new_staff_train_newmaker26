#include "utils/DebugLogger.h"

#include "utils/DrawUtils.h"

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

int g_debug_image_index = 0;

std::string sanitizeStem(const std::string& image_path) {
    std::string stem = std::filesystem::path(image_path).stem().string();
    if (stem.empty()) {
        stem = "frame";
    }
    for (char& ch : stem) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-')) {
            ch = '_';
        }
    }
    return stem;
}

std::filesystem::path makeDebugPath(const std::string& event_name,
                                    const std::string& image_path,
                                    const std::string& suffix,
                                    int index,
                                    const std::string& output_dir) {
    std::ostringstream name;
    name << std::setw(4) << std::setfill('0') << index
         << "_" << event_name
         << "_" << sanitizeStem(image_path)
         << "_" << suffix << ".png";
    return std::filesystem::path(output_dir) / name.str();
}

std::filesystem::path resolveOutputDir(const AppConfig& config) {
    std::filesystem::path output_dir(config.debug.output_dir.empty() ? "debug_output" : config.debug.output_dir);
    if (output_dir.is_absolute()) {
        return output_dir;
    }

    const std::filesystem::path base_dir(config.base_dir);
    if (base_dir.has_parent_path()) {
        return base_dir.parent_path() / output_dir;
    }
    return std::filesystem::current_path() / output_dir;
}

}  // namespace

void DebugLogger::saveCommandImages(const std::string& event_name,
                                    const std::string& image_path,
                                    const cv::Mat& frame,
                                    const std::vector<Detection>& detections,
                                    const VisionResult& result,
                                    const AppConfig& config,
                                    bool force_save) {
    if (frame.empty()) {
        return;
    }

    const bool save_raw = force_save || config.debug.save_raw_frame;
    const bool save_result = force_save || config.debug.save_result_image;
    const bool show_window = config.debug.show_window;
    const std::filesystem::path output_dir = resolveOutputDir(config);
    if (!save_raw && !save_result && !show_window) {
        return;
    }

    if (save_raw || save_result) {
        std::filesystem::create_directories(output_dir);
    }
    const int image_index = ++g_debug_image_index;

    if (save_raw) {
        const std::filesystem::path raw_path =
            makeDebugPath(event_name, image_path, "raw", image_index, output_dir.string());
        if (cv::imwrite(raw_path.string(), frame)) {
            std::cout << "[DEBUG IMAGE] raw=" << raw_path.string() << "\n";
        } else {
            std::cout << "[WARN] failed to save raw debug image: " << raw_path.string() << "\n";
        }
    }

    cv::Mat visual;
    if (save_result || show_window) {
        // 结果图统一叠加 ROI、YOLO 框和 ROI 解析后的固定点结果，便于现场调试。
        visual = frame.clone();
        DrawUtils::drawRois(visual, config.roi);
        DrawUtils::drawDetections(visual, detections);
        DrawUtils::drawVisionResult(visual, result);
    }

    if (save_result) {
        if (visual.empty()) {
            visual = frame.clone();
        }
        const std::filesystem::path result_path =
            makeDebugPath(event_name, image_path, "result", image_index, output_dir.string());
        if (cv::imwrite(result_path.string(), visual)) {
            std::cout << "[DEBUG IMAGE] result=" << result_path.string() << "\n";
        } else {
            std::cout << "[WARN] failed to save result debug image: " << result_path.string() << "\n";
        }
    }

    if (show_window && !visual.empty()) {
        const std::string window_name = "bean_vision_debug_" + event_name;
        cv::imshow(window_name, visual);
        // 命令模式下终端会阻塞在下一次输入，因此这里需要等待按键，
        // 否则窗口只会短暂弹出却来不及真正刷新显示。
        cv::waitKey(0);
        cv::destroyWindow(window_name);
    }
}
