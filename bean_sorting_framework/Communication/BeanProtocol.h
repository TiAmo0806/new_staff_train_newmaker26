#ifndef BEAN_SORTING_PROTOCOL_H_
  #define BEAN_SORTING_PROTOCOL_H_

  #include <cstdint>
  #include <cstring>
  #include <vector>
  #include "CRC16.hpp"
  #include "ByteConverter.h"

  namespace bean_sorting {

  constexpr uint8_t  HEADER_VISION   = 0xBB;
  constexpr uint8_t  HEADER_CONTROL  = 0xCC;
  constexpr uint8_t  HEADER_BATCH    = 0xBD;
  constexpr size_t   VISION_FRAME    = 4;
  constexpr size_t   CONTROL_FRAME   = 10;
  constexpr size_t   BATCH_FRAME_BASE = 4;   // header(1)+count(1)+CRC16(2), +N entries

  enum class BeanType : uint8_t {
      SOYBEAN     = 0,
      MUNG_BEAN   = 1,
      KIDNEY_BEAN = 2,
      DATA_1      = 3,
      DATA_2      = 4,
      DATA_3      = 5,
      DATA_4      = 6,
      DATA_5      = 7,
      UNKNOWN     = 8,
      ERROR       = 9,
  };

  inline const char* bean_type_name(BeanType t) {
      switch (t) {
          case BeanType::SOYBEAN:     return "黄豆";
          case BeanType::MUNG_BEAN:   return "绿豆";
          case BeanType::KIDNEY_BEAN: return "白芸豆";
          case BeanType::DATA_1:      return "1";
          case BeanType::DATA_2:      return "2";
          case BeanType::DATA_3:      return "3";
          case BeanType::DATA_4:      return "4";
          case BeanType::DATA_5:      return "5";
          case BeanType::ERROR:       return "错误";
          default:                    return "未知";
      }
  }

  enum class CtrlState : uint8_t {
      IDLE = 0, GRASPING = 1, MOVING = 2, PLACING = 3, DONE = 4, FAULT = 5
  };

  enum class ErrCode : uint8_t {
      OK = 0, MISS = 1, DROPPED = 2, COLLISION = 3, TIMEOUT = 4
  };

  // ---- 批量帧条目 ----
  struct BatchEntry {
      BeanType bean_type  = BeanType::UNKNOWN;
      uint8_t  target_box = 0;
      bool     detected   = true;
  };

  // ---- 单帧 (豆子阶段用) ----
  struct VisionData {
      BeanType bean_type  = BeanType::UNKNOWN;
      uint8_t  target_box = 0;
      bool     detected   = false;
  };

  struct ControlData {
      CtrlState state        = CtrlState::IDLE;
      ErrCode   err          = ErrCode::OK;
      bool      need_next    = false;
      uint32_t  ack_frame_id = 0;
      uint8_t   holding      = 0xFF;
  };

  // Vision 单帧编解码 (不变)
  inline std::vector<uint8_t> encode_vision(const VisionData& v) {
      std::vector<uint8_t> f(VISION_FRAME, 0);
      f[0] = HEADER_VISION;
      uint8_t fl = 0;
      fl |= (static_cast<uint8_t>(v.bean_type) & 0x0F);
      fl |= ((v.target_box & 0x07) << 4);
      if (v.detected) fl |= (1 << 7);
      f[1] = fl;
      crc16::Append_CRC16_Check_Sum(f.data(), VISION_FRAME);
      return f;
  }

  inline VisionData decode_vision(const uint8_t* buf, size_t len) {
      VisionData v;
      if (len < VISION_FRAME || buf[0] != HEADER_VISION) return v;
      if (!crc16::Verify_CRC16_Check_Sum(buf, VISION_FRAME)) return v;
      uint8_t fl = buf[1];
      v.bean_type  = static_cast<BeanType>(fl & 0x0F);
      v.target_box = (fl >> 4) & 0x07;
      v.detected   = (fl >> 7) & 0x01;
      return v;
  }

  // Control 编解码 (不变)
  inline std::vector<uint8_t> encode_control(const ControlData& c) {
      std::vector<uint8_t> f(CONTROL_FRAME, 0);
      f[0] = HEADER_CONTROL;
      uint8_t fl = 0;
      fl |= (static_cast<uint8_t>(c.state) & 0x07);
      fl |= ((static_cast<uint8_t>(c.err) & 0x07) << 3);
      if (c.need_next) fl |= (1 << 6);
      f[1] = fl;
      ByteConverter::writeBytes(f.data(), 2, ByteConverter::uint32ToBytesLittle(c.ack_frame_id));
      f[6] = c.holding;
      crc16::Append_CRC16_Check_Sum(f.data(), CONTROL_FRAME);
      return f;
  }

  inline ControlData decode_control(const uint8_t* buf, size_t len) {
      ControlData c;
      if (len < CONTROL_FRAME || buf[0] != HEADER_CONTROL) return c;
      if (!crc16::Verify_CRC16_Check_Sum(buf, CONTROL_FRAME)) return c;
      uint8_t fl = buf[1];
      c.state        = static_cast<CtrlState>(fl & 0x07);
      c.err          = static_cast<ErrCode>((fl >> 3) & 0x07);
      c.need_next    = (fl >> 6) & 0x01;
      c.ack_frame_id = ByteConverter::bytesToUint32Little(buf + 2);
      c.holding      = buf[6];
      return c;
  }

  // ================================================================
  // 批量帧编解码 (数字打包阶段用)
  // 格式: [0]=0xBD [1]=count [2..1+N]=N个条目 [末2]=CRC16
  // 每个条目1字节: bit[7]=detected, bit[6:4]=target_box, bit[3:0]=bean_type
  // ================================================================
  inline std::vector<uint8_t> encode_batch(const std::vector<BatchEntry>& entries) {
      size_t n = entries.size();
      if (n > 8) n = 8;
      std::vector<uint8_t> f(BATCH_FRAME_BASE + n, 0);
      f[0] = HEADER_BATCH;
      f[1] = (uint8_t)n;
      for (size_t i = 0; i < n; ++i) {
          uint8_t b = 0;
          b |= (static_cast<uint8_t>(entries[i].bean_type) & 0x0F);
          b |= ((entries[i].target_box & 0x07) << 4);
          if (entries[i].detected) b |= (1 << 7);
          f[2 + i] = b;
      }
      crc16::Append_CRC16_Check_Sum(f.data(), f.size());
      return f;
  }

  inline std::vector<BatchEntry> decode_batch(const uint8_t* buf, size_t len) {
      std::vector<BatchEntry> out;
      if (len < BATCH_FRAME_BASE || buf[0] != HEADER_BATCH) return out;
      if (!crc16::Verify_CRC16_Check_Sum(buf, len)) return out;
      uint8_t n = buf[1];
      if (len < BATCH_FRAME_BASE + n) return out;
      for (uint8_t i = 0; i < n; ++i) {
          BatchEntry e;
          uint8_t b = buf[2 + i];
          e.bean_type  = static_cast<BeanType>(b & 0x0F);
          e.target_box = (b >> 4) & 0x07;
          e.detected   = (b >> 7) & 0x01;
          out.push_back(e);
      }
      return out;
  }

  }  // namespace bean_sorting

  #endif
