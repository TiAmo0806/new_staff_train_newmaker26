#ifndef SERIAL_PORT_HPP
#define SERIAL_PORT_HPP

#include "packet.hpp"
#include <string>
#include <vector>
#include <cstdint>

class SerialPort {
public:
    SerialPort() = default;
    SerialPort(const std::string& port, int baud_rate = 115200);
    ~SerialPort();

    bool open(const std::string& port, int baud_rate = 115200);
    void close();
    bool isOpen() const { return fd >= 0; }
    
    // 发送
    bool sendPacket(const rm_serial_driver::VisionToMCUPacket& packet);
    bool send(const std::string& data);
    
    // ⭐ 接收电控反馈
    bool receivePacket(rm_serial_driver::MCUToVisionPacket& packet, size_t timeout_ms = 100);
    std::string receive(size_t timeout_ms = 100);

private:
    int fd = -1;
    uint16_t calcCRC16(const uint8_t* data, size_t len);
};

#endif