#include "core/AppConfig.h"
#include "input/InputManager.h"
#include "utils/DrawUtils.h"

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

struct MouseState {
    bool enabled = false;
    cv::Point point{-1, -1};
};

MouseState g_mouse_state;
int g_saved_index = 0;

void onMouse(int event, int x, int y, int, void*) {
    if (event == cv::EVENT_MOUSEMOVE) {
        g_mouse_state.point = cv::Point(x, y);
    }
}

std::string makeImagePath(const std::string& output_dir) {
    std::ostringstream oss;
    oss << std::setw(4) << std::setfill('0') << ++g_saved_index << "_preview.png";
    return (std::filesystem::path(output_dir) / oss.str()).string();
}

void drawMouseOverlay(cv::Mat& frame) {
    if (!g_mouse_state.enabled || g_mouse_state.point.x < 0 || g_mouse_state.point.y < 0) {
        return;
    }
    cv::circle(frame, g_mouse_state.point, 4, cv::Scalar(0, 200, 255), cv::FILLED);
    cv::putText(frame,
                "(" + std::to_string(g_mouse_state.point.x) + "," + std::to_string(g_mouse_state.point.y) + ")",
                g_mouse_state.point + cv::Point(8, -8),
                cv::FONT_HERSHEY_SIMPLEX,
                0.5,
                cv::Scalar(0, 200, 255),
                1);
}

void printUsage(const char* app) {
    std::cout << "Usage: " << app << " <config_path>\n";
    std::cout << "Keys: s=save current frame, r=reload config/roi, q=quit\n";
}

}  // namespace

int main(int argc, char** argv) {
    const std::string config_path = argc > 1 ? argv[1] : "config/camera_preview_demo.yaml";
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        printUsage(argv[0]);
        return 0;
    }

    AppConfig config = AppConfig::load(config_path);
    InputManager input(config.input, config.camera);
    if (!input.open()) {
        std::cerr << "Input open failed.\n";
        return 1;
    }

    const std::string window_name = "camera_preview_demo";
    g_mouse_state.enabled = config.debug.show_mouse_position;
    if (config.debug.show_window) {
        cv::namedWindow(window_name, cv::WINDOW_NORMAL);
        if (g_mouse_state.enabled) {
            cv::setMouseCallback(window_name, onMouse);
        }
    }
    std::filesystem::create_directories(config.debug.output_dir);

    cv::Mat frame;
    while (input.read(frame)) {
        if (frame.empty()) {
            continue;
        }

        cv::Mat visual = frame.clone();
        DrawUtils::drawRois(visual, config.roi);
        drawMouseOverlay(visual);

        if (config.debug.show_window) {
            cv::imshow(window_name, visual);
        }

        const int key = cv::waitKey(1);
        if (key == 'q' || key == 'Q' || key == 27) {
            break;
        }
        if (key == 's' || key == 'S') {
            const std::string output_path = makeImagePath(config.debug.output_dir);
            if (cv::imwrite(output_path, visual)) {
                std::cout << "[SAVE] " << output_path << "\n";
            } else {
                std::cout << "[WARN] failed to save " << output_path << "\n";
            }
        }
        if (key == 'r' || key == 'R') {
            config = AppConfig::load(config_path);
            g_mouse_state.enabled = config.debug.show_mouse_position;
            std::cout << "[RELOAD] " << config_path << "\n";
        }
    }

    input.release();
    return 0;
}
