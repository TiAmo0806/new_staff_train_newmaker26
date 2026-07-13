/**
 * @file    vision.c
 * @brief   抓豆分拣 C板 - 视觉通信协议实现
 * @note    与 VisionNode (NUC) 通过 USART 通信
 *          协议: 虚拟串口通信协议6.0  (9字节检测包 + 8字节数字包)
 *
 * 工作流程:
 *   1. UART 中断逐字节接收 → vision_uart_rx_callback()
 *   2. vision_task() 从 rx_buf 中查找完整帧 → 校验 CRC → 解析
 *   3. 解析结果写入 vision_real
 *   4. 调用 vision_on_beans_received / vision_on_digits_received
 *      执行实际控制逻辑
 *
 * 状态包发送 (C板→Vision): [状态包] 可选，暂时不用
 *   vision_send_status() 打包 11字节状态帧通过 UART 发出
 */

#include "vision.h"

// ======================== 全局变量 ========================

VisionData_t vision_real = {0};        // 视觉数据全局实例
static UART_HandleTypeDef *vision_uart = NULL;  // 串口句柄
static uint8_t vision_rx_byte;                   // 单字节接收缓冲

// ======================== CRC16-CCITT (与 VisionNode 一致) ========================

static const uint16_t crc16_table[256] = {
    0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
    0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7,
    0x1081, 0x0108, 0x3393, 0x221A, 0x56A5, 0x472C, 0x75B7, 0x643E,
    0x9CC9, 0x8D40, 0xBFDB, 0xAE52, 0xDAED, 0xCB64, 0xF9FF, 0xE876,
    0x2102, 0x308B, 0x0210, 0x1399, 0x6726, 0x76AF, 0x4434, 0x55BD,
    0xAD4A, 0xBCC3, 0x8E58, 0x9FD1, 0xEB6E, 0xFAE7, 0xC87C, 0xD9F5,
    0x3183, 0x200A, 0x1291, 0x0318, 0x77A7, 0x662E, 0x54B5, 0x453C,
    0xBDCB, 0xAC42, 0x9ED9, 0x8F50, 0xFBEF, 0xEA66, 0xD8FD, 0xC974,
    0x4204, 0x538D, 0x6116, 0x709F, 0x0420, 0x15A9, 0x2732, 0x36BB,
    0xCE4C, 0xDFC5, 0xED5E, 0xFCD7, 0x8868, 0x99E1, 0xAB7A, 0xBAF3,
    0x5285, 0x430C, 0x7197, 0x601E, 0x14A1, 0x0528, 0x37B3, 0x263A,
    0xDECD, 0xCF44, 0xFDDF, 0xEC56, 0x98E9, 0x8960, 0xBBFB, 0xAA72,
    0x6306, 0x728F, 0x4014, 0x519D, 0x2522, 0x34AB, 0x0630, 0x17B9,
    0xEF4E, 0xFEC7, 0xCC5C, 0xDDD5, 0xA96A, 0xB8E3, 0x8A78, 0x9BF1,
    0x7387, 0x620E, 0x5095, 0x411C, 0x35A3, 0x242A, 0x16B1, 0x0738,
    0xFFCF, 0xEE46, 0xDCDD, 0xCD54, 0xB9EB, 0xA862, 0x9AF9, 0x8B70,
    0x8408, 0x9581, 0xA71A, 0xB693, 0xC22C, 0xD3A5, 0xE13E, 0xF0B7,
    0x0840, 0x19C9, 0x2B52, 0x3ADB, 0x4E64, 0x5FED, 0x6D76, 0x7CFF,
    0x9489, 0x8500, 0xB79B, 0xA612, 0xD2AD, 0xC324, 0xF1BF, 0xE036,
    0x18C1, 0x0948, 0x3BD3, 0x2A5A, 0x5EE5, 0x4F6C, 0x7DF7, 0x6C7E,
    0xA50A, 0xB483, 0x8618, 0x9791, 0xE32E, 0xF2A7, 0xC03C, 0xD1B5,
    0x2942, 0x38CB, 0x0A50, 0x1BD9, 0x6F66, 0x7EEF, 0x4C74, 0x5DFD,
    0xB58B, 0xA402, 0x9699, 0x8710, 0xF3AF, 0xE226, 0xD0BD, 0xC134,
    0x39C3, 0x284A, 0x1AD1, 0x0B58, 0x7FE7, 0x6E6E, 0x5CF5, 0x4D7C,
    0xC60C, 0xD785, 0xE51E, 0xF497, 0x8028, 0x91A1, 0xA33A, 0xB2B3,
    0x4A44, 0x5BCD, 0x6956, 0x78DF, 0x0C60, 0x1DE9, 0x2F72, 0x3EFB,
    0xD68D, 0xC704, 0xF59F, 0xE416, 0x90A9, 0x8120, 0xB3BB, 0xA232,
    0x5AC5, 0x4B4C, 0x79D7, 0x685E, 0x1CE1, 0x0D68, 0x3FF3, 0x2E7A,
    0xE70E, 0xF687, 0xC41C, 0xD595, 0xA12A, 0xB0A3, 0x8238, 0x93B1,
    0x6B46, 0x7ACF, 0x4854, 0x59DD, 0x2D62, 0x3CEB, 0x0E70, 0x1FF9,
    0xF78F, 0xE606, 0xD49D, 0xC514, 0xB1AB, 0xA022, 0x92B9, 0x8330,
    0x7BC7, 0x6A4E, 0x58D5, 0x495C, 0x3DE3, 0x2C6A, 0x1EF1, 0x0F78
};

