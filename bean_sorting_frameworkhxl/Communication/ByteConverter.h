#ifndef BEAN_SORTING_BYTE_CONVERTER_H_
  #define BEAN_SORTING_BYTE_CONVERTER_H_

  #include <cstdint>
  #include <cstddef>
  #include <vector>

  class ByteConverter {
  public:
      static std::vector<uint8_t> floatToBytesLittle(float value);
      static float bytesToFloatLittle(const uint8_t* bytes);
      static std::vector<uint8_t> uint16ToBytesLittle(uint16_t value);
      static std::vector<uint8_t> uint32ToBytesLittle(uint32_t value);
      static uint16_t bytesToUint16Little(const uint8_t* bytes);
      static uint32_t bytesToUint32Little(const uint8_t* bytes);
      static void writeBytes(uint8_t* dst, size_t offset,
                             const std::vector<uint8_t>& src);

  private:
      ByteConverter() = delete;
  };

  #endif