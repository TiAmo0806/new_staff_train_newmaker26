/**
 * @file BeanProtocol.cpp
 * @brief 抓豆分拣协议序列化实现，把结构体数据打包成字节流发送，把字节流解析成结构体接收
 */

#include "BeanProtocol.hpp"

namespace bean_sorter
{

std::vector<uint8_t> encodeDetection(const DetectionPacket & pkt)
{//把3个位置的豆子类型和目标箱号打包成9字节帧
  std::vector<uint8_t> frame(DETECTION_PACKET_SIZE, 0);
  frame[0] = DETECTION_HEADER;
  frame[1] = pkt.bean_types[0];
  frame[2] = pkt.target_bins[0];
  frame[3] = pkt.bean_types[1];
  frame[4] = pkt.target_bins[1];
  frame[5] = pkt.bean_types[2];
  frame[6] = pkt.target_bins[2];

  crc16::Append_CRC16_Check_Sum(frame.data(), DETECTION_PACKET_SIZE);
  return frame;
}

bool decodeDetection(const uint8_t * frame, DetectionPacket & pkt)
{
  if (!crc16::Verify_CRC16_Check_Sum(frame, DETECTION_PACKET_SIZE))
    return false;

  pkt.bean_types[0]  = frame[1];
  pkt.target_bins[0] = frame[2];
  pkt.bean_types[1]  = frame[3];
  pkt.target_bins[1] = frame[4];
  pkt.bean_types[2]  = frame[5];
  pkt.target_bins[2] = frame[6];
  return true;
}

std::vector<uint8_t> encodeNumber(const NumberPacket & pkt)
{
  std::vector<uint8_t> frame(NUMBER_PACKET_SIZE, 0);
  frame[0] = NUMBER_HEADER;
  for (int i = 0; i < 5; ++i)
    frame[1 + i] = pkt.digits[i];

  crc16::Append_CRC16_Check_Sum(frame.data(), NUMBER_PACKET_SIZE);
  return frame;
}

bool decodeNumber(const uint8_t * frame, NumberPacket & pkt)
{
  if (!crc16::Verify_CRC16_Check_Sum(frame, NUMBER_PACKET_SIZE))
    return false;

  for (int i = 0; i < 5; ++i)
    pkt.digits[i] = frame[1 + i];
  return true;
}

std::vector<uint8_t> encodeStatus(const StatusPacket & pkt)
{
  std::vector<uint8_t> frame(STATUS_PACKET_SIZE, 0);
  frame[0] = STATUS_HEADER;//帧头
  frame[1] = pkt.system_state;//系统状态
  frame[2] = pkt.flags;//标志位
  frame[3] = pkt.current_bin;//当前箱子
  frame[4] = pkt.error_code;//错误码

  auto ts = byte_converter::uint32ToBytesLittle(pkt.timestamp_ms);
  std::memcpy(&frame[5], ts.data(), 4);

  crc16::Append_CRC16_Check_Sum(frame.data(), STATUS_PACKET_SIZE);
  return frame;
}

bool decodeStatus(const uint8_t * frame, StatusPacket & pkt)//把接受到的状态帧解析成结构体
{
  if (!crc16::Verify_CRC16_Check_Sum(frame, STATUS_PACKET_SIZE))
    return false;

  pkt.system_state = frame[1];
  pkt.flags        = frame[2];
  pkt.current_bin  = frame[3];
  pkt.error_code   = frame[4];
  pkt.timestamp_ms = byte_converter::bytesLittleToUint32(&frame[5]);//提取时间戳
  return true;
}

// ==================== 帧同步扫描 ====================

bool scanFrame(const std::vector<uint8_t> & buffer,//在缓冲区查找一帧完整的数据包
               uint8_t expected_header, uint32_t expected_size,
               std::vector<uint8_t> & out_frame)
{
  if (buffer.size() < expected_size) return false;//检查缓冲区是否足够大

  for (size_t i = 0; i <= buffer.size() - expected_size; ++i) {
    if (buffer[i] == expected_header) {//遍历缓冲区，逐个字节查找帧头
      if (crc16::Verify_CRC16_Check_Sum(&buffer[i], expected_size)) {
        out_frame.assign(buffer.begin() + i, buffer.begin() + i + expected_size);//提取这一帧
        return true;
      }
    }
  }
  return false;
}

}  // namespace bean_sorter