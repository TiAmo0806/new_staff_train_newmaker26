#include "communication/ByteConverter.h"
#include "communication/Protocol.h"
#include "communication/SerialPort.h"
#include "core/AppConfig.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct DemoOptions {
    bool mock = false;                      // true 时不打开真实串口，只打印 TX 包。
    std::string port = "/dev/ttyACM0";      // NUC/Ubuntu 上常见串口设备名。
    int baudrate = 115200;                  // 默认波特率，与主工程 serial 配置保持一致。
    bool send_bean_bind = false;
    bool send_final_task = false;
    bool listen = false;
};

/**
 * @brief 打印 demo 使用方法。
 * @param app 当前可执行文件名。
 */
void printUsage(const char* app) {
    std::cout << "Usage:\n";
    std::cout << "  " << app << " --mock\n";
    std::cout << "  " << app << " --port /dev/ttyACM0 --baudrate 115200 --send-bean-bind\n";
    std::cout << "  " << app << " --port /dev/ttyUSB0 --baudrate 115200 --send-final-task\n";
    std::cout << "  " << app << " --port /dev/ttyACM0 --baudrate 115200 --listen\n";
    std::cout << "  " << app << " --port /dev/ttyACM0 --baudrate 115200 --send-bean-bind --send-final-task --listen\n";
    std::cout << "\n";
    std::cout << "Options:\n";
    std::cout << "  --mock              Do not open a real serial port; print demo packets locally.\n";
    std::cout << "  --port <device>     Serial device path, e.g. /dev/ttyACM0 or /dev/ttyUSB0.\n";
    std::cout << "  --baudrate <value>  Serial baudrate. Current protocol default is 115200.\n";
    std::cout << "  --send-bean-bind    Send one demo BEAN_BIND packet.\n";
    std::cout << "  --send-final-task   Send one demo FINAL_TASK packet.\n";
    std::cout << "  --listen            Keep reading RX bytes and parse incoming packets.\n";
    std::cout << "  --help, -h          Show this help message.\n";
    std::cout << "\n";
    std::cout << "Device name notes:\n";
    std::cout << "  /dev/ttyACM0 is common for USB CDC ACM devices.\n";
    std::cout << "  /dev/ttyUSB0 is common for USB-to-serial adapters.\n";
    std::cout << "\n";
    std::cout << "Default behavior:\n";
    std::cout << "  If no send/listen options are given:\n";
    std::cout << "  - in --mock mode: send demo BEAN_BIND and FINAL_TASK locally, then exit\n";
    std::cout << "  - in real serial mode: send demo BEAN_BIND and FINAL_TASK, then enter listen mode\n";
}

/**
 * @brief 解析命令行参数。
 * @param argc 参数数量。
 * @param argv 参数数组。
 * @return 串口 demo 运行选项。
 */
DemoOptions parseArgs(int argc, char** argv) {
    DemoOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--mock") {
            options.mock = true;
        } else if (arg == "--port" && i + 1 < argc) {
            options.port = argv[++i];
        } else if (arg == "--baudrate" && i + 1 < argc) {
            options.baudrate = std::stoi(argv[++i]);
        } else if (arg == "--send-bean-bind") {
            options.send_bean_bind = true;
        } else if (arg == "--send-final-task") {
            options.send_final_task = true;
        } else if (arg == "--listen") {
            options.listen = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            std::exit(1);
        }
    }
    return options;
}

/**
 * @brief 把协议命令字转换成便于阅读的名称。
 * @param cmd 协议命令字。
 * @return 命令名称。
 */
/**
 * @brief 打印协议解析结果。
 * @param prefix 日志前缀，例如 [RX PARSED] 或 [TX PARSED]。
 * @param parsed Protocol::parsePacket 的解析结果。
 */
void printParsed(const std::string& prefix, const ParsedPacket& parsed) {
    if (!parsed.valid) {
        std::cout << prefix << " invalid reason=" << parsed.reason << "\n";
        return;
    }

    std::cout << prefix
              << " cmd=0x" << std::hex << static_cast<int>(parsed.cmd) << std::dec
              << "(" << Protocol::commandName(parsed.cmd) << ")"
              << " seq=" << static_cast<int>(parsed.seq)
              << " length=" << static_cast<int>(parsed.length)
              << " payload=" << ByteConverter::toHex(parsed.payload)
              << "\n";
}

