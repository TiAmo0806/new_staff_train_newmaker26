#ifndef VISION_PROTOCOL_H
#define VISION_PROTOCOL_H

#include "Communication/VirtualSerial.h"
#include "ImgProcessing/FieldState.h"

// 把完整场地状态打包成串口 payload。
// 这个函数只负责“把识别结果变成字节内容”。
// VirtualSerial 会继续负责添加帧头 0xA6 和 CRC16。
//
// 最终发送帧格式：
//   A6 valid bean1 bean2 bean3 boxA boxB boxC boxD boxE CRC_L CRC_H
VisionTxPacket buildFieldStatePacket(const FieldState &state);

#endif // VISION_PROTOCOL_H
