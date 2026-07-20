/**
 * @file SerialPort.h
 * @brief 视觉抓豆分拣系统 - 串口通信类
 *
 * 融合 dart 项目的 VirtualSerial 特性:
 *   - 自动重连 (扫描 /dev/ttyACM* /dev/ttyUSB*)
 *   - 发送重试机制
 *   - 模拟模式 (无硬件可运行)
 *   - TX/RX 日志开关
 */

#ifndef BEAN_SORTER__SERIAL_PORT_H_
#define BEAN_SORTER__SERIAL_PORT_H_

#include <string>
#include <cstdint>
#include <vector>
#include <functional>

namespace bean_sorter
{

class SerialPort
{
public:
  /**
   * @param portName 串口设备名，如 "/dev/ttyACM0" 或虚拟端口如 "/dev/pts/2"
   */
  explicit SerialPort(const std::string & portName = "/dev/ttyACM0");
  ~SerialPort();

  SerialPort(const SerialPort &) = delete;
  SerialPort & operator=(const SerialPort &) = delete;

  // ---- 基础操作 ----
  bool Open();
  void Close();
  bool IsOpen() const { return fd_ >= 0; }

  // ---- 发送 ----
  size_t Write(const uint8_t * data, size_t len);
  size_t Write(const std::vector<uint8_t> & data);//重载

  /** 发送并自动 CRC + 重试 */
  bool SendFrame(const uint8_t * frame, uint32_t len, int maxRetries = 3);

  // ---- 接收 ----
  /** 读取指定字节数 (阻塞) */
  size_t Read(uint8_t * buf, size_t max_len);

  /** 带超时读取 (毫秒) */
  size_t ReadTimeout(uint8_t * buf, size_t max_len, int timeout_ms);

  /**
   * @brief 读取一个完整帧 (扫描帧头, 验证 CRC)
   * @param expected_header  期望的帧头字节
   * @param expected_size    完整帧长度 (含帧头和CRC)
   * @param timeout_ms      超时时间
   * @return 完整帧数据 (空表示超时/失败)
   */

   //从串口读取一帧完整数据
  std::vector<uint8_t> ReadFrame(uint8_t expected_header,// 要找的帧头
                                  uint32_t expected_size,// 要找的帧头
                                  int timeout_ms = 1000);// 最长等待时间

  /** 清空缓冲区 */
  void Flush();

  // 

// 设置/读取 模拟模式
void SetSimulated(bool simulated) { simulated_ = simulated; }    // 设置
bool IsSimulated() const { return simulated_; }                  // 读取

// 设置/读取 发送日志
void SetTxLogEnabled(bool enabled) { txLogEnabled_ = enabled; }  // 设置
bool IsTxLogEnabled() const { return txLogEnabled_; }            // 读取

// 设置/读取 接收日志
void SetRxLogEnabled(bool enabled) { rxLogEnabled_ = enabled; }  // 设置
bool IsRxLogEnabled() const { return rxLogEnabled_; }            // 读取

// 设置/读取 自动重连
void SetAutoReconnect(bool enabled) { autoReconnect_ = enabled; } // 设置
bool IsAutoReconnect() const { return autoReconnect_; }          // 读取

  /** 扫描可用串口设备 */
  std::string FindAvailablePort();

  /** 尝试重新连接 */
  bool TryReconnect();

  const std::string & GetPortName() const { return portName_; }

  /** 错误回调 (用于外部监控) */
  void SetErrorCallback(std::function<void(const std::string &)> cb) { errorCb_ = cb; }

private:
  bool ConfigurePort();

  int fd_;
  std::string portName_;
  bool simulated_;
  bool txLogEnabled_;
  bool rxLogEnabled_;
  bool autoReconnect_;
  std::function<void(const std::string &)> errorCb_;
};

} // namespace bean_sorter

#endif // BEAN_SORTER__SERIAL_PORT_H_
