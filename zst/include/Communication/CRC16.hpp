#ifndef CRC16_HPP
#define CRC16_HPP

#include <cstdint>

namespace crc16
{
uint16_t Calc(const uint8_t *data, int length);
void Append(uint8_t *data, int length);
bool Verify(const uint8_t *data, int length);
}

#endif // CRC16_HPP

