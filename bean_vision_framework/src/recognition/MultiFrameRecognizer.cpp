#if 0
#include "recognition/MultiFrameRecognizer.h"

#include "utils/DebugLogger.h"
#include "utils/LogUtils.h"

#include <chrono>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

struct VoteBucket {
    int count = 0;                      // 同一 ROI 内同一类别在多帧中出现的次数。
    float confidence_sum = 0.0f;        // 该类别多次出现的置信度总和，用于计算平均置信度。
    PositionResult best;                // 该类别中置信度最高的一次结果，用于保留框和中心点。
};

// 第一层 key 是固定位置，例如 P1/L4；第二层 key 是类别名，例如 soybean/digit_1。
using PositionVotes = std::map<std::string, std::map<std::string, VoteBucket>>;

/**
 * @brief 将单帧 ROI 结果累计到投票表。
 * @param votes 多帧投票表。
 * @param position 单个固定位置的识别结果。
 */
void addVote(PositionVotes& votes, const PositionResult& position) {
    if (!position.valid) {
        return;
    }

    VoteBucket& bucket = votes[position.position_id][position.class_name];
    bucket.count += 1;
    bucket.confidence_sum += position.confidence;
    // 最终输出时需要一个具体检测框，因此保留该类别里置信度最高的一帧。
    if (!bucket.best.valid || position.confidence > bucket.best.confidence) {
        bucket.best = position;
    }
}

/**
 * @brief 从某个固定位置的多类别投票中选择稳定结果。
 * @param votes 多帧投票表。
 * @param position_id 固定位置编号。
 * @param min_vote_count 判定成功所需的最少出现次数。
 * @param min_avg_confidence 判定成功所需的平均置信度。
 * @param frames_per_scan 本轮扫描总帧数。
 * @param print_vote_result 是否打印投票日志。
 * @return 满足投票条件时返回 valid=true 的 PositionResult，否则返回 valid=false。
 */
PositionResult chooseVote(const PositionVotes& votes,
                          const std::string& position_id,
                          int min_vote_count,
                          float min_avg_confidence,
                          int frames_per_scan,
                          bool print_vote_result) {
    PositionResult result;
    result.position_id = position_id;

    const auto position_it = votes.find(position_id);
    if (position_it == votes.end()) {
        if (print_vote_result) {
            std::cout << "[ROI-VOTE] " << position_id << " missing count=0/" << frames_per_scan << " fail\n";
        }
        return result;
    }

    const VoteBucket* best_bucket = nullptr;
    const std::string* best_class = nullptr;
    for (const auto& [class_name, bucket] : position_it->second) {
        // 优先选择出现次数最多的类别；次数相同时选择置信度总和更高的类别。
        if (best_bucket == nullptr || bucket.count > best_bucket->count ||
            (bucket.count == best_bucket->count && bucket.confidence_sum > best_bucket->confidence_sum)) {
            best_bucket = &bucket;
            best_class = &class_name;
        }
    }

    if (best_bucket == nullptr || best_class == nullptr) {
        return result;
    }

    const float avg_confidence = best_bucket->confidence_sum / static_cast<float>(best_bucket->count);
    const bool success = best_bucket->count >= min_vote_count && avg_confidence >= min_avg_confidence;
    if (print_vote_result) {
        std::cout << "[ROI-VOTE] " << position_id << " " << *best_class
                  << " count=" << best_bucket->count << "/" << frames_per_scan
                  << " avg_conf=" << avg_confidence
                  << (success ? " success" : " fail") << "\n";
    }

    if (!success) {
        return result;
    }

    result = best_bucket->best;
    result.confidence = avg_confidence;
    return result;
}

}  // namespace

MultiFrameRecognizer::MultiFrameRecognizer(const AppConfig& config) : config_(config) {}

VisionResult MultiFrameRecognizer::scanBeans(InputManager& input, BeanNumberDetector& detector, RoiParser& parser) {
    return scan(input, detector, parser, true);
}

VisionResult MultiFrameRecognizer::scanDigits(InputManager& input, BeanNumberDetector& detector, RoiParser& parser) {
    return scan(input, detector, parser, false);
}

