/**
 * @file    vision.h
 * @brief   抓豆分拣 C板 - 视觉通信协议 (USB CDC)
 * @note    与 VisionNode (NUC) 通过 USB CDC (Virtual COM Port) 通信
 *          协议: 虚拟串口通信协议6.0
 *          硬件: STM32F407IGHX  Keil5  HAL库
 *
 * 集成说明:
 *   1. USB 初始化后在 main 中调用 vision_init()
 *   2. 在 usbd_cdc_if.c 的 CDC_Receive_FS() 中调用 vision_cdc_rx_callback()
 *   3. 主循环中周期调用 vision_task()
 *   4. 读取 vision_real 获取最新视觉数据
 *   5. 在 vision_on_beans_received / vision_on_digits_received
 *      中写自己的电机/夹爪控制逻辑
 */

#ifndef __VISION_H
#define __VISION_H

#include "stm32f4xx_hal.h"
#include <string.h>

// ======================== 协议定义 ========================

/* 帧头 */
#define VISION_HEADER_DETECTION     0xAA  // 豆子检测包
#define VISION_HEADER_NUMBER        0xCC  // 数字标签包

/* 包大小 */
#define VISION_DETECTION_SIZE       9     // [0xAA] [type1] [bin1] [type2] [bin2] [type3] [bin3] [crcL] [crcH]
#define VISION_NUMBER_SIZE          8     // [0xCC] [d1] [d2] [d3] [d4] [d5] [crcL] [crcH]

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

/* 接收缓冲区 */
#define VISION_RX_BUF_SIZE  256

// ======================== 核心数据结构 ========================

/**
 * @brief vision_real - 视觉数据全局结构体
 *        电控主逻辑读取此结构体获取视觉信息并做出反应
 */
typedef struct
{
    /* -------- CDC 接收层 -------- */
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

} VisionData_t;

/**
 * @brief vision_real - 全局实例
 *        所有视觉数据存于此, 电控主逻辑直接读取
 */
extern VisionData_t vision_real;

// ======================== 函数声明 ========================

void vision_init(void);
void vision_task(void);
void vision_cdc_rx_callback(uint8_t *buf, uint32_t len);

uint16_t vision_crc16(const uint8_t *data, uint32_t len);
uint8_t  vision_crc_verify(const uint8_t *frame, uint32_t frame_len);

const char* vision_bean_name(uint8_t type);
uint8_t     vision_bean_to_bin(uint8_t type);

/* 用户实现: 收到视觉数据后的控制逻辑 */
void vision_on_beans_received(VisionData_t *data);
void vision_on_digits_received(VisionData_t *data);

#endif /* __VISION_H */
