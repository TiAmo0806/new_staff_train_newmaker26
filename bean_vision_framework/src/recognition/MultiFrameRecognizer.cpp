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
    int count = 0;                // 同一 ROI 内同一类别在多帧中出现的次数。
    float confidence_sum = 0.0f;  // 多次出现的置信度总和，用于计算平均置信度。
    PositionResult best;          // 该类别中置信度最高的一次结果，用于保留框和中心点。
};

// 第一层 key 是固定位置，例如 P1/L4；第二层 key 是类别名，例如 soybean/digit_1。
using PositionVotes = std::map<std::string, std::map<std::string, VoteBucket>>;

/**
 * @brief 将单帧 ROI 结果累计到多帧投票表。
 */
void addVote(PositionVotes& votes, const PositionResult& position) {
    if (!position.valid) {
        return;
    }

    VoteBucket& bucket = votes[position.position_id][position.class_name];
    bucket.count += 1;
    bucket.confidence_sum += position.confidence;

    if (!bucket.best.valid || position.confidence > bucket.best.confidence) {
        bucket.best = position;
    }
}

/**
 * @brief 从某个固定位置的多类别投票中选择稳定结果。
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

int bestVoteCount(const PositionVotes& votes, const std::string& position_id) {
    const auto position_it = votes.find(position_id);
    if (position_it == votes.end()) {
        return 0;
    }

    int best_count = 0;
    for (const auto& [class_name, bucket] : position_it->second) {
        (void)class_name;
        if (bucket.count > best_count) {
            best_count = bucket.count;
        }
    }
    return best_count;
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

    VisionResult best_partial_result;
    int best_partial_valid_count = 0;
    for (int retry = 1; retry <= config_.scan.max_retry; ++retry) {
        std::cout << "[SCAN] capture " << config_.scan.frames_per_scan
                  << " frames retry=" << retry << "/" << config_.scan.max_retry << "\n";

        PositionVotes votes;
        int frames_read_ok = 0;
        int frames_read_failed = 0;
        int detection_frames = 0;
        int roi_hit_frames = 0;
        for (int frame_index = 0; frame_index < config_.scan.frames_per_scan; ++frame_index) {
            cv::Mat frame;
            if (!input.read(frame) || frame.empty()) {
                std::cout << "[WARN] failed to read camera frame\n";
                frames_read_failed += 1;
                continue;
            }
            frames_read_ok += 1;

            std::vector<Detection> detections = detector.detect(frame);
            if (!detections.empty()) {
                detection_frames += 1;
            }
            VisionResult frame_result = parser.parse(detections);

            // 豆子阶段只统计 P1/P2/P3，数字阶段只统计 L4-L8。
            if (scan_beans) {
                const bool has_roi_hit =
                    frame_result.p1.valid || frame_result.p2.valid || frame_result.p3.valid;
                if (has_roi_hit) {
                    roi_hit_frames += 1;
                }
                addVote(votes, frame_result.p1);
                addVote(votes, frame_result.p2);
                addVote(votes, frame_result.p3);
            } else {
                const bool has_roi_hit =
                    frame_result.l4.valid || frame_result.l5.valid || frame_result.l6.valid ||
                    frame_result.l7.valid || frame_result.l8.valid;
                if (has_roi_hit) {
                    roi_hit_frames += 1;
                }
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

            // 保存图片和弹出可视化窗口统一交给 DebugLogger，避免调试绘制逻辑散落。
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

            const int valid_count = static_cast<int>(merged.p1.valid) +
                                    static_cast<int>(merged.p2.valid) +
                                    static_cast<int>(merged.p3.valid);
            if (!merged.success && valid_count > best_partial_valid_count) {
                // 只保留单次 retry 内投票融合出的结果，不跨 retry 拼接 P 位置。
                best_partial_result = merged;
                best_partial_valid_count = valid_count;
            }
        } else {
            // 数字区不强制 L4-L8 全部出现，只要识别到有效数字位置即可生成最终任务。
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

        if (config_.debug.print_vote_result) {
            std::cout << "[SCAN SUMMARY] stage=" << (scan_beans ? "bean" : "digit")
                      << " retry=" << retry << "/" << config_.scan.max_retry
                      << " frames_requested=" << config_.scan.frames_per_scan
                      << " frames_read_ok=" << frames_read_ok
                      << " frames_read_failed=" << frames_read_failed
                      << " detection_frames=" << detection_frames
                      << " roi_hit_frames=" << roi_hit_frames;
            if (scan_beans) {
                std::cout << " P1_votes=" << bestVoteCount(votes, "P1")
                          << " P2_votes=" << bestVoteCount(votes, "P2")
                          << " P3_votes=" << bestVoteCount(votes, "P3");
            } else {
                std::cout << " L4_votes=" << bestVoteCount(votes, "L4")
                          << " L5_votes=" << bestVoteCount(votes, "L5")
                          << " L6_votes=" << bestVoteCount(votes, "L6")
                          << " L7_votes=" << bestVoteCount(votes, "L7")
                          << " L8_votes=" << bestVoteCount(votes, "L8");
            }
            std::cout << " result=" << (merged.success ? "success" : "fail") << "\n";
        }

        if (merged.success) {
            return merged;
        }
    }

    std::cout << "[WARN] " << (scan_beans ? "bean" : "digit") << " scan failed after retries\n";
    if (scan_beans && best_partial_valid_count > 0) {
        best_partial_result.success = false;
        best_partial_result.reason = "bean_scan_partial_after_retries";
        return best_partial_result;
    }
    VisionResult failed;
    failed.success = false;
    failed.reason = scan_beans ? "bean_scan_failed_after_retries" : "digit_scan_failed_after_retries";
    return failed;
}
