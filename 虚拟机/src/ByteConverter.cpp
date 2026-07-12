/**
 * @file ByteConverter.cpp
 * @brief 字节转换工具类实现
 * @author lxy
 * @date 2025-10-24
 */

#include "ByteConverter.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>

std::vector<uint8_t> ByteConverter::floatToBytesLittle(float value) {
    std::vector<uint8_t> bytes(4);
    uint8_t* floatBytes = reinterpret_cast<uint8_t*>(&value);
    
    // 小端序：低字节在前
    bytes[0] = floatBytes[0];
    bytes[1] = floatBytes[1];
    bytes[2] = floatBytes[2];
    bytes[3] = floatBytes[3];
    
    return bytes;
}