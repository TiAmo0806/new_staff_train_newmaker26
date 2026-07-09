#include "communication/ByteConverter.h"

#include <iomanip>
#include <sstream>

/**
 * @brief 将字节数组转换成十六进制字符串。
 * @param data 待转换的字节数组。
 * @return 形如 "A5 02 0B" 的字符串。
 */
std::string ByteConverter::toHex(const std::vector<uint8_t>& data) {
    // 把字节数组转成 "A5 02 0B ..." 这种格式，便于查看串口包。
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0');
    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0) {
            oss << ' ';
        }
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

/**
 * @brief 以小端序向数组末尾追加 uint16_t。
 * @param data 要追加数据的字节数组。
 * @param value 要追加的 16 位整数。
 */
void ByteConverter::appendUint16LE(std::vector<uint8_t>& data, uint16_t value) {
    // 小端序追加 16 位整数：低字节在前，高字节在后。
    data.push_back(static_cast<uint8_t>(value & 0xFFU));
    data.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
}
