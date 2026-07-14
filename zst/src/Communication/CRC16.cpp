#include "Communication/CRC16.hpp"
#include <cstdint>

namespace crc16
{
uint16_t Calc(const uint8_t *data, int length)
{
    // 与电控referee.c、26new飞镖完全一致的裁判系统CRC16。
    // 其查表实现来自反向多项式0x8408；这里使用等价的逐位算法，结果逐字节相同。
    uint16_t crc = 0xFFFF;                      // 裁判系统CRC16固定初值
    for (int i = 0; i < length; ++i)
    {
        crc ^= data[i];                         // 当前字节异或进 CRC 低字节
        for (int j = 0; j < 8; ++j)             // 逐位处理
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);  // 最低位为1时异或0x8408
    }
    return crc;
}

void Append(uint8_t *data, int length)
{
    // length 是"含两个 CRC 占位字节"的总帧长，因此实际参与计算的是 length - 2。
    if (length < 2) return;                     // 帧太短，无法追加
    uint16_t crc = Calc(data, length - 2);      // 计算前 length-2 字节的 CRC
    data[length - 2] = static_cast<uint8_t>(crc & 0xFF);          // CRC 低字节（小端）
    data[length - 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);   // CRC 高字节（小端）
}

bool Verify(const uint8_t *data, int length)
{
    // 协议按小端发送 CRC：先低 8 位，再高 8 位。
    if (length < 2) return false;               // 帧太短，无法验证
    uint16_t expected = static_cast<uint16_t>(data[length - 2]) |          // CRC 低字节
                        (static_cast<uint16_t>(data[length - 1]) << 8);    // CRC 高字节（小端重组）
    return expected == Calc(data, length - 2);  // 与重新计算的 CRC 比较
}
}
