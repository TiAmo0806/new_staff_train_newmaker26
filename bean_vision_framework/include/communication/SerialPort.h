#pragma once

#include "core/AppConfig.h"

#include <cstdint>
#include <string>
#include <vector>

class SerialPort {
public:
    /**
     * @brief 构造串口模块。
     * @param config 串口配置，包含端口名、波特率、mock 开关等。
     */
    explicit SerialPort(const SerialConfig& config);

    /**
     * @brief 打开串口或进入 mock 模式。
     * @return 打开成功返回 true，失败返回 false。
     *
     * mock=true 时不打开硬件，只打印数据；Linux/NUC 下 mock=false 会打开真实串口。
     */
    bool open();

    /**
     * @brief 发送字节数据。
     * @param data 要发送的完整协议帧。
     * @return 发送成功返回 true，失败返回 false。
     *
     * mock 模式下会把数据打印成十六进制字符串。
     */
    bool write(const std::vector<uint8_t>& data);

    /**
     * @brief 读取当前可用的串口字节。
     * @param data 输出读取到的字节。
     * @return 读取成功返回 true；mock 或暂无数据时 data 为空。
     */
    bool readAvailable(std::vector<uint8_t>& data);

    bool writeAck(uint8_t acked_cmd, uint8_t acked_seq);

    bool writePong();

    /**
     * @brief 关闭串口资源。
     *
     * 当前 mock 模式下没有真实资源需要关闭。
     */
    void close();

private:
    bool waitForAck(uint8_t expected_cmd, uint8_t expected_seq);
    bool tryExtractPacket(std::vector<uint8_t>& packet);
    void printParsedPacket(const std::string& prefix, const std::vector<uint8_t>& packet) const;

    SerialConfig config_;
    int fd_ = -1;
    std::vector<uint8_t> rx_buffer_;
};