VisionResult MultiFrameRecognizer::scan(InputManager& input,
                                         BeanNumberDetector& detector,
                                         RoiParser& parser,
                                         bool scan_beans) {
    const std::string event_name = scan_beans ? "camera_bean" : "camera_digit";

    if (config_.scan.stable_delay_ms > 0) {
        // 收到到位命令后先等一小段时间，减少车体刚停下时画面抖动的影响。
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.scan.stable_delay_ms));
    }

    // 一次扫描失败不会立刻结束，而是按 max_retry 重新抓一组帧再投票。
    for (int retry = 1; retry <= config_.scan.max_retry; ++retry) {
        std::cout << "[SCAN] capture " << config_.scan.frames_per_scan
                  << " frames retry=" << retry << "/" << config_.scan.max_retry << "\n";

        PositionVotes votes;
        for (int frame_index = 0; frame_index < config_.scan.frames_per_scan; ++frame_index) {
            cv::Mat frame;
            if (!input.read(frame) || frame.empty()) {
                std::cout << "[WARN] failed to read camera frame\n";
                continue;
            }

            std::vector<Detection> detections = detector.detect(frame);
            VisionResult frame_result = parser.parse(detections);

            // 豆子阶段只统计 P1/P2/P3，数字阶段只统计 L4-L8。
            if (scan_beans) {
                addVote(votes, frame_result.p1);
                addVote(votes, frame_result.p2);
                addVote(votes, frame_result.p3);
            } else {
                addVote(votes, frame_result.l4);
                addVote(votes, frame_result.l5);
                addVote(votes, frame_result.l6);
                addVote(votes, frame_result.l7);
                addVote(votes, frame_result.l8);
            }

            if (config_.debug.print_detections) {
                LogUtils::printDetections(detections);
            }
            if (config_.debug.print_roi_result) {
                LogUtils::printVisionResult(frame_result);
            }

            if (config_.debug.show_window) {
                cv::Mat visual = frame.clone();
                DrawUtils::drawRois(visual, config_.roi);
                DrawUtils::drawDetections(visual, detections);
                DrawUtils::drawVisionResult(visual, frame_result);
                cv::imshow("bean_vision_camera_debug", visual);
                if (cv::waitKey(1) == 27) {
                    std::cout << "[SCAN] window stopped by ESC\n";
                    VisionResult stopped;
                    stopped.success = false;
                    stopped.reason = "window_stopped";
                    return stopped;
                }
            }

            // 每帧都可以保存调试图，方便观察投票失败到底是 YOLO 漏检还是 ROI 没覆盖。
            DebugLogger::saveCommandImages(event_name, "frame", frame, detections, frame_result, config_, false);
        }

        VisionResult merged;
        if (scan_beans) {
            // 豆子区要求 P1/P2/P3 三个取货点都稳定识别成功，才发送 BEAN_BIND。
            merged.p1 = chooseVote(votes, "P1", config_.scan.min_vote_count,
                                   config_.scan.min_avg_confidence,
                                   config_.scan.frames_per_scan,
                                   config_.debug.print_vote_result);
            merged.p2 = chooseVote(votes, "P2", config_.scan.min_vote_count,
                                   config_.scan.min_avg_confidence,
                                   config_.scan.frames_per_scan,
                                   config_.debug.print_vote_result);
            merged.p3 = chooseVote(votes, "P3", config_.scan.min_vote_count,
                                   config_.scan.min_avg_confidence,
                                   config_.scan.frames_per_scan,
                                   config_.debug.print_vote_result);
            merged.success = merged.p1.valid && merged.p2.valid && merged.p3.valid;
            merged.reason = merged.success ? "ok" : "bean_scan_failed";
        } else {
            // 数字区不强制 L4-L8 全部出现，只要识别到有效数字位置即可尝试生成最终任务。
            merged.l4 = chooseVote(votes, "L4", config_.scan.min_vote_count,
                                   config_.scan.min_avg_confidence,
                                   config_.scan.frames_per_scan,
                                   config_.debug.print_vote_result);
            merged.l5 = chooseVote(votes, "L5", config_.scan.min_vote_count,
                                   config_.scan.min_avg_confidence,
                                   config_.scan.frames_per_scan,
                                   config_.debug.print_vote_result);
            merged.l6 = chooseVote(votes, "L6", config_.scan.min_vote_count,
                                   config_.scan.min_avg_confidence,
                                   config_.scan.frames_per_scan,
                                   config_.debug.print_vote_result);
            merged.l7 = chooseVote(votes, "L7", config_.scan.min_vote_count,
                                   config_.scan.min_avg_confidence,
                                   config_.scan.frames_per_scan,
                                   config_.debug.print_vote_result);
            merged.l8 = chooseVote(votes, "L8", config_.scan.min_vote_count,
                                   config_.scan.min_avg_confidence,
                                   config_.scan.frames_per_scan,
                                   config_.debug.print_vote_result);
            merged.success = merged.l4.valid || merged.l5.valid || merged.l6.valid || merged.l7.valid || merged.l8.valid;
            merged.reason = merged.success ? "ok" : "digit_scan_failed";
        }

        if (merged.success) {
            return merged;
        }
    }

    std::cout << "[WARN] " << (scan_beans ? "bean" : "digit") << " scan failed after retries\n";
    VisionResult failed;
    failed.success = false;
    failed.reason = scan_beans ? "bean_scan_failed_after_retries" : "digit_scan_failed_after_retries";
    return failed;
}
#endif