uint16_t vision_crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFF;
    uint8_t ch;
    while (len--)
    {
        ch = *data++;
        crc = (crc >> 8) ^ crc16_table[(crc ^ ch) & 0xFF];
    }
    return crc;
}

uint8_t vision_crc_verify(const uint8_t *frame, uint32_t frame_len)
{
    if (frame_len <= 2) return 0;
    uint16_t expected = vision_crc16(frame, frame_len - 2);
    // CRC 存储: [crcL] [crcH] (小端)
    return ((expected & 0xFF) == frame[frame_len - 2] &&
            ((expected >> 8) & 0xFF) == frame[frame_len - 1]);
}

// ======================== 工具函数 ========================

const char* vision_bean_name(uint8_t type)
{
    switch (type)
    {
        case BEAN_NONE:    return "无";
        case BEAN_SOY:     return "黄豆";
        case BEAN_MUNG:    return "绿豆";
        case BEAN_KIDNEY:  return "白芸豆";
        case DATA_1:       return "标签1";
        case DATA_2:       return "标签2";
        case DATA_3:       return "标签3";
        case DATA_4:       return "标签4";
        case DATA_5:       return "标签5";
        default:           return "未知";
    }
}

uint8_t vision_bean_to_bin(uint8_t type)
{
    switch (type)
    {
        case BEAN_SOY:     return 1;
        case BEAN_MUNG:    return 2;
        case BEAN_KIDNEY:  return 3;
        case DATA_1:       return 1;
        case DATA_2:       return 2;
        case DATA_3:       return 3;
        case DATA_4:       return 4;
        case DATA_5:       return 5;
        default:           return 0;
    }
}

// ======================== 帧解析 ========================

/**
 * @brief 从缓冲区中查找并提取完整帧
 * @param buf        缓冲区
 * @param buf_len    缓冲区有效长度
 * @param header     期望帧头 (0xAA / 0xCC)
 * @param size       期望帧总长度
 * @param out_frame  输出: 完整帧数据
 * @return 1=找到, 0=未找到
 */
static int scan_frame(const uint8_t *buf, int buf_len,
                      uint8_t header, int size,
                      uint8_t *out_frame)
{
    if (buf_len < size) return 0;

    for (int i = 0; i <= buf_len - size; i++)
    {
        if (buf[i] == header)
        {
            if (vision_crc_verify(&buf[i], size))
            {
                memcpy(out_frame, &buf[i], size);
                return i + size;  // 返回帧结束后的位置 (用于清除已处理数据)
            }
        }
    }
    return 0;
}

/**
 * @brief 解析 0xAA 检测包
 */
