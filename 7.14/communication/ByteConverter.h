/**
 * @file ByteConverter.h
 * @brief 字节转换工具类，用于浮点数与字节数组的转换
 * @author lxy
 * @date 2025-10-24
 */

#ifndef BYTE_CONVERTER_H
#define BYTE_CONVERTER_H

#include <vector>
#include <cstdint>
#include <string>

/**
 * @brief 字节转换工具类
 * 提供浮点数与字节数组之间的转换功能
 */
class ByteConverter {
public:
    /**
     * @brief 将浮点数转换为小端序字节数组
     * @param value 浮点数值
     * @return 4字节的字节数组
     */
    static std::vector<uint8_t> floatToBytesLittle(float value);

private:
    // 禁用构造函数
    ByteConverter() = delete;
};

#endif // BYTE_CONVERTER_H