#include "recognition/MultiFrameRecognizer.h"

#include "utils/DebugLogger.h"
#include "utils/LogUtils.h"

#include <chrono>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

struct VoteBucketFixed {
    int count = 0;                // 同一 ROI 内同一类别在多帧中出现的次数。
    float confidence_sum = 0.0f;  // 多次出现的置信度总和，用于计算平均置信度。
    PositionResult best;          // 该类别中置信度最高的一次结果，用于保留框和中心点。
};

// 第一层 key 是固定位置，例如 P1/L4；第二层 key 是类别名，例如 soybean/digit_1。
using PositionVotesFixed = std::map<std::string, std::map<std::string, VoteBucketFixed>>;

/**
 * @brief 将单帧 ROI 结果累计到多帧投票表。
 */
void addVoteFixed(PositionVotesFixed& votes, const PositionResult& position) {
    if (!position.valid) {
        return;
    }

    VoteBucketFixed& bucket = votes[position.position_id][position.class_name];
    bucket.count += 1;
    bucket.confidence_sum += position.confidence;

    if (!bucket.best.valid || position.confidence > bucket.best.confidence) {
        bucket.best = position;
    }
}

/**
 * @brief 从某个固定位置的多类别投票中选择稳定结果。
 */
PositionResult chooseVoteFixed(const PositionVotesFixed& votes,
                               const std::string& position_id,
                               int min_vote_count,
                               float min_avg_confidence,
                               int frames_per_scan,
                               bool print_vote_result) {
    PositionResult result;
    result.position_id = position_id;

    const auto position_it = votes.find(position_id);
    if (position_it == votes.end()) {
        if (print_vote_result) {
            std::cout << "[ROI-VOTE] " << position_id << " missing count=0/" << frames_per_scan << " fail\n";
        }
        return result;
    }

    const VoteBucketFixed* best_bucket = nullptr;
    const std::string* best_class = nullptr;
    for (const auto& [class_name, bucket] : position_it->second) {
        const bool better_count = best_bucket == nullptr || bucket.count > best_bucket->count;
        const bool better_confidence =
            best_bucket != nullptr &&
            bucket.count == best_bucket->count &&
            bucket.confidence_sum > best_bucket->confidence_sum;
        if (better_count || better_confidence) {
            best_bucket = &bucket;
            best_class = &class_name;
        }
    }

    if (best_bucket == nullptr || best_class == nullptr) {
        return result;
    }

    const float avg_confidence = best_bucket->confidence_sum / static_cast<float>(best_bucket->count);
    const bool success = best_bucket->count >= min_vote_count && avg_confidence >= min_avg_confidence;
    if (print_vote_result) {
        std::cout << "[ROI-VOTE] " << position_id << " " << *best_class
                  << " count=" << best_bucket->count << "/" << frames_per_scan
                  << " avg_conf=" << avg_confidence
                  << (success ? " success" : " fail") << "\n";
    }

    if (!success) {
        return result;
    }

    result = best_bucket->best;
    result.confidence = avg_confidence;
    return result;
}

}  // namespace

MultiFrameRecognizer::MultiFrameRecognizer(const AppConfig& config) : config_(config) {}

VisionResult MultiFrameRecognizer::scanBeans(InputManager& input, BeanNumberDetector& detector, RoiParser& parser) {
    return scan(input, detector, parser, true);
}

VisionResult MultiFrameRecognizer::scanDigits(InputManager& input, BeanNumberDetector& detector, RoiParser& parser) {
    return scan(input, detector, parser, false);
}

