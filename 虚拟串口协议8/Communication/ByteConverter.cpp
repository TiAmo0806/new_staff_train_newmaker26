/**
 * @file ByteConverter.cpp
 * @brief 字节转换工具实现 (from dart project),数据类型与字节数组之间的互相转换
 */

#include "ByteConverter.h"
#include <cstring>

namespace bean_sorter
{
namespace byte_converter
{

std::vector<uint8_t> floatToBytesLittle(float value)
{
  std::vector<uint8_t> bytes(4);
  uint8_t * floatBytes = reinterpret_cast<uint8_t *>(&value);  // 把float的地址当成uint8_t*来读取
  bytes[0] = floatBytes[0]; // 逐字节复制（小端序）
  bytes[1] = floatBytes[1];
  bytes[2] = floatBytes[2];
  bytes[3] = floatBytes[3];
  return bytes;
}

float bytesLittleToFloat(const uint8_t * bytes)//字节数组→float
{
  float value;
  uint8_t * p = reinterpret_cast<uint8_t *>(&value);
  p[0] = bytes[0];
  p[1] = bytes[1];
  p[2] = bytes[2];
  p[3] = bytes[3];
  return value;
}

std::vector<uint8_t> uint32ToBytesLittle(uint32_t value)//uint32_t→字节数组
{
  std::vector<uint8_t> bytes(4);
  bytes[0] = value & 0xFF;
  bytes[1] = (value >> 8) & 0xFF;
  bytes[2] = (value >> 16) & 0xFF;
  bytes[3] = (value >> 24) & 0xFF;
  return bytes;
}

uint32_t bytesLittleToUint32(const uint8_t * bytes)//字节数组→uint32_t
{
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

}  // namespace byte_converter
}  // namespace bean_sorter
