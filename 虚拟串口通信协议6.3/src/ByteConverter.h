/**
 * @file ByteConverter.h
 * @brief 字节转换工具 (from dart project)
 */

#ifndef BEAN_SORTER__BYTE_CONVERTER_H_
#define BEAN_SORTER__BYTE_CONVERTER_H_

#include <vector>
#include <cstdint>

namespace bean_sorter
{
namespace byte_converter
{

std::vector<uint8_t> floatToBytesLittle(float value);
float bytesLittleToFloat(const uint8_t * bytes);
std::vector<uint8_t> uint32ToBytesLittle(uint32_t value);
uint32_t bytesLittleToUint32(const uint8_t * bytes);

}  // namespace byte_converter
}  // namespace bean_sorter

#endif  // BEAN_SORTER__BYTE_CONVERTER_H_
