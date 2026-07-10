/**
 * @file ControlNode.cpp
 * @brief 控制节点 - 模拟机械臂电控，接收视觉检测结果并执行抓取放置
 *
 * 通信流程:
 *   1. 发送 StatusPacket (0xBB) 表示就绪
 *   2. 接收 DetectionPacket (0xAA) 获取豆子信息
 *   3. 模拟抓取+放置动作序列
 *   4. 回到步骤1
 *
 * Usage: ./ControlNode /dev/pts/3 [--simulate] [--txlog] [--rxlog]
 */

#include "BeanProtocol.hpp"
#include "SerialPort.h"
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <chrono>
#include <string>
#include <cstring>

// ==================== Configuration ====================
struct Config
{
  std::string serialPort = "/dev/ttyACM0";
  bool simulate = false;
  bool txLog = true;
  bool rxLog = false;
  int moveTimeMs  = 800;   // 移动时间
  int gripTimeMs  = 500;   // 夹取时间
  int placeTimeMs = 400;   // 放置时间
};

static Config loadConfig(int argc, char * argv[])
{
  Config cfg;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--simulate")      cfg.simulate = true;
    else if (arg == "--txlog")    cfg.txLog = true;
    else if (arg == "--rxlog")    cfg.rxLog = true;
    else if (arg[0] != '-')       cfg.serialPort = arg;
  }
  return cfg;
}

// ==================== Gripper Simulation (替换为真实电控代码) ====================

/// 发送状态更新并执行动作延迟
static bool sendStatus(bean_sorter::SerialPort & serial,
                        uint8_t state, uint8_t flags,
                        uint8_t bin, uint8_t error)
{
  bean_sorter::StatusPacket status;
  status.system_state = state;
  status.flags        = flags;
  status.current_bin  = bin;
  status.error_code   = error;
  status.timestamp_ms = static_cast<uint32_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());

  auto frame = bean_sorter::encodeStatus(status);
  return serial.SendFrame(frame.data(), frame.size());
}

