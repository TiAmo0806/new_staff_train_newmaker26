#include "core/AppConfig.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

/**
 * @brief 去掉字符串左右两侧的空白字符。
 * @param text 原始字符串。
 * @return 去掉首尾空白后的字符串。
 */
std::string trim(const std::string& text) {
    // 去掉字符串左右两边的空白字符，便于解析简单配置文件。
    const auto begin = std::find_if_not(text.begin(), text.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto end = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

/**
 * @brief 去掉配置行中的注释内容。
 * @param line 原始配置行。
 * @return 去掉 # 之后内容并裁剪空白后的字符串。
 */
std::string stripComment(const std::string& line) {
    // 支持 YAML 里的 # 注释：只解析 # 前面的内容。
    const auto pos = line.find('#');
    return trim(pos == std::string::npos ? line : line.substr(0, pos));
}

/**
 * @brief 去掉配置行中的注释，但保留前导缩进。
 * @param line 原始配置行。
 * @return 去掉注释并去掉末尾空白后的字符串。
 */
std::string stripCommentKeepIndent(const std::string& line) {
    const auto pos = line.find('#');
    std::string kept = pos == std::string::npos ? line : line.substr(0, pos);
    while (!kept.empty() && std::isspace(static_cast<unsigned char>(kept.back())) != 0) {
        kept.pop_back();
    }
    return kept;
}

/**
 * @brief 统计一行前导空白字符数量。
 * @param line 保留了前导空白的配置行。
 * @return 前导空白字符个数。
 */
int leadingIndent(const std::string& line) {
    int indent = 0;
    while (indent < static_cast<int>(line.size()) &&
           std::isspace(static_cast<unsigned char>(line[static_cast<size_t>(indent)])) != 0) {
        ++indent;
    }
    return indent;
}

/**
 * @brief 去掉配置值两边的单引号或双引号。
 * @param value 原始配置值。
 * @return 去掉引号后的配置值。
 */
std::string unquote(std::string value) {
    // 去掉配置值两边的单引号或双引号。
    value = trim(value);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

/**
 * @brief 将配置中的文本解析为布尔值。
 * @param value 原始文本，例如 true、1、yes。
 * @return 表示真值时返回 true，否则返回 false。
 */
bool parseBool(const std::string& value) {
    // 简单支持 true/1/yes 三种真值写法。
    const std::string v = unquote(value);
    return v == "true" || v == "1" || v == "yes";
}

/**
 * @brief 根据配置文件目录解析相对路径。
 * @param base_dir 当前 app.yaml 所在目录。
 * @param path 配置中写的路径，可以是绝对路径或相对路径。
 * @return 解析后的文件路径。
 */
std::filesystem::path resolvePath(const std::filesystem::path& base_dir, const std::string& path) {
    // 配置文件里常写相对路径。
    // 这里按当前工作目录、配置文件目录、工程根目录依次尝试，减少运行目录变化带来的问题。
    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return p;
    }
    if (std::filesystem::exists(p)) {
        return p;
    }
    if (std::filesystem::exists(base_dir / p)) {
        return base_dir / p;
    }
    if (base_dir.has_parent_path() && std::filesystem::exists(base_dir.parent_path() / p)) {
        return base_dir.parent_path() / p;
    }
    return base_dir / p;
}

/**
 * @brief 解析 ROI 数组文本。
 * @param value 形如 "[x, y, w, h]" 的字符串。
 * @return 解析出的整数数组；格式错误时返回空数组。
 */
std::vector<int> parseRectValues(const std::string& value) {
    // 把 [x, y, w, h] 解析成 4 个整数。
    const auto left = value.find('[');
    const auto right = value.find(']');
    if (left == std::string::npos || right == std::string::npos || right <= left) {
        return {};
    }

    std::vector<int> values;
    std::stringstream ss(value.substr(left + 1, right - left - 1));
    std::string item;
    while (std::getline(ss, item, ',')) {
        values.push_back(std::stoi(trim(item)));
    }
    return values;
}

/**
 * @brief 加载类别名称和别名映射。
 * @param path classes.yaml 路径。
 * @param detector 输入输出参数，会写入 names 和 aliases。
 */
void loadClasses(const std::filesystem::path& path, DetectorConfig& detector) {
    // 读取类别名和别名映射。
    // 这里只写了一个很轻量的解析器，避免教学框架一开始就强依赖 yaml-cpp。
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Class file not found, using defaults: " << path.string() << "\n";
        detector.names = {
            {0, "soybean"}, {1, "mung_bean"}, {2, "white_kidney_bean"},
            {3, "digit_1"}, {4, "digit_2"}, {5, "digit_3"}, {6, "digit_4"}, {7, "digit_5"}
        };
        return;
    }

    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        line = stripComment(line);
        if (line.empty()) {
            continue;
        }

        if (line == "names:") {
            section = "names";
            continue;
        }
        if (line == "aliases:") {
            section = "aliases";
            continue;
        }

        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, colon));
        const std::string value = unquote(line.substr(colon + 1));
        if (section == "names") {
            // names:
            //   0: soybean
            detector.names[std::stoi(key)] = value;
        } else if (section == "aliases") {
            // aliases:
            //   data_1: digit_1
            detector.aliases[key] = value;
        }
    }
}

