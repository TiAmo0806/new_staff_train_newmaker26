#include "/home/zst/zst/include/Communication/CRC16.hpp"
#include <cstdint>

namespace crc16
{
uint16_t Calc(const uint8_t *data, int length)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

void Append(uint8_t *data, int length)
{
    if (length < 2) return;
    uint16_t crc = Calc(data, length - 2);
    data[length - 2] = static_cast<uint8_t>(crc & 0xFF);
    data[length - 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
}

bool Verify(const uint8_t *data, int length)
{
    if (length < 2) return false;
    uint16_t expected = static_cast<uint16_t>(data[length - 2]) |
                        (static_cast<uint16_t>(data[length - 1]) << 8);
    return expected == Calc(data, length - 2);
}
}

