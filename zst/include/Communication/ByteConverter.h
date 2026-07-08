#ifndef BYTE_CONVERTER_H
#define BYTE_CONVERTER_H

#include <array>
#include <cstdint>

class ByteConverter
{
public:
    static std::array<uint8_t, 4> floatToBytesLittle(float value);
};

#endif // BYTE_CONVERTER_H

