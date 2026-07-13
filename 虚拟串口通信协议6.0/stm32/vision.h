/**
 * @file    vision.h
 * @brief   抓豆分拣 C板通信协议 - 接收视觉检测结果 + 控制反应
 * @note    与 VisionNode (NUC) 通过 USART 通信
 *          协议: 虚拟串口通信协议6.0
 *          硬件: STM32F407IGHX  Keil5  HAL库
 *
 * 集成说明:
 *   1. 在 USART 中断中调用 vision_uart_rx_callback()
 *   2. 主循环/RTOS任务中周期调用 vision_task()
 *   3. 读取 vision_real 结构体获取最新视觉数据
 *   4. 在 vision_on_beans_received / vision_on_digits_received
 *      中写自己的电机/夹爪控制逻辑
 */

#ifndef __VISION_H
#define __VISION_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <string.h>

// ======================== 协议定义 ========================

/* 帧头 */
#define VISION_HEADER_DETECTION     0xAA  // 豆子检测包
#define VISION_HEADER_NUMBER        0xCC  // 数字标签包
// [状态包] 可选，暂时不用
// #define VISION_HEADER_STATUS      0xBB  // 状态包 (C板→Vision)

/* 包大小 */
#define VISION_DETECTION_SIZE       9     // [0xAA] [type1] [bin1] [type2] [bin2] [type3] [bin3] [crcL] [crcH]
#define VISION_NUMBER_SIZE          8     // [0xCC] [d1] [d2] [d3] [d4] [d5] [crcL] [crcH]
// [状态包] 可选，暂时不用
// #define VISION_STATUS_SIZE        11    // [0xBB] [state] [flags] [bin] [err] [ts:4] [crcL] [crcH]

/* 豆子类型 (与 VisionNode 一致) */
#define BEAN_NONE       0
#define BEAN_SOY        1   // 黄豆 → 1号箱
#define BEAN_MUNG       2   // 绿豆 → 2号箱
#define BEAN_KIDNEY     3   // 白芸豆 → 3号箱
#define DATA_1          4   // 1号箱标签
#define DATA_2          5   // 2号箱标签
#define DATA_3          6   // 3号箱标签
#define DATA_4          7   // 4号箱标签
#define DATA_5          8   // 5号箱标签

// [状态包] 以下状态/错误/标志定义仅用于 0xBB 状态包，暂时不用
/* 系统状态 (C板→Vision 状态包用) */
// #define STATE_IDLE      0
// #define STATE_DETECTING 1
// #define STATE_MOVING    2
// #define STATE_GRIPPING  3
// #define STATE_PLACING   4
// #define STATE_COMPLETED 5
// #define STATE_ERROR     6

/* 错误码 */
// #define ERR_NONE        0
// #define ERR_GRIPPER     1
// #define ERR_BIN_FULL    2
// #define ERR_TIMEOUT     3
// #define ERR_COMM        4
// #define ERR_UNKNOWN     5

/* 标志位 (StatusPacket.flags) */
// #define FLAG_GRIPPER_OPEN   0x01
// #define FLAG_READY          0x02
// #define FLAG_DIGIT_SCAN     0x04

/* 接收缓冲区 */
#define VISION_RX_BUF_SIZE  128

// ======================== 核心数据结构 ========================

/**
 * @brief vision_real - 视觉数据全局结构体
 *        电控主逻辑读取此结构体获取视觉信息并做出反应
 */
typedef struct
{
    /* -------- 串口接收层 -------- */
    uint8_t  rx_buf[VISION_RX_BUF_SIZE];  // 接收环形缓冲
    uint16_t rx_len;                       // 缓冲中有效字节数
    uint16_t rx_err_cnt;                   // CRC错误计数

    /* -------- 豆子检测结果 (来自 0xAA 包) -------- */
    uint8_t  bean_types[3];       // 左/中/右 三个位置的豆子类型
    uint8_t  target_bins[3];      // 三个位置各自的目标箱号
    uint32_t detection_tick_ms;   // 收到 0xAA 时的时间戳 (HAL_GetTick)
    uint8_t  beans_valid;         // 1 = 新豆子数据已到达

    /* -------- 数字标签结果 (来自 0xCC 包) -------- */
    uint8_t  digits[5];           // 5个料箱数字标签 (左→右)
    uint32_t digit_tick_ms;       // 收到 0xCC 时的时间戳
    uint8_t  digits_valid;        // 1 = 新数字数据已到达

    // [状态包] 以下字段用于 0xBB 状态包，暂时不用
    // uint8_t  state;               // 当前执行状态 (STATE_IDLE/MOVING/...)
    // uint8_t  current_pos;         // 当前正在处理的位置 (0~2)
    // uint8_t  cycle_busy;          // 1 = 正在执行抓放周期, 0 = 空闲等视觉
    // uint8_t  send_enable;         // 1 = 允许发送状态包回 Vision

} VisionData_t;

/**
 * @brief vision_real - 全局实例
 *        所有视觉数据存于此, 电控主逻辑直接读取
 */
extern VisionData_t vision_real;

// ======================== 函数声明 ========================

/* ---------- 初始化 ---------- */
void vision_init(UART_HandleTypeDef *huart);  // 传入串口句柄, 启动接收

/* ---------- 中断回调 (由 HAL_UART_RxCpltCallback 调用) ---------- */
void vision_uart_rx_callback(uint8_t byte);   // 每收到1字节调用一次

/* ---------- 主任务 (主循环或RTOS任务中周期调用) ---------- */
void vision_task(void);                       // 解析帧 + 执行控制逻辑

// [状态包] 可选，暂时不用
// void vision_send_status(uint8_t state, uint8_t flags,
//                         uint8_t bin, uint8_t error);

/* ---------- CRC16 ---------- */
uint16_t vision_crc16(const uint8_t *data, uint32_t len);
uint8_t  vision_crc_verify(const uint8_t *frame, uint32_t frame_len);

/* ---------- 工具 ---------- */
const char* vision_bean_name(uint8_t type);   // 豆子类型→名字
uint8_t     vision_bean_to_bin(uint8_t type); // 豆子类型→箱号

/* ---------- 用户控制接口 (用户实现) ---------- */

/**
 * @brief 收到豆子检测包 (0xAA) 后的回调
 *        用户在此函数中实现: 移动到豆子位置 → 夹取 → 移动到料箱 → 放置
 * @param data  vision_real 指针, 读取 bean_types/target_bins
 */
void vision_on_beans_received(VisionData_t *data);

/**
 * @brief 收到数字标签包 (0xCC) 后的回调
 *        用户在此函数中确认料箱位置信息
 * @param data  vision_real 指针, 读取 digits
 */
void vision_on_digits_received(VisionData_t *data);

#endif /* __VISION_H */