static void parse_detection_packet(const uint8_t *frame)
{
    // frame[0] = 0xAA
    // frame[1] = bean_types[0], frame[2] = target_bins[0]
    // frame[3] = bean_types[1], frame[4] = target_bins[1]
    // frame[5] = bean_types[2], frame[6] = target_bins[2]
    // frame[7-8] = CRC

    vision_real.bean_types[0]  = frame[1];
    vision_real.target_bins[0] = frame[2];
    vision_real.bean_types[1]  = frame[3];
    vision_real.target_bins[1] = frame[4];
    vision_real.bean_types[2]  = frame[5];
    vision_real.target_bins[2] = frame[6];

    vision_real.detection_tick_ms = HAL_GetTick();
    vision_real.beans_valid = 1;
}

/**
 * @brief 解析 0xCC 数字标签包
 */
static void parse_number_packet(const uint8_t *frame)
{
    // frame[0] = 0xCC
    // frame[1~5] = digits[0~5]
    // frame[6-7] = CRC

    for (int i = 0; i < 5; i++)
        vision_real.digits[i] = frame[1 + i];

    vision_real.digit_tick_ms = HAL_GetTick();
    vision_real.digits_valid = 1;
}

// [状态包] ======================== 状态包发送 (C板 → Vision) 暂时不用 ========================

// void vision_send_status(uint8_t state, uint8_t flags,
//                         uint8_t bin, uint8_t error)
// {
//     uint8_t frame[VISION_STATUS_SIZE];
//     uint32_t ts = HAL_GetTick();
//
//     frame[0] = VISION_HEADER_STATUS;  // 0xBB
//     frame[1] = state;
//     frame[2] = flags;
//     frame[3] = bin;
//     frame[4] = error;
//     frame[5] = ts & 0xFF;
//     frame[6] = (ts >> 8) & 0xFF;
//     frame[7] = (ts >> 16) & 0xFF;
//     frame[8] = (ts >> 24) & 0xFF;
//
//     // CRC
//     uint16_t crc = vision_crc16(frame, VISION_STATUS_SIZE - 2);
//     frame[9]  = crc & 0xFF;
//     frame[10] = (crc >> 8) & 0xFF;
//
//     // 通过 UART 发送
//     if (vision_uart)
//         HAL_UART_Transmit(vision_uart, frame, VISION_STATUS_SIZE, 100);
// }

// ======================== 初始化 ========================

void vision_init(UART_HandleTypeDef *huart)
{
    vision_uart = huart;
    memset(&vision_real, 0, sizeof(VisionData_t));
    // [状态包] vision_real.state = STATE_IDLE;

    // 启动 UART 单字节接收中断
    HAL_UART_Receive_IT(vision_uart, &vision_rx_byte, 1);
}

// ======================== 中断回调 (每字节) ========================

void vision_uart_rx_callback(uint8_t byte)
{
    // 写入环形缓冲 (简单 FIFO, 溢出则覆盖尾部)
    if (vision_real.rx_len < VISION_RX_BUF_SIZE)
    {
        vision_real.rx_buf[vision_real.rx_len++] = byte;
    }
    else
    {
        // 缓冲区满, 整体左移丢弃最早的一半
        int half = VISION_RX_BUF_SIZE / 2;
        memmove(vision_real.rx_buf, vision_real.rx_buf + half,
                VISION_RX_BUF_SIZE - half);
        vision_real.rx_len = VISION_RX_BUF_SIZE - half;
        vision_real.rx_buf[vision_real.rx_len++] = byte;
    }

    // 重新启动接收
    if (vision_uart)
        HAL_UART_Receive_IT(vision_uart, &vision_rx_byte, 1);
}

// ======================== 主任务 (周期调用) ========================

static int vision_find_and_parse(uint8_t header, int size)
{
    uint8_t frame[20];
    int end = scan_frame(vision_real.rx_buf, vision_real.rx_len,
                          header, size, frame);
    if (end > 0)
    {
        // 清除已处理的字节
        int remaining = vision_real.rx_len - end;
        if (remaining > 0)
            memmove(vision_real.rx_buf, vision_real.rx_buf + end, remaining);
        vision_real.rx_len = remaining;

        // 解析帧
        if (header == VISION_HEADER_DETECTION)
            parse_detection_packet(frame);
        else if (header == VISION_HEADER_NUMBER)
            parse_number_packet(frame);

        return 1;
    }

    // 防止无用数据堆积: 缓冲超过大小时清理
    if (vision_real.rx_len >= VISION_RX_BUF_SIZE - 4)
    {
        // 从后往前找可能的帧头
        int i;
        for (i = vision_real.rx_len - 1; i >= 0; i--)
        {
            if (vision_real.rx_buf[i] == VISION_HEADER_DETECTION ||
                vision_real.rx_buf[i] == VISION_HEADER_NUMBER)
                break;
        }
        if (i > 0)
        {
            vision_real.rx_len -= i;
            memmove(vision_real.rx_buf, vision_real.rx_buf + i, vision_real.rx_len);
        }
        else
        {
            vision_real.rx_len = 0;
        }
    }

    return 0;
}