/**
 * @brief 构造一组示例豆子绑定关系。
 * @return 三个取货点对应目标数字的绑定关系。
 */
std::vector<BeanBind> makeDemoBinds() {
    return {
        {"P1", "soybean", "digit_1", 0.91f, true},
        {"P2", "mung_bean", "digit_2", 0.88f, true},
        {"P3", "white_kidney_bean", "digit_3", 0.86f, true},
    };
}

/**
 * @brief 构造一组示例最终搬运任务。
 * @return 可用于发送 FINAL_TASK 包的任务结果。
 */
TaskResult makeDemoTaskResult() {
    TaskResult result;
    result.success = true;
    result.reason = "ok";
    result.tasks = {
        {1, 6, 0},
        {2, 5, 1},
        {3, 7, 2},
    };
    return result;
}

/**
 * @brief 发送协议包并同时打印十六进制和解析结果。
 * @param serial 串口模块，mock/real 都复用主工程 SerialPort。
 * @param protocol 协议模块，用于解析刚构造出的包。
 * @param name 示例包名称。
 * @param packet 要发送的完整协议帧。
 */
void sendAndPrint(SerialPort& serial, Protocol& protocol, const std::string& name, const std::vector<uint8_t>& packet) {
    std::cout << "[TX PARSED] " << name << "\n";
    printParsed("[TX PARSED]", protocol.parsePacket(packet));
    std::cout << "[TX HEX] " << ByteConverter::toHex(packet) << "\n";
    serial.write(packet);
}

}  // namespace

int main(int argc, char** argv) {
    DemoOptions options = parseArgs(argc, argv);

    // demo 不读取 yaml，直接把命令行参数转换成主工程 SerialConfig。
    SerialConfig serial_config;
    serial_config.enable = !options.mock;
    serial_config.mock = options.mock;
    serial_config.port = options.port;
    serial_config.baudrate = options.baudrate;
    serial_config.ack_timeout_ms = 0;
    serial_config.print_tx_hex = true;
    serial_config.print_rx_hex = true;
    serial_config.print_parsed_packet = true;

    Protocol protocol;
    SerialPort serial(serial_config);
    if (!serial.open()) {
        return 1;
    }

    if (!options.send_bean_bind && !options.send_final_task && !options.listen) {
        options.send_bean_bind = true;
        options.send_final_task = true;
        options.listen = !options.mock;
    }

    std::cout << "serial_protocol_demo started. mock=" << (options.mock ? "true" : "false")
              << " port=" << options.port
              << " baudrate=" << options.baudrate << "\n";

    if (options.send_bean_bind) {
        const std::vector<uint8_t> bean_bind_packet = protocol.makeBeanBindPacket(makeDemoBinds());
        sendAndPrint(serial, protocol, "BEAN_BIND demo", bean_bind_packet);
    }

    if (options.send_final_task) {
        const std::vector<uint8_t> final_task_packet = protocol.makeTaskPacket(makeDemoTaskResult());
        sendAndPrint(serial, protocol, "FINAL_TASK demo", final_task_packet);
    }

    if (options.mock || !options.listen) {
        std::cout << "Mock mode finished. No RX bytes are read.\n";
        serial.close();
        return 0;
    }

    std::cout << "Listening for RX bytes. Press Ctrl+C to stop.\n";
    while (true) {
        std::vector<uint8_t> rx;
        if (!serial.readAvailable(rx)) {
            break;
        }
        if (!rx.empty()) {
            // 这里假设一次 readAvailable 读到的是一包完整数据。
            // 如果后续 C 板连续发包或半包到达，需要在这里补流式拆包缓存。
            std::cout << "[RX HEX] " << ByteConverter::toHex(rx) << "\n";
            printParsed("[RX PARSED]", protocol.parsePacket(rx));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    serial.close();
    return 0;
}