/**
 * @brief 加载固定位置 ROI 配置。
 * @param path roi.yaml 路径。
 * @param roi 输出参数，会写入 pickup_rois 和 place_rois。
 * @throws std::runtime_error 当 ROI 配置文件不存在时抛出。
 */
void loadRois(const std::filesystem::path& path, RoiConfig& roi) {
    // 读取固定位置 ROI。
    // pickup_rois 对应 P1/P2/P3，place_rois 对应 L4-L8。
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("ROI file not found: " + path.string());
    }

    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        line = stripComment(line);
        if (line.empty()) {
            continue;
        }

        if (line == "pickup_rois:") {
            section = "pickup";
            continue;
        }
        if (line == "place_rois:") {
            section = "place";
            continue;
        }

        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, colon));
        const auto values = parseRectValues(line.substr(colon + 1));
        if (values.size() != 4) {
            continue;
        }

        cv::Rect rect(values[0], values[1], values[2], values[3]);
        if (section == "pickup") {
            // 取货区 ROI。
            roi.pickup_rois[key] = rect;
        } else if (section == "place") {
            // 放置区 ROI。
            roi.place_rois[key] = rect;
        }
    }
}

/**
 * @brief 加载串口配置。
 * @param path serial.yaml 路径。
 * @param serial 输出参数，会写入 enable、mock、port、baudrate。
 */
void loadSerial(const std::filesystem::path& path, SerialConfig& serial) {
    // 读取串口配置。当前 mock 模式只需要 enable/mock 两个字段就能跑通。
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Serial file not found, using defaults: " << path.string() << "\n";
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        line = stripComment(line);
        if (line.empty()) {
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));
        if (key == "enable") {
            serial.enable = parseBool(value);
        } else if (key == "mock") {
            serial.mock = parseBool(value);
        } else if (key == "port") {
            serial.port = unquote(value);
        } else if (key == "baudrate") {
            serial.baudrate = std::stoi(value);
        } else if (key == "ack_timeout_ms") {
            serial.ack_timeout_ms = std::stoi(value);
        } else if (key == "max_resend") {
            serial.max_resend = std::stoi(value);
        }
    }
}

}  // namespace

/**
 * @brief 从配置文件加载完整应用配置。
 * @param path app.yaml 的路径。
 * @return 加载完成的 AppConfig。
 * @throws std::runtime_error 当 app.yaml 或 roi.yaml 无法读取时抛出。
 */
