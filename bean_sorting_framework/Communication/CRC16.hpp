#ifndef BEAN_SORTING_CRC16_HPP_
  #define BEAN_SORTING_CRC16_HPP_

  #include <cstdint>

  namespace crc16 {

  uint16_t Get_CRC16_Check_Sum(const uint8_t* pchMessage, uint32_t dwLength,
                               uint16_t wCRC = 0xFFFF);

  uint32_t Verify_CRC16_Check_Sum(const uint8_t* pchMessage, uint32_t dwLength);

  void Append_CRC16_Check_Sum(uint8_t* pchMessage, uint32_t dwLength);

  }  // namespace crc16

  #endif