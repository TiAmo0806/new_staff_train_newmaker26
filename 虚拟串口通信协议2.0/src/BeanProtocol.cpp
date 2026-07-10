/**
 * @file BeanProtocol.cpp
 * @brief 抓豆分拣协议序列化实现，把结构体数据打包成字节流发送，把字节流解析成结构体接收
 */

#include "BeanProtocol.hpp"

namespace bean_sorter
{

std::vector<uint8_t> encodeDetection(const DetectionPacket & pkt)
{//帧是通信中一个完整数据包，包含帧头，数据和CRC校验
  std::vector<uint8_t> frame(DETECTION_PACKET_SIZE, 0);//创建一个18字节的帧，全部初始化为0,frame现在是一个有18个元素的vector，每个元素都是0
  frame[0] = DETECTION_HEADER;//填入帧头（第0字节），告诉接受方"这是一个检测包"
  frame[1] = pkt.bean_type;//填入豆种类
  frame[2] = pkt.target_bin;//填入目标箱子
  frame[3] = pkt.confidence;//填入置信度

  auto px = byte_converter::floatToBytesLittle(pkt.position_x);//把float转成字节数组，按小端序（低位在前），串口只能传输字节，不能直接传输float，能传输0～255的整数
  auto py = byte_converter::floatToBytesLittle(pkt.position_y);
  auto sz = byte_converter::floatToBytesLittle(pkt.size);
  //拷贝到帧的对应位置
  std::memcpy(&frame[4], px.data(), 4);
  std::memcpy(&frame[8], py.data(), 4);
  std::memcpy(&frame[12], sz.data(), 4);

  crc16::Append_CRC16_Check_Sum(frame.data(), DETECTION_PACKET_SIZE);//计算校验和，追加到帧末尾
     // CRC占第16-17字节（最后2字节）
    // 计算前16字节的CRC值，填入最后2字节
   //  返回完整的18字节数据包
  return frame;
}

bool decodeDetection(const uint8_t * frame, DetectionPacket & pkt)
{
  if (!crc16::Verify_CRC16_Check_Sum(frame, DETECTION_PACKET_SIZE))//验证CRC
    return false;
  //提取数据
  pkt.bean_type  = frame[1];
  pkt.target_bin = frame[2];
  pkt.confidence = frame[3];
  pkt.position_x = byte_converter::bytesLittleToFloat(&frame[4]);
  pkt.position_y = byte_converter::bytesLittleToFloat(&frame[8]);
  pkt.size       = byte_converter::bytesLittleToFloat(&frame[12]);
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