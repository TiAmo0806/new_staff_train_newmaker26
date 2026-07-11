#include "/home/zst/zst/include/Communication/ByteConverter.h"
#include <cstring>

std::array<uint8_t, 4> ByteConverter::floatToBytesLittle(float value)
{
    std::array<uint8_t, 4> out{};
    // memcpy 避免通过指针强转读取 float 所引起的严格别名问题。
    static_assert(sizeof(float) == 4, "float must be 4 bytes");
    std::memcpy(out.data(), &value, 4);
    return out;
}
