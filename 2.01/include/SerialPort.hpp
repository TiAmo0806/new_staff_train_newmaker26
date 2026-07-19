/**
 * @file SerialPort.hpp
 * @brief 串口通信封装层
 *
 * 封装 Linux 串口（USB CDC ACM）的打开、关闭、收发操作。
 * 与电控（STM32F407）通过 USB 虚拟串口相连，提供 CRC-16/CCITT 校验。
 *
 * 典型用法:
 * @code
 *   SerialPort serial("/dev/ttyACM0", 115200);
 *   serial.sendPacket(vision_packet);
 *   serial.receivePacket(mcu_feedback, 50);
 * @endcode
 */

#ifndef SERIAL_PORT_HPP
#define SERIAL_PORT_HPP

#include "packet.hpp"
#include <string>
#include <vector>
#include <cstdint>

/**
 * @brief 串口通信类
 *
 * 负责：
 *   1. 以 0xA6 包头发送 12 字节的视觉→电控数据包（自动计算 CRC）
 *   2. 以 0x5A 包头接收 4 字节的电控→视觉反馈包（自动验证 CRC）
 *   3. 字节同步：接收时不断搜索包头，丢弃无效字节
 */
class SerialPort {
public:
    SerialPort() = default;
    SerialPort(const std::string& port, int baud_rate = 115200);
    ~SerialPort();

    bool open(const std::string& port, int baud_rate = 115200);
    void close();
    bool isOpen() const { return fd >= 0; }

    // 发送 12 字节视觉→电控包
    bool sendPacket(const rm_serial_driver::VisionToMCUPacket& packet);
    // ⭐ 发送原始字符串数据（不常用，保留兼容）
    bool send(const std::string& data);

    // ⭐ 接收电控反馈：阻塞接收，自动搜索 0x5A 包头 + CRC 校验
    bool receivePacket(rm_serial_driver::MCUToVisionPacket& packet, size_t timeout_ms = 100);
    // 接收原始字符串数据（不常用，保留兼容）
    std::string receive(size_t timeout_ms = 100);

private:
    int fd = -1;                                              // 串口文件描述符
    uint16_t calcCRC16(const uint8_t* data, size_t len);      // CRC-16/CCITT 计算
};

#endif