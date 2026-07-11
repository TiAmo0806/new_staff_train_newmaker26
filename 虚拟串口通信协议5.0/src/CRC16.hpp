/**
 * @file CRC16.hpp
 * @brief CRC16-CCITT 查表法校验 (from dart project)
 */

#ifndef BEAN_SORTER__CRC16_HPP_
#define BEAN_SORTER__CRC16_HPP_

#include <cstdint>

namespace bean_sorter
{
namespace crc16
{

uint16_t Get_CRC16_Check_Sum(const uint8_t * pchMessage, uint32_t dwLength, uint16_t wCRC = 0xFFFF);
uint32_t Verify_CRC16_Check_Sum(const uint8_t * pchMessage, uint32_t dwLength);
void Append_CRC16_Check_Sum(uint8_t * pchMessage, uint32_t dwLength);

}  // namespace crc16
}  // namespace bean_sorter

#endif  // BEAN_SORTER__CRC16_HPP_
