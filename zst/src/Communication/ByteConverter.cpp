#include "Communication/ByteConverter.h"
#include <cstring>

std::array<uint8_t, 4> ByteConverter::floatToBytesLittle(float value)
{
    std::array<uint8_t, 4> out{};               // 4 字节输出数组，初始化为 0
    // memcpy 避免通过指针强转读取 float 所引起的严格别名问题。
    static_assert(sizeof(float) == 4, "float must be 4 bytes");  // 编译期确保 float 是 4 字节
    std::memcpy(out.data(), &value, 4);         // 按小端字节序拷贝 float 的内存表示
    return out;
}
