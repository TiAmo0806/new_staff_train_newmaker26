#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class CRC16 {
public:
    /**
     * @brief 计算原始内存数据的 CRC16-MODBUS 校验值。
     * @param data 数据起始地址。
     * @param length 数据长度，单位字节。
     * @return CRC16 校验值。
     */
    static uint16_t calculate(const uint8_t* data, size_t length);

    /**
     * @brief 计算字节数组的 CRC16-MODBUS 校验值。
     * @param data 待计算的字节数组。
     * @return CRC16 校验值。
     */
    static uint16_t calculate(const std::vector<uint8_t>& data);
};
