#include "communication/SerialPort.h"

#include "communication/ByteConverter.h"
#include "communication/Protocol.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

speed_t baudrateToFlag(int baudrate) {
    switch (baudrate) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    default:
        return B115200;
    }
}

Protocol& protocolInstance() {
    static Protocol protocol;
    return protocol;
}

}  // namespace

/**
 * @brief 构造串口模块。
 * @param config 串口配置。
 */
SerialPort::SerialPort(const SerialConfig& config) : config_(config) {}

/**
 * @brief 打开串口或进入 mock 模式。
 * @return 打开成功返回 true，失败返回 false。
 */
bool SerialPort::open() {
    if (!config_.enable || config_.mock) {
        // mock 模式不打开真实串口，避免没有硬件时程序跑不起来。
        std::cout << "Serial mock mode enabled. Port is not opened.\n";
        return true;
    }

    fd_ = ::open(config_.port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd_ < 0) {
        std::cerr << "Failed to open serial port " << config_.port
                  << ": " << std::strerror(errno) << "\n";
        return false;
    }

    termios tty {};
    if (tcgetattr(fd_, &tty) != 0) {
        std::cerr << "Failed to get serial attributes: " << std::strerror(errno) << "\n";
        close();
        return false;
    }

    const speed_t speed = baudrateToFlag(config_.baudrate);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        std::cerr << "Failed to set serial attributes: " << std::strerror(errno) << "\n";
        close();
        return false;
    }

    std::cout << "Serial opened: " << config_.port << " baudrate=" << config_.baudrate << "\n";
    return true;
}

/**
 * @brief 发送字节数据。
 * @param data 要发送的完整协议帧。
 * @return 发送成功返回 true，失败返回 false。
 */
bool SerialPort::write(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return false;
    }

    if (!config_.enable || config_.mock) {
        if (config_.print_tx_hex || config_.print_packet_hex) {
            std::cout << "[TX HEX] " << ByteConverter::toHex(data) << "\n";
        }
        if (config_.print_parsed_packet) {
            printParsedPacket("[TX PARSED]", data);
        }
        return true;
    }

    const int attempts = std::max(1, config_.max_resend + 1);
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        const ssize_t written = ::write(fd_, data.data(), data.size());
        if (written < 0 || static_cast<size_t>(written) != data.size()) {
            std::cerr << "Serial write failed: " << std::strerror(errno) << "\n";
            return false;
        }

        if (config_.print_tx_hex || config_.print_packet_hex) {
            std::cout << "[TX HEX] " << ByteConverter::toHex(data) << "\n";
        }
        if (config_.print_parsed_packet) {
            printParsedPacket("[TX PARSED]", data);
        }

        const bool skip_ack_wait =
            data[1] == static_cast<uint8_t>(ProtocolCommand::Ack) ||
            data[1] == static_cast<uint8_t>(ProtocolCommand::Pong);
        if (config_.ack_timeout_ms <= 0 || skip_ack_wait) {
            return true;
        }
        if (waitForAck(data[1], data[3])) {
            return true;
        }
        std::cout << "[WARN] ACK timeout for cmd=" << Protocol::commandName(data[1])
                  << " seq=" << static_cast<int>(data[3])
                  << " retry=" << attempt << "/" << attempts << "\n";
    }
    return false;
}

/**
 * @brief 读取当前可用的串口字节。
 * @param data 输出读取到的字节。
 * @return 读取成功返回 true。
 */
bool SerialPort::readAvailable(std::vector<uint8_t>& data) {
    data.clear();
    if (!config_.enable || config_.mock) {
        return true;
    }

    uint8_t buffer[256];
    const ssize_t count = ::read(fd_, buffer, sizeof(buffer));
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        std::cerr << "Serial read failed: " << std::strerror(errno) << "\n";
        return false;
    }
    if (count > 0) {
        rx_buffer_.insert(rx_buffer_.end(), buffer, buffer + count);
    }
    if (!tryExtractPacket(data)) {
        return true;
    }
    if (config_.print_rx_hex || config_.print_packet_hex) {
        std::cout << "[RX HEX] " << ByteConverter::toHex(data) << "\n";
    }
    if (config_.print_parsed_packet) {
        printParsedPacket("[RX PARSED]", data);
    }
    return true;
}

bool SerialPort::writeAck(uint8_t acked_cmd, uint8_t acked_seq) {
    return write(protocolInstance().makeAckPacket(acked_cmd, acked_seq));
}

bool SerialPort::writePong() {
    return write(protocolInstance().makePongPacket());
}

/**
 * @brief 关闭串口资源。
 */
void SerialPort::close() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = -1;
    rx_buffer_.clear();
}

bool SerialPort::waitForAck(uint8_t expected_cmd, uint8_t expected_seq) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.ack_timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::vector<uint8_t> packet;
        if (!readAvailable(packet)) {
            return false;
        }
        if (!packet.empty()) {
            const ParsedPacket parsed = protocolInstance().parsePacket(packet);
            if (parsed.valid &&
                parsed.cmd == static_cast<uint8_t>(ProtocolCommand::Ack) &&
                parsed.payload.size() >= 2 &&
                parsed.payload[0] == expected_cmd &&
                parsed.payload[1] == expected_seq) {
                std::cout << "[ACK] received for " << Protocol::commandName(expected_cmd)
                          << " seq=" << static_cast<int>(expected_seq) << "\n";
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool SerialPort::tryExtractPacket(std::vector<uint8_t>& packet) {
    packet.clear();
    while (!rx_buffer_.empty() && rx_buffer_.front() != 0xA5) {
        rx_buffer_.erase(rx_buffer_.begin());
    }
    if (rx_buffer_.size() < 4) {
        return false;
    }

    const size_t expected_size = 4U + static_cast<size_t>(rx_buffer_[2]) + 2U;
    if (rx_buffer_.size() < expected_size) {
        return false;
    }

    packet.assign(rx_buffer_.begin(), rx_buffer_.begin() + static_cast<std::ptrdiff_t>(expected_size));
    rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + static_cast<std::ptrdiff_t>(expected_size));
    return true;
}

void SerialPort::printParsedPacket(const std::string& prefix, const std::vector<uint8_t>& packet) const {
    const ParsedPacket parsed = protocolInstance().parsePacket(packet);
    if (!parsed.valid) {
        std::cout << prefix << " invalid reason=" << parsed.reason << "\n";
        return;
    }
    std::cout << prefix
              << " cmd=" << Protocol::commandName(parsed.cmd)
              << " seq=" << static_cast<int>(parsed.seq)
              << " len=" << static_cast<int>(parsed.length)
              << " payload=" << ByteConverter::toHex(parsed.payload) << "\n";
}