AppConfig AppConfig::load(const std::string& path) {
    AppConfig config;

    //确定配置文件所在目录，用于解析相对路径。
    const std::filesystem::path config_path(path);
    const std::filesystem::path base_dir = config_path.has_parent_path()
        ? config_path.parent_path()
        : std::filesystem::current_path();
    config.base_dir = base_dir.string();

    //打开 app.yaml
    std::ifstream in(config_path);
    if (!in) {
        throw std::runtime_error("App config not found: " + path);
    }

    std::string section;
    std::string camera_subsection;
    std::string roi_file = "config/roi.yaml";
    std::string serial_file;

        // 解析 app.yaml 的顶层 section，例如 runtime/input/command/detector/roi/serial/debug/preview。
    std::string line;
    while (std::getline(in, line)) {
        const std::string raw_line = stripCommentKeepIndent(line);
        const std::string trimmed_line = trim(raw_line);
        if (trimmed_line.empty()) {
            continue;
        }

        const int indent = leadingIndent(raw_line);
        if (indent == 0 && trimmed_line.back() == ':') {
            // 没有缩进且以冒号结尾，认为它是一个新 section。
            section = trim(trimmed_line.substr(0, trimmed_line.size() - 1));
            camera_subsection.clear();
            continue;
        }

        if (section == "camera" && indent > 0 && trimmed_line.back() == ':') {
            camera_subsection = trim(trimmed_line.substr(0, trimmed_line.size() - 1));
            continue;
        }

        const auto colon = trimmed_line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        const std::string key = trim(trimmed_line.substr(0, colon));
        const std::string value = trim(trimmed_line.substr(colon + 1));

        if (section == "runtime") {
            if (key == "mode") {
                config.runtime.mode = unquote(value);
            }
        } else if (section == "input") {
            if (key == "type") {
                config.input.type = unquote(value);
            } else if (key == "source" || key == "path") {
                config.input.source = unquote(value);
            } else if (key == "bean_path") {
                config.input.bean_path = unquote(value);
            } else if (key == "digit_path") {
                config.input.digit_path = unquote(value);
            } else if (key == "camera_id") {
                config.input.camera_id = std::stoi(value);
                config.camera.camera_id = config.input.camera_id;
            }
        } else if (section == "camera") {
            if (key == "camera_id") {
                config.camera.camera_id = std::stoi(value);
                config.input.camera_id = config.camera.camera_id;
            } else if (key == "width") {
                config.camera.width = std::stoi(value);
            } else if (key == "height") {
                config.camera.height = std::stoi(value);
            } else if (key == "fps") {
                config.camera.fps = std::stoi(value);
            } else if (key == "exposure") {
                config.camera.exposure_time = std::stod(value);
            } else if (key == "gain") {
                config.camera.gain = std::stod(value);
            } else if (key == "auto_exposure") {
                config.camera.auto_exposure = parseBool(value);
            } else if (key == "auto_gain") {
                config.camera.auto_gain = parseBool(value);
            } else if (key == "auto_white_balance") {
                config.camera.auto_white_balance = parseBool(value);
            } else if (key == "flip_horizontal") {
                config.camera.flip_horizontal = parseBool(value);
            } else if (key == "flip_vertical") {
                config.camera.flip_vertical = parseBool(value);
            } else if (key == "rotate") {
                config.camera.rotate = std::stoi(value);
            } else if (camera_subsection == "exposure") {
                if (key == "auto") {
                    config.camera.auto_exposure = parseBool(value);
                } else if (key == "time") {
                    config.camera.exposure_time = std::stod(value);
                }
            } else if (camera_subsection == "gain") {
                if (key == "auto") {
                    config.camera.auto_gain = parseBool(value);
                } else if (key == "value") {
                    config.camera.gain = std::stod(value);
                }
            } else if (camera_subsection == "white_balance") {
                if (key == "auto") {
                    config.camera.auto_white_balance = parseBool(value);
                }
            } else if (camera_subsection == "image") {
                if (key == "flip_horizontal") {
                    config.camera.flip_horizontal = parseBool(value);
                } else if (key == "flip_vertical") {
                    config.camera.flip_vertical = parseBool(value);
                } else if (key == "rotate") {
                    config.camera.rotate = std::stoi(value);
                }
            }
        } else if (section == "command") {
            if (key == "source") {
                config.command.source = unquote(value);
            }
        } else if (section == "scan") {
            if (key == "frames_per_scan") {
                config.scan.frames_per_scan = std::stoi(value);
            } else if (key == "min_vote_count") {
                config.scan.min_vote_count = std::stoi(value);
            } else if (key == "min_avg_confidence") {
                config.scan.min_avg_confidence = std::stof(value);
            } else if (key == "max_retry") {
                config.scan.max_retry = std::stoi(value);
            } else if (key == "stable_delay_ms") {
                config.scan.stable_delay_ms = std::stoi(value);
            }
        } else if (section == "detector") {
            if (key == "backend") {
                config.detector.backend = unquote(value);
            } else if (key == "model_path") {
                config.detector.model_path = unquote(value);
            } else if (key == "conf_threshold") {
                config.detector.conf_threshold = std::stof(value);
            } else if (key == "nms_threshold") {
                config.detector.nms_threshold = std::stof(value);
            } else if (key == "class_file") {
                config.detector.class_file = unquote(value);
            }
        } else if (section == "roi" && key == "file") {
            roi_file = unquote(value);
        } else if (section == "serial") {
            if (key == "file") {
                serial_file = unquote(value);
            } else if (key == "enable") {
                config.serial.enable = parseBool(value);
            } else if (key == "mock") {
                config.serial.mock = parseBool(value);
                config.serial.enable = !config.serial.mock;
            } else if (key == "port") {
                config.serial.port = unquote(value);
            } else if (key == "baudrate") {
                config.serial.baudrate = std::stoi(value);
            } else if (key == "ack_timeout_ms") {
                config.serial.ack_timeout_ms = std::stoi(value);
            } else if (key == "max_resend") {
                config.serial.max_resend = std::stoi(value);
            }
        } else if (section == "debug") {
            if (key == "show_window") {
                config.debug.show_window = parseBool(value);
            } else if (key == "draw_result") {
                config.debug.draw_result = parseBool(value);
            } else if (key == "print_detections") {
                config.debug.print_detections = parseBool(value);
            } else if (key == "print_roi_result") {
                config.debug.print_roi_result = parseBool(value);
            } else if (key == "print_vote_result") {
                config.debug.print_vote_result = parseBool(value);
            } else if (key == "print_state") {
                config.debug.print_state = parseBool(value);
            } else if (key == "print_packet_hex") {
                config.debug.print_packet_hex = parseBool(value);
            } else if (key == "print_rx_hex") {
                config.debug.print_rx_hex = parseBool(value);
            } else if (key == "print_tx_hex") {
                config.debug.print_tx_hex = parseBool(value);
            } else if (key == "print_parsed_packet") {
                config.debug.print_parsed_packet = parseBool(value);
            } else if (key == "save_raw_frame") {
                config.debug.save_raw_frame = parseBool(value);
            } else if (key == "save_result_image") {
                config.debug.save_result_image = parseBool(value);
            } else if (key == "show_mouse_position") {
                config.debug.show_mouse_position = parseBool(value);
            } else if (key == "output_dir") {
                config.debug.output_dir = unquote(value);
            }
        } else if (section == "preview") {
            if (key == "draw_roi") {
                config.preview.draw_roi = parseBool(value);
            } else if (key == "yolo_enable") {
                config.preview.yolo_enable = parseBool(value);
            } else if (key == "draw_boxes") {
                config.preview.draw_boxes = parseBool(value);
            } else if (key == "print_detections") {
                config.preview.print_detections = parseBool(value);
            }
        }
    }

    // app.yaml 只告诉我们子配置文件在哪里，这里继续加载子配置。
    loadClasses(resolvePath(base_dir, config.detector.class_file), config.detector);
    if (!config.input.source.empty()) {
        config.input.source = resolvePath(base_dir, config.input.source).string();
    }
    if (!config.input.bean_path.empty()) {
        config.input.bean_path = resolvePath(base_dir, config.input.bean_path).string();
    }
    if (!config.input.digit_path.empty()) {
        config.input.digit_path = resolvePath(base_dir, config.input.digit_path).string();
    }
    if (!config.detector.model_path.empty()) {
        config.detector.model_path = resolvePath(base_dir, config.detector.model_path).string();
    }
    loadRois(resolvePath(base_dir, roi_file), config.roi);
    if (!serial_file.empty()) {
        loadSerial(resolvePath(base_dir, serial_file), config.serial);
    }
    config.serial.print_packet_hex = config.debug.print_packet_hex;
    config.serial.print_rx_hex = config.debug.print_rx_hex;
    config.serial.print_tx_hex = config.debug.print_tx_hex || config.debug.print_packet_hex;
    config.serial.print_parsed_packet = config.debug.print_parsed_packet;
    return config;
}
