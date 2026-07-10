#include "Communication/VisionProtocol.h"

VisionTxPacket buildFieldStatePacket(const FieldState &state)
{
    // VisionTxPacket 里只放 payload。
    // 帧头 0xA6 和 CRC16 不在这里加，交给 VirtualSerial::sendPacket 统一处理。
    VisionTxPacket packet;

    // payload 格式：
    // byte0: valid，1 表示 3 个豆子位置和 5 个箱子位置都识别完成
    // byte1: bean_place_1，0未知，1黄豆，2绿豆，3白芸豆
    // byte2: bean_place_2
    // byte3: bean_place_3
    // byte4: box_place_a，0未知，1~5表示识别到的数字
    // byte5: box_place_b
    // byte6: box_place_c
    // byte7: box_place_d
    // byte8: box_place_e
    packet.payload.reserve(9);

    // byte0：整场结果是否有效。
    // valid=1 表示豆子和箱子都已经识别完成；
    // valid=0 表示当前发送的是不完整结果，电控一般不应执行。
    packet.payload.push_back(state.valid() ? 1 : 0);

    // byte1~byte3：三个豆子位置。
    for (BeanType bean : state.beanPlaces)
    {
        packet.payload.push_back(encodeBeanType(bean));
    }

    // byte4~byte8：五个箱子位置。
    for (int digit : state.boxPlaces)
    {
        if (digit >= 1 && digit <= 5)
        {
            packet.payload.push_back(static_cast<uint8_t>(digit));
        }
        else
        {
            packet.payload.push_back(0);
        }
    }

    return packet;
}
