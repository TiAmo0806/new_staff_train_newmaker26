// Communication/ByteConverter.cpp
  // 将所有数据转为小端序，确保视觉算法和下位机能够正确理解对方的数据

  #include "ByteConverter.h"
  #include <cstring>

  std::vector<uint8_t> ByteConverter::floatToBytesLittle(float value) {//float转换为bytes
      std::vector<uint8_t> bytes(4);
      uint8_t* p = reinterpret_cast<uint8_t*>(&value);
      bytes[0] = p[0];
      bytes[1] = p[1];
      bytes[2] = p[2];
      bytes[3] = p[3];
      return bytes;
  }

  float ByteConverter::bytesToFloatLittle(const uint8_t* bytes) {
      float value = 0.0f;
      uint8_t* p = reinterpret_cast<uint8_t*>(&value);
      p[0] = bytes[0];
      p[1] = bytes[1];
      p[2] = bytes[2];
      p[3] = bytes[3];
      return value;
  }

  std::vector<uint8_t> ByteConverter::uint16ToBytesLittle(uint16_t value) {
      std::vector<uint8_t> bytes(2);
      bytes[0] = static_cast<uint8_t>(value & 0xFF);
      bytes[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
      return bytes;
  }

  std::vector<uint8_t> ByteConverter::uint32ToBytesLittle(uint32_t value) {
      std::vector<uint8_t> bytes(4);
      bytes[0] = static_cast<uint8_t>(value & 0xFF);
      bytes[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
      bytes[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
      bytes[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
      return bytes;
  }

  uint16_t ByteConverter::bytesToUint16Little(const uint8_t* bytes) {
      return static_cast<uint16_t>(bytes[0]) |
             (static_cast<uint16_t>(bytes[1]) << 8);
  }

  uint32_t ByteConverter::bytesToUint32Little(const uint8_t* bytes) {
      return static_cast<uint32_t>(bytes[0]) |
             (static_cast<uint32_t>(bytes[1]) << 8) |
             (static_cast<uint32_t>(bytes[2]) << 16) |
             (static_cast<uint32_t>(bytes[3]) << 24);
  }

  void ByteConverter::writeBytes(uint8_t* dst, size_t offset,
                                 const std::vector<uint8_t>& src) {
      std::memcpy(dst + offset, src.data(), src.size());
  }