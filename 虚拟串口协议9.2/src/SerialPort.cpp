/**
 * @file SerialPort.cpp
 * @brief 串口通信类实现
 */

#include "SerialPort.h"
#include "CRC16.hpp"
#include "BeanProtocol.hpp"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <chrono>//提供时间相关功能
#include <termios.h>//串口配置头文件
#include <unistd.h>
#include <fcntl.h>//提供文件控制函数
#include <dirent.h>//用于查找串口设备
#include <sys/select.h>
#include <sys/stat.h>

namespace bean_sorter
{

SerialPort::SerialPort(const std::string & portName)
  : fd_(-1)
  , portName_(portName)// 串口设备路径
  , simulated_(false)// 是否模拟模式
  , txLogEnabled_(false)// 是否打印发送日志
  , rxLogEnabled_(false)// 是否打印接收日志
  , autoReconnect_(true) // 是否自动重连
{
}

SerialPort::~SerialPort()
{
  Close();
}

bool SerialPort::Open()
{
  if (IsOpen()) return true;

  fd_ = ::open(portName_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);//打开串口
  if (fd_ < 0) {
    std::cerr << "[串口] 打开失败 " << portName_ << std::endl;
    return false;
  }

  if (!ConfigurePort()) {//配置串口参数（波特率、数据位、停止位等）
    Close();
    return false;
  }

  std::cout << "[串口] 已打开 " << portName_ << " (fd=" << fd_ << ")" << std::endl;
  return true;
}

void SerialPort::Close()
{
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool SerialPort::ConfigurePort()
{
  struct termios tty;
  std::memset(&tty, 0, sizeof(tty));

  if (tcgetattr(fd_, &tty) != 0) {
    std::cerr << "[串口] 获取属性失败" << std::endl;
    return false;
  }

  
  //------------------------ 串口通信协议参数---------------------------


  cfsetispeed(&tty, B115200);//波特率输入
  cfsetospeed(&tty, B115200);//波特率输出

  tty.c_cflag &= ~PARENB;    // 无校验位
  tty.c_cflag &= ~CSTOPB;    // 1位停止位
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8;        // 8位数据位
  tty.c_cflag &= ~CRTSCTS;   // 115200 波特率、8 数据位、No 校验、1 停止位
  tty.c_cflag |= CREAD | CLOCAL;

  tty.c_lflag &= ~ICANON;    // Raw mode
  tty.c_lflag &= ~ECHO;
  tty.c_lflag &= ~ECHOE;
  tty.c_lflag &= ~ECHONL;
  tty.c_lflag &= ~ISIG;

  tty.c_iflag &= ~(IXON | IXOFF | IXANY);
  tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

  tty.c_oflag &= ~OPOST;
  tty.c_oflag &= ~ONLCR;

  // For ReadFrame: use blocking reads, we handle timeout via select
  tty.c_cc[VMIN]  = 1;
  tty.c_cc[VTIME] = 0;

  if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
    std::cerr << "[串口] 设置属性失败" << std::endl;
    return false;
  }

  tcflush(fd_, TCIOFLUSH);// 清空缓冲区
  return true;
}

size_t SerialPort::Write(const uint8_t * data, size_t len)//写入数据
{
  if (simulated_) {
    if (txLogEnabled_) {
      std::cout << "[串口][SIM] TX " << len << " 字节:";
      hexDump(data, len);
      std::cout << std::endl;
    }
    return len;
  }
  if (!IsOpen()) return 0;

  ssize_t ret = ::write(fd_, data, len);
  if (ret < 0) return 0;

  if (txLogEnabled_) {
    std::cout << "[串口] TX " << ret << " 字节:";
    hexDump(data, static_cast<uint32_t>(ret));
    std::cout << std::endl;
  }
  return static_cast<size_t>(ret);
}

size_t SerialPort::Write(const std::vector<uint8_t> & data)
{
  return Write(data.data(), data.size());
}

bool SerialPort::SendFrame(const uint8_t * frame, uint32_t len, int maxRetries)//发送完整帧
{
  if (simulated_) {
    if (txLogEnabled_) {
      std::cout << "[串口][SIM] Frame TX " << len << " 字节:";
      hexDump(frame, len);
      std::cout << std::endl;
    }
    return true;
  }

  for (int retry = 0; retry < maxRetries; ++retry) {
    if (Write(frame, len) == len) return true;
    if (retry < maxRetries - 1) usleep(2000);
  }

  std::string err = "发送失败，重试 " + std::to_string(maxRetries) + " 次";
  std::cerr << "[串口] 错误: " << err << std::endl;
  if (errorCb_) errorCb_(err);

  if (autoReconnect_ && TryReconnect()) {
    if (Write(frame, len) == len) return true;
  }
  return false;
}

size_t SerialPort::Read(uint8_t * buf, size_t max_len)//读取数据
{
  if (simulated_ || !IsOpen()) return 0;
  ssize_t n = ::read(fd_, buf, max_len);
  if (n < 0) return 0;

  if (rxLogEnabled_ && n > 0) {
    std::cout << "[串口] RX " << n << " 字节:";
    hexDump(buf, static_cast<uint32_t>(n));
    std::cout << std::endl;
  }
  return static_cast<size_t>(n);
}

size_t SerialPort::ReadTimeout(uint8_t * buf, size_t max_len, int timeout_ms)
{
  if (simulated_ || !IsOpen()) return 0;

  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(fd_, &read_fds);

  struct timeval tv;
  tv.tv_sec  = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  int ret = select(fd_ + 1, &read_fds, nullptr, nullptr, &tv);
  if (ret <= 0) return 0;

  ssize_t n = ::read(fd_, buf, max_len);
  if (n < 0) return 0;

  if (rxLogEnabled_ && n > 0) {
    std::cout << "[串口] RX " << n << " 字节:";
    hexDump(buf, static_cast<uint32_t>(n));
    std::cout << std::endl;
  }
  return static_cast<size_t>(n);
}

std::vector<uint8_t> SerialPort::ReadFrame(uint8_t expected_header,
                                            uint32_t expected_size,
                                            int timeout_ms)
{
  std::vector<uint8_t> buffer;// 接收缓冲区
  buffer.reserve(expected_size * 2);// 预分配内存
  auto start = std::chrono::steady_clock::now();

  while (true) {
    // Check timeout
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    if (elapsed_ms >= timeout_ms) {
      if (rxLogEnabled_)
        std::cout << "[串口] 读帧超时 " << timeout_ms << "ms" << std::endl;
      return {};
    }

    int remaining_ms = timeout_ms - static_cast<int>(elapsed_ms);
    if (remaining_ms < 10) remaining_ms = 10;

    uint8_t byte;
    size_t n = ReadTimeout(&byte, 1, remaining_ms);
    if (n == 0) continue;

    buffer.push_back(byte);//存入缓冲区

    // Scan buffer for a valid frame
    size_t scan_start = (buffer.size() > expected_size * 2)
                          ? buffer.size() - expected_size * 2 : 0;

    for (size_t i = scan_start; i + expected_size <= buffer.size(); ++i) {
      if (buffer[i] == expected_header) {
        if (crc16::Verify_CRC16_Check_Sum(&buffer[i], expected_size)) {
          std::vector<uint8_t> frame(buffer.begin() + i, buffer.begin() + i + expected_size);
          if (rxLogEnabled_) {
            std::cout << "[串口] Frame RX: ";
            hexDump(frame.data(), expected_size);
            std::cout << std::endl;
          }
          return frame;
        }
      }
    }
  }
}

void SerialPort::Flush()//清空缓冲区
{
  if (fd_ >= 0) tcflush(fd_, TCIOFLUSH);
}

std::string SerialPort::FindAvailablePort()//查找可用串口
{
  DIR * dir = opendir("/dev"); // 打开 /dev 目录
  if (!dir) return "";

  struct dirent * entry;
  std::string found;

  while ((entry = readdir(dir)) != nullptr) {
    std::string name = entry->d_name;
    if (name.find("ttyACM") == 0 || name.find("ttyUSB") == 0 ||
        name.find("ttyS") == 0 || name.find("pts/") != std::string::npos) {
      // For virtual ports (pts), we skip direct open test since they're dynamic
      if (name.find("pts/") == 0) continue;

      std::string fullPath = "/dev/" + name;
      int testFd = ::open(fullPath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
      if (testFd >= 0) {
        ::close(testFd);
        found = fullPath;
        break;
      }
    }
  }
  closedir(dir);
  return found;
}

bool SerialPort::TryReconnect()//自动重连
{
  if (!autoReconnect_) return false;

  std::cout << "[串口] 尝试重连..." << std::endl;
  Close();

  constexpr int kMaxWaitMs = 5000;
  constexpr int kIntervalMs = 200;

  std::string newPort;
  for (int waited = 0; waited < kMaxWaitMs; waited += kIntervalMs) {
    newPort = FindAvailablePort();
    if (!newPort.empty()) break;
    usleep(kIntervalMs * 1000);
  }

  if (newPort.empty()) {
    std::cerr << "[串口] 重连: 未找到可用串口" << std::endl;
    if (errorCb_) errorCb_("重连失败: 未找到可用串口");
    return false;
  }

  if (newPort != portName_) {
    std::cout << "[串口] 串口变更: " << portName_ << " -> " << newPort << std::endl;
    portName_ = newPort;
  }

  if (Open()) {
    std::cout << "[串口] 已重连到 " << portName_ << std::endl;
    return true;
  }

  return false;
}

} 
