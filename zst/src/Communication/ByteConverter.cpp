#include "/home/zst/zst/include/Communication/ByteConverter.h"
#include <cstring>

std::array<uint8_t, 4> ByteConverter::floatToBytesLittle(float value)
{
    std::array<uint8_t, 4> out{};
    static_assert(sizeof(float) == 4, "float must be 4 bytes");
    std::memcpy(out.data(), &value, 4);
    return out;
}

