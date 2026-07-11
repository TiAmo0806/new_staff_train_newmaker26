#ifndef BYTE_CONVERTER_H
#define BYTE_CONVERTER_H

#include <array>
#include <cstdint>

class ByteConverter
{
public:
    // 将 IEEE-754 单精度浮点数按本机小端内存顺序拆成 4 字节。
    // 当前场地状态协议尚未使用；以后发送偏差量时，电控端必须用相同字节序还原。
    static std::array<uint8_t, 4> floatToBytesLittle(float value);
};

#endif // BYTE_CONVERTER_H
