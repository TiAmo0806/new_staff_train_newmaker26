#pragma once

#include "core/VisionResult.h"

/**
 * @brief 统一的一次识别执行接口。
 *
 * RecognitionRunner 只负责“如何完成一次豆子/数字识别”，
 * 不负责状态流转、任务生成、协议打包或串口发送。
 */
class RecognitionRunner {
public:
    virtual ~RecognitionRunner() = default;

    /**
     * @brief 执行一次豆子区识别。
     * @param result 输出参数，成功时写入识别结果。
     * @return 成功完成一次识别返回 true，失败返回 false。
     */
    virtual bool scanBeans(VisionResult& result) = 0;

    /**
     * @brief 执行一次数字区识别。
     * @param result 输出参数，成功时写入识别结果。
     * @return 成功完成一次识别返回 true，失败返回 false。
     */
    virtual bool scanDigits(VisionResult& result) = 0;

    /**
     * @brief 重置识别执行器的内部读取状态。
     */
    virtual void reset() = 0;
};