void vision_task(void)
{
    // 1. 查找并解析 0xAA 检测包
    if (vision_find_and_parse(VISION_HEADER_DETECTION, VISION_DETECTION_SIZE))
    {
        // 打印日志 (通过串口调试)
        // printf("[视觉] << 豆子: %s→%d箱  %s→%d箱  %s→%d箱\n",
        //     vision_bean_name(vision_real.bean_types[0]), vision_real.target_bins[0],
        //     vision_bean_name(vision_real.bean_types[1]), vision_real.target_bins[1],
        //     vision_bean_name(vision_real.bean_types[2]), vision_real.target_bins[2]);

        // 通知控制层
        vision_on_beans_received(&vision_real);
    }

    // 2. 查找并解析 0xCC 数字包
    if (vision_find_and_parse(VISION_HEADER_NUMBER, VISION_NUMBER_SIZE))
    {
        // printf("[视觉] << 数字标签: %d %d %d %d %d\n",
        //     vision_real.digits[0], vision_real.digits[1],
        //     vision_real.digits[2], vision_real.digits[3], vision_real.digits[4]);

        // 通知控制层
        vision_on_digits_received(&vision_real);
    }
}

// ======================== 用户控制接口 (弱引用/默认实现) ========================

/**
 * @brief 收到豆子检测包 (0xAA) 后的默认回调
 *        用户应在本文件中重写此函数, 实现抓放逻辑
 *
 * 典型流程:
 *   1. 检查 vision_real.bean_types[i] 确定豆子类型
 *   2. 检查 vision_real.target_bins[i] 确定目标箱号
 *   3. 控制电机移动到豆子位置
 *   4. 控制夹爪夹取
 *   5. 控制电机移动到目标料箱
 *   6. 控制夹爪释放
 *   7. 循环处理三个位置
 *   8. 处理完发送 vision_send_status(STATE_COMPLETED, ...)
 */
void vision_on_beans_received(VisionData_t *data)
{
    /* ========== 用户在此实现抓放逻辑 ========== */

    // 示例: 处理3个位置
    for (int i = 0; i < 3; i++)
    {
        if (data->bean_types[i] == BEAN_NONE)
        {
            // 空位, 跳过
            continue;
        }

        // 1. 移动豆子位置
        // 2. 夹爪闭合
        // 3. 移动到 data->target_bins[i] 号料箱
        // 4. 夹爪张开
    }

    data->beans_valid = 0;  // 清除标志
}

/**
 * @brief 收到数字标签包 (0xCC) 后的默认回调
 *        用户应在本文件中重写此函数
 *
 * 典型用途:
 *   - 记录料箱编号位置, 校准料箱坐标
 *   - 确认当前分区正确
 */
void vision_on_digits_received(VisionData_t *data)
{
    /* ========== 用户在此实现数字标签处理 ========== */

    // 例如: 将5个料箱编号保存到全局变量
    // bin_numbers[0] = data->digits[0];
    // ...

    data->digits_valid = 0;  // 清除标志
}

// ======================== HAL UART 接收中断回调 ========================

/**
 * @brief HAL 库 UART 接收完成中断回调
 * @note  在 stm32f4xx_it.c 的 USARTx_IRQHandler 中,
 *        HAL_UART_IRQHandler 会自动调用此函数
 *
 * 用法: 在 main.c 中确保:
 *   void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
 *   {
 *       if (huart->Instance == USARTx)  // 改为实际 USART
 *           vision_uart_rx_callback(vision_rx_byte);
 *   }
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == vision_uart)
    {
        vision_uart_rx_callback(vision_rx_byte);
    }
}
