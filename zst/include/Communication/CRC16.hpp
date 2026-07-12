#ifndef CRC16_HPP
#define CRC16_HPP

#include <cstdint>

namespace crc16
{
// Modbus CRC16：初值 0xFFFF，多项式 0xA001。
// Calc 计算指定字节范围；Append 写入缓冲区末尾低/高字节；Verify 校验收到的完整帧。

// 计算 data 前 length 字节的 CRC16 校验值
uint16_t Calc(const uint8_t *data, int length);
// 将 CRC16 校验值追加到缓冲区末尾（低字节在前，高字节在后）
void Append(uint8_t *data, int length);
// 验证收到的完整帧：重新计算 CRC 并与帧尾两字节比较
bool Verify(const uint8_t *data, int length);
}

#endif // CRC16_HPP
