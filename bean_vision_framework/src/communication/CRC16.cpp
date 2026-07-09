#include "communication/CRC16.h"

/**
 * @brief 计算原始内存数据的 CRC16-MODBUS 校验值。
 * @param data 数据起始地址。
 * @param length 数据长度，单位字节。
 * @return CRC16 校验值。
 */
uint16_t CRC16::calculate(const uint8_t* data, size_t length) {
    // CRC16-MODBUS 常见初始值是 0xFFFF，多项式是 0xA001。
    // 接收端只要使用同样算法，就能检查数据包是否损坏。
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001U) != 0U) {
                crc = static_cast<uint16_t>((crc >> 1U) ^ 0xA001U);
            } else {
                crc = static_cast<uint16_t>(crc >> 1U);
            }
        }
    }
    return crc;
}

/**
 * @brief 计算字节数组的 CRC16-MODBUS 校验值。
 * @param data 待计算的字节数组。
 * @return CRC16 校验值。
 */
uint16_t CRC16::calculate(const std::vector<uint8_t>& data) {
    return calculate(data.data(), data.size());
}
