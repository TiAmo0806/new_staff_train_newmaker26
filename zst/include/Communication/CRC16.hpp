#ifndef CRC16_HPP
#define CRC16_HPP

#include <cstdint>

namespace crc16
{
// Modbus CRC16：初值 0xFFFF，多项式 0xA001。
// Calc 计算指定字节范围；Append 写入缓冲区末尾低/高字节；Verify 校验收到的完整帧。
uint16_t Calc(const uint8_t *data, int length);
void Append(uint8_t *data, int length);
bool Verify(const uint8_t *data, int length);
}

#endif // CRC16_HPP
