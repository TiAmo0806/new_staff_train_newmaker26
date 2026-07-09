#ifndef BEAN_SORTING_SERIAL_H_
  #define BEAN_SORTING_SERIAL_H_

  #include <string>
  #include <cstdint>
  #include <vector>
  #include "BeanProtocol.h"

  class BeanSerial {
  public:
      explicit BeanSerial(const std::string& portName = "/dev/ttyACM0");
      ~BeanSerial();

      BeanSerial(const BeanSerial&) = delete;
      BeanSerial& operator=(const BeanSerial&) = delete;

      bool Open();
      void Close();
      bool IsOpen() const { return fd_ >= 0; }
      void SetSimulated(bool v) { simulated_ = v; }
      bool IsSimulated() const { return simulated_; }
      void SetTxLogEnabled(bool v) { txLogEnabled_ = v; }
      bool IsTxLogEnabled() const { return txLogEnabled_; }
      void SetAutoReconnect(bool v) { autoReconnect_ = v; }
      bool TryReconnect();
      const std::string& GetPortName() const { return portName_; }

      bool sendVision(const bean_sorting::VisionData& v, int maxRetries = 3);
      bool sendControl(const bean_sorting::ControlData& c, int maxRetries = 3);
      std::vector<uint8_t> recvRaw(int timeout_ms = 50);

  private:
      bool ConfigurePort();
      std::string FindAvailablePort();
      bool sendFrame(const uint8_t* frame, size_t len, int maxRetries,
                     const char* label);

      int fd_ = -1;
      std::string portName_;
      bool simulated_ = false;
      bool txLogEnabled_ = false;
      bool autoReconnect_ = true;
  };

  #endif