VisionResult MultiFrameRecognizer::scan(InputManager& input,
                                         BeanNumberDetector& detector,
                                         RoiParser& parser,
                                         bool scan_beans) {
    const std::string event_name = scan_beans ? "camera_bean" : "camera_digit";

    if (config_.scan.stable_delay_ms > 0) {
        // 到位后先等待一小段时间，减少车体刚停下时画面抖动的影响。
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.scan.stable_delay_ms));
    }

    for (int retry = 1; retry <= config_.scan.max_retry; ++retry) {
        std::cout << "[SCAN] capture " << config_.scan.frames_per_scan
                  << " frames retry=" << retry << "/" << config_.scan.max_retry << "\n";

        PositionVotesFixed votes;
        for (int frame_index = 0; frame_index < config_.scan.frames_per_scan; ++frame_index) {
            cv::Mat frame;
            if (!input.read(frame) || frame.empty()) {
                std::cout << "[WARN] failed to read camera frame\n";
                continue;
            }

            std::vector<Detection> detections = detector.detect(frame);
            VisionResult frame_result = parser.parse(detections);

            // 豆子阶段只统计 P1/P2/P3，数字阶段只统计 L4-L8。
            if (scan_beans) {
                addVoteFixed(votes, frame_result.p1);
                addVoteFixed(votes, frame_result.p2);
                addVoteFixed(votes, frame_result.p3);
            } else {
                addVoteFixed(votes, frame_result.l4);
                addVoteFixed(votes, frame_result.l5);
                addVoteFixed(votes, frame_result.l6);
                addVoteFixed(votes, frame_result.l7);
                addVoteFixed(votes, frame_result.l8);
            }

            if (config_.debug.print_detections) {
                LogUtils::printDetections(detections);
            }
            if (config_.debug.print_roi_result) {
                LogUtils::printVisionResult(frame_result);
            }

            // 保存图片和弹出可视化窗口统一交给 DebugLogger，避免调试绘制逻辑散落。
            DebugLogger::saveCommandImages(event_name, "frame", frame, detections, frame_result, config_, false);
        }

        VisionResult merged;
        if (scan_beans) {
            // 豆子区要求 P1/P2/P3 三个取货点都稳定识别成功，才发送 BEAN_BIND。
            merged.p1 = chooseVoteFixed(votes, "P1", config_.scan.min_vote_count,
                                        config_.scan.min_avg_confidence,
                                        config_.scan.frames_per_scan,
                                        config_.debug.print_vote_result);
            merged.p2 = chooseVoteFixed(votes, "P2", config_.scan.min_vote_count,
                                        config_.scan.min_avg_confidence,
                                        config_.scan.frames_per_scan,
                                        config_.debug.print_vote_result);
            merged.p3 = chooseVoteFixed(votes, "P3", config_.scan.min_vote_count,
                                        config_.scan.min_avg_confidence,
                                        config_.scan.frames_per_scan,
                                        config_.debug.print_vote_result);
            merged.success = merged.p1.valid && merged.p2.valid && merged.p3.valid;
            merged.reason = merged.success ? "ok" : "bean_scan_failed";
        } else {
            // 数字区不强制 L4-L8 全部出现，只要识别到有效数字位置即可生成最终任务。
            merged.l4 = chooseVoteFixed(votes, "L4", config_.scan.min_vote_count,
                                        config_.scan.min_avg_confidence,
                                        config_.scan.frames_per_scan,
                                        config_.debug.print_vote_result);
            merged.l5 = chooseVoteFixed(votes, "L5", config_.scan.min_vote_count,
                                        config_.scan.min_avg_confidence,
                                        config_.scan.frames_per_scan,
                                        config_.debug.print_vote_result);
            merged.l6 = chooseVoteFixed(votes, "L6", config_.scan.min_vote_count,
                                        config_.scan.min_avg_confidence,
                                        config_.scan.frames_per_scan,
                                        config_.debug.print_vote_result);
            merged.l7 = chooseVoteFixed(votes, "L7", config_.scan.min_vote_count,
                                        config_.scan.min_avg_confidence,
                                        config_.scan.frames_per_scan,
                                        config_.debug.print_vote_result);
            merged.l8 = chooseVoteFixed(votes, "L8", config_.scan.min_vote_count,
                                        config_.scan.min_avg_confidence,
                                        config_.scan.frames_per_scan,
                                        config_.debug.print_vote_result);
            merged.success = merged.l4.valid || merged.l5.valid || merged.l6.valid || merged.l7.valid || merged.l8.valid;
            merged.reason = merged.success ? "ok" : "digit_scan_failed";
        }

        if (merged.success) {
            return merged;
        }
    }

    std::cout << "[WARN] " << (scan_beans ? "bean" : "digit") << " scan failed after retries\n";
    VisionResult failed;
    failed.success = false;
    failed.reason = scan_beans ? "bean_scan_failed_after_retries" : "digit_scan_failed_after_retries";
    return failed;
}
