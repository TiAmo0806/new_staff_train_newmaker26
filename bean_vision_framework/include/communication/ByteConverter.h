#pragma once

#include <cstdint>
#include <string>
#include <vector>

class ByteConverter {
public:
    /**
     * @brief 将字节数组转换成十六进制字符串。
     * @param data 待转换的字节数组。
     * @return 形如 "A5 02 0B" 的字符串。
     */
    static std::string toHex(const std::vector<uint8_t>& data);

    /**
     * @brief 以小端序向数组末尾追加 uint16_t。
     * @param data 要追加数据的字节数组。
     * @param value 要追加的 16 位整数。
     */
    static void appendUint16LE(std::vector<uint8_t>& data, uint16_t value);
};