static bool simulateMove(bean_sorter::SerialPort & serial,
                          const char * action, int duration_ms,
                          uint8_t bin)
{
  std::cout << "[控制] " << action << "..." << std::endl;
  if (!sendStatus(serial, bean_sorter::STATE_MOVING, 0, bin,
                  bean_sorter::ERR_NONE)) {
    return false;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
  std::cout << "[控制] " << action << " 完成" << std::endl;
  return true;
}

static bool simulateGrip(bean_sorter::SerialPort & serial, bool close)
{
  const char * action = close ? "夹爪闭合" : "夹爪张开";
  uint8_t flags = close ? 0 : bean_sorter::FLAG_GRIPPER_OPEN;
  std::cout << "[控制] " << action << "..." << std::endl;
  if (!sendStatus(serial, close ? bean_sorter::STATE_GRIPPING
                                : bean_sorter::STATE_PLACING,
                  flags, 0, bean_sorter::ERR_NONE)) {
    return false;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(close ? 500 : 400));
  std::cout << "[控制] " << action << " 完成" << std::endl;
  return true;
}

// ==================== Action Sequence ====================

static bool executePickAndPlace(bean_sorter::SerialPort & serial,
                                 const bean_sorter::DetectionPacket & det,
                                 const Config & cfg)
{
  for (int i = 0; i < 3; ++i)
  {
    uint8_t type = det.bean_types[i];
    uint8_t bin  = det.target_bins[i];
    if (type == bean_sorter::BEAN_NONE || bin < 1 || bin > 5)
    {
      std::cout << "[控制] 位置" << (i+1) << ": 空，跳过" << std::endl;
      continue;
    }

    std::cout << "[控制] 位置" << (i+1) << ": "
              << bean_sorter::beanTypeName(static_cast<bean_sorter::BeanType>(type))
              << " -> " << static_cast<int>(bin) << "号箱" << std::endl;

    // Step 1: Move to bean position
    if (!simulateMove(serial, "移动到抓取位置", cfg.moveTimeMs, 0))
      return false;

    // Step 2: Close gripper (grip bean)
    if (!simulateGrip(serial, true))
      return false;

    // Step 3: Move to target bin
    if (!simulateMove(serial, "移动到料箱", cfg.moveTimeMs, bin))
      return false;

    // Step 4: Open gripper (place bean)
    if (!simulateGrip(serial, false))
      return false;
  }

  // All positions done
  if (!sendStatus(serial, bean_sorter::STATE_COMPLETED,
                  bean_sorter::FLAG_GRIPPER_OPEN, 0,
                  bean_sorter::ERR_NONE)) {
    return false;
  }

  std::cout << "[控制] 所有抓放完成!" << std::endl;
  return true;
}

// ==================== Main ====================

int main(int argc, char * argv[])
{
  Config cfg = loadConfig(argc, argv);

  bean_sorter::SerialPort serial(cfg.serialPort);
  serial.SetSimulated(cfg.simulate);
  serial.SetTxLogEnabled(cfg.txLog);
  serial.SetRxLogEnabled(cfg.rxLog);
  serial.SetAutoReconnect(false);

  if (!cfg.simulate) {
    if (!serial.Open()) {
      std::cerr << "[控制] 打开失败 " << cfg.serialPort << std::endl;
      serial.SetSimulated(true);
      std::cout << "[控制] 降级到模拟模式" << std::endl;
    }
  }

  std::cout << "\n========== 抓豆分拣 控制节点 v2.0 ==========" << std::endl;
  std::cout << "  串口: " << serial.GetPortName()
            << " | 模式: " << (serial.IsSimulated() ? "模拟" : "真实串口") << std::endl;
  std::cout << "  TX日志: " << (cfg.txLog ? "开" : "关")
            << " | RX日志: " << (cfg.rxLog ? "开" : "关") << std::endl;
  std::cout << "====================================================\n" << std::endl;

  int total_pick = 0;
  int error_count = 0;

  while (true) {
    // 1. Signal ready to vision
    std::cout << "\n[控制] 系统空闲，发送就绪信号..." << std::endl;

    if (!sendStatus(serial, bean_sorter::STATE_IDLE,
                    bean_sorter::FLAG_GRIPPER_OPEN | bean_sorter::FLAG_READY,
                    0, bean_sorter::ERR_NONE)) {
      std::cerr << "[控制] 就绪信号发送失败" << std::endl;
      error_count++;
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }

    // 2. Wait for detection result
    auto frame = serial.ReadFrame(bean_sorter::DETECTION_HEADER,
                                   bean_sorter::DETECTION_PACKET_SIZE, 5000);
    if (frame.empty()) {
      std::cout << "[控制] 等待超时，重新发送就绪..." << std::endl;
      continue;
    }

    // 3. Decode detection
    bean_sorter::DetectionPacket det;
    if (!bean_sorter::decodeDetection(frame.data(), det)) {
      std::cerr << "[控制] 检测帧无效 (CRC错误)" << std::endl;
      error_count++;
      continue;
    }

    // 检查是否有有效豆种
    {
      bool hasAny = false;
      for (int i = 0; i < 3; ++i)
      {
        if (det.bean_types[i] != bean_sorter::BEAN_NONE &&
            det.bean_types[i] < bean_sorter::BEAN_UNKNOWN)
        {
          hasAny = true;
          break;
        }
      }
      if (!hasAny)
      {
        std::cout << "[控制] 包中无有效豆种，跳过" << std::endl;
        continue;
      }
    }

    std::cout << "[控制] << 3位置包: ";
    for (int i = 0; i < 3; ++i)
    {
      if (det.bean_types[i] != bean_sorter::BEAN_NONE)
        std::cout << "pos" << (i+1) << "="
                  << bean_sorter::beanTypeName(static_cast<bean_sorter::BeanType>(det.bean_types[i]))
                  << "->" << static_cast<int>(det.target_bins[i]) << "号箱 ";
      else
        std::cout << "pos" << (i+1) << "=空 ";
    }
    std::cout << std::endl;

    // 4. Execute pick-and-place
    if (!executePickAndPlace(serial, det, cfg)) {
      std::cerr << "[控制] 抓放失败!" << std::endl;
      error_count++;

      // Send error state
      sendStatus(serial, bean_sorter::STATE_ERROR,
                 bean_sorter::FLAG_GRIPPER_OPEN, 0,
                 bean_sorter::ERR_UNKNOWN);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      continue;
    }

    // 5. 数字标签扫描（模拟转到数字区）
    std::cout << "\n[控制] 转到数字标签区..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 请求数字标签检测
    if (!sendStatus(serial, bean_sorter::STATE_IDLE,
                    bean_sorter::FLAG_GRIPPER_OPEN | bean_sorter::FLAG_DIGIT_SCAN,
                    0, bean_sorter::ERR_NONE)) {
      std::cerr << "[控制] 数字扫描请求发送失败" << std::endl;
      continue;
    }

    // 接收数字标签包
    {
      auto num_frame = serial.ReadFrame(bean_sorter::NUMBER_HEADER,
                                         bean_sorter::NUMBER_PACKET_SIZE, 15000);
      if (!num_frame.empty()) {
        bean_sorter::NumberPacket num;
        if (bean_sorter::decodeNumber(num_frame.data(), num)) {
          std::cout << "[控制] << 数字标签: ";
          for (int i = 0; i < 5; ++i) {
            if (num.digits[i] != 0)
              std::cout << "pos" << (i+1) << "=" << static_cast<int>(num.digits[i]) << " ";
            else
              std::cout << "pos" << (i+1) << "=空 ";
          }
          std::cout << std::endl;
        }
      } else {
        std::cout << "[控制] 数字标签等待超时" << std::endl;
      }
    }

    total_pick++;
    std::cout << "[控制] 统计: 成功=" << total_pick
              << " 失败=" << error_count << std::endl;
  }

  serial.Close();
  return 0;
}
