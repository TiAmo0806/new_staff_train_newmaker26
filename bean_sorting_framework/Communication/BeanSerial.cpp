// Communication/BeanSerial.cpp
  // 对标飞镖 VirtualSerial.cpp

  #include "BeanSerial.h"
  #include "CRC16.hpp"

  #include <iostream>
  #include <iomanip>
  #include <cstring>

  #include <termios.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <dirent.h>
  #include <poll.h>
  #include <sys/ioctl.h>

  BeanSerial::BeanSerial(const std::string& portName)
      : portName_(portName) {}

  BeanSerial::~BeanSerial() { Close(); }

  // ================================================================
  // 串口打开 / 关闭
  // ================================================================
  bool BeanSerial::Open() {
      if (IsOpen()) return true;
      // ---- 新增: 展开通配符 ----
      if (portName_.find('*') != std::string::npos) {
          std::string resolved = FindAvailablePort();
          if (resolved.empty()) {
              std::cerr << "[BeanSerial] Error: 找不到匹配 " << portName_ << " 的设备" << std::endl;
              return false;
          }
          std::cout << "[BeanSerial] 解析 " << portName_ << " → " << resolved << std::endl;
          portName_ = resolved;
      }

      fd_ = open(portName_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
      if (fd_ < 0) {
          std::cerr << "[BeanSerial] Error: 无法打开串口 " << portName_ << std::endl;
          return false;
      }

      if (!ConfigurePort()) { Close(); return false; }

      std::cout << "[BeanSerial] 已连接 " << portName_ << std::endl;
      return true;
  }

  void BeanSerial::Close() {
      if (fd_ >= 0) { close(fd_); fd_ = -1; }
  }

  // ================================================================
  // 串口配置 (对标飞镖 ConfigurePort)
  // ================================================================
  bool BeanSerial::ConfigurePort() {
      struct termios tty;
      std::memset(&tty, 0, sizeof(tty));

      if (tcgetattr(fd_, &tty) != 0) {
          std::cerr << "[BeanSerial] Error: tcgetattr 失败" << std::endl;
          return false;
      }

      cfsetispeed(&tty, B115200);
      cfsetospeed(&tty, B115200);

      tty.c_cflag &= ~PARENB;
      tty.c_cflag &= ~CSTOPB;
      tty.c_cflag &= ~CSIZE;
      tty.c_cflag |= CS8;
      tty.c_cflag &= ~CRTSCTS;
      tty.c_cflag |= CREAD | CLOCAL;

      tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);
      tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK |
                       ISTRIP | INLCR | IGNCR | ICRNL);
      tty.c_oflag &= ~OPOST;
      tty.c_oflag &= ~ONLCR;

      tty.c_cc[VMIN]  = 0;
      tty.c_cc[VTIME] = 0;

      if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
          std::cerr << "[BeanSerial] Error: tcsetattr 失败" << std::endl;
          return false;
      }

      tcflush(fd_, TCIOFLUSH);
      return true;
  }

  // ================================================================
  // 发送数据帧
  // ================================================================
  bool BeanSerial::sendFrame(const uint8_t* frame, size_t len,
                             int maxRetries, const char* label) {
      if (simulated_) {
          if (txLogEnabled_)
              std::cout << "[BeanSerial] Simulated " << label << " len=" << len << std::endl;
          return true;
      }

      if (!IsOpen()) {
          std::cerr << "[BeanSerial] Error: 串口未打开" << std::endl;
          return false;
      }

      // ---- hex dump (对标飞镖) ----
      if (txLogEnabled_) {
          std::cout << "[BeanSerial] " << label << " frame: ";
          std::ios oldState(nullptr);
          oldState.copyfmt(std::cout);
          for (size_t i = 0; i < len; ++i)
              std::cout << std::uppercase << std::hex << std::setw(2)
                        << std::setfill('0') << static_cast<int>(frame[i])
                        << (i < len - 1 ? " " : "");
          std::cout.copyfmt(oldState);
          std::cout << std::endl;
      }

      // ---- 重试发送 (对标飞镖) ----
      for (int retry = 0; retry < maxRetries; ++retry) {
          if (write(fd_, frame, len) == static_cast<ssize_t>(len))
              return true;
          if (retry < maxRetries - 1)
              usleep(1000);
      }

      std::cerr << "[BeanSerial] Error: " << label
                << " 发送失败 (" << maxRetries << " retries)" << std::endl;

      if (autoReconnect_ && TryReconnect()) {
          std::cout << "[BeanSerial] 重连后重试发送..." << std::endl;
          if (write(fd_, frame, len) == static_cast<ssize_t>(len))
              return true;
      }

      return false;
  }

  bool BeanSerial::sendVision(const bean_sorting::VisionData& v, int maxRetries) {
      auto buf = bean_sorting::encode_vision(v);
      return sendFrame(buf.data(), buf.size(), maxRetries, "Vision→电控");
  }

  bool BeanSerial::sendControl(const bean_sorting::ControlData& c, int maxRetries) {
      auto buf = bean_sorting::encode_control(c);
      return sendFrame(buf.data(), buf.size(), maxRetries, "Control→视觉");
  }

  bool BeanSerial::sendBatch(const std::vector<bean_sorting::BatchEntry>& entries, int maxRetries) {
      auto buf = bean_sorting::encode_batch(entries);
      return sendFrame(buf.data(), buf.size(), maxRetries, "Batch→电控");
  }

  // ================================================================
  // 接收
  // ================================================================
  std::vector<uint8_t> BeanSerial::recvRaw(int timeout_ms) {
      std::vector<uint8_t> result;
      if (!IsOpen()) return result;

      struct pollfd pfd = {};
      pfd.fd = fd_;
      pfd.events = POLLIN;

      if (poll(&pfd, 1, timeout_ms) <= 0) return result;

      int available = 0;
      if (ioctl(fd_, FIONREAD, &available) != 0 || available <= 0) return result;

      result.resize(available);
      ssize_t n = read(fd_, result.data(), available);
      if (n > 0) result.resize(n);
      else result.clear();
      return result;
  }

  // ================================================================
  // 自动重连 (对标飞镖 TryReconnect + FindAvailablePort)
  // ================================================================
  std::string BeanSerial::FindAvailablePort() {
      DIR* dir = opendir("/dev");
      if (!dir) return "";

      struct dirent* entry;
      while ((entry = readdir(dir)) != nullptr) {
          std::string name = entry->d_name;
          if (name.find("ttyACM") == 0 || name.find("ttyUSB") == 0) {
              std::string full = "/dev/" + name;
              int testFd = open(full.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
              if (testFd >= 0) {
                  close(testFd);
                  closedir(dir);
                  return full;
              }
          }
      }
      closedir(dir);
      return "";
  }

  bool BeanSerial::TryReconnect() {
      if (!autoReconnect_) return false;

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
          std::cerr << "[BeanSerial] 重连: 未找到可用设备" << std::endl;
          return false;
      }

      if (newPort != portName_) {
          std::cout << "[BeanSerial] 重连: 设备变更 " << portName_
                    << " → " << newPort << std::endl;
          portName_ = newPort;
      }

      if (Open()) {
          std::cout << "[BeanSerial] 重连成功: " << portName_ << std::endl;
          return true;
      }
      return false;
  }
