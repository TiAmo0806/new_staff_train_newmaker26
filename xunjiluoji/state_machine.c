/**
 * @file    state_machine.c
 * @brief   双周期五箱 — 主辅对调 + 双白横条
 *
 * 第一周期(c0): T→RIGHT, 豆子1→后退(||), 下断点→左扫, 第4小断点换向,
 *               双白(1)=ignore, 双白(2)=T→RIGHT→箱1, 上断点v1→第二周期
 * 第二周期(c1): T→LEFT,  豆子3→后退(||), 下断点→左扫, 同上,
 *               上断点v2→结束
 *
 * 排序规则:
 *   上断点 + 下断点: 缺失箱号都放首位 (两侧都是先遇到不可见箱)
 *   上断点可见箱: 原序(左→右)
 *   下断点可见箱: 反转 (小车从右到左)
 *
 * 小断点映射:
 *   第4个小断点处执行主辅对调 (SwapSensors) 实现行驶方向换向
 */

#include "state_machine.h"
#include "communication.h"
#include "main.h"

/*=================================================================
 * 全局状态
 *=================================================================*/
static SystemState_t     g_s      = STATE_INIT;
static SystemState_t     g_last   = STATE_INIT;
static uint32_t          g_tick   = 0;
static uint32_t          g_entry  = 0;
static uint32_t          g_act    = 0;

/* 周期/计数 */
static uint8_t           g_cycle;            /* 0=第一周期 1=第二周期 */
static uint8_t           g_upper_visits;     /* 上断点到访次数 */
static uint8_t           g_white_double_cnt; /* 当前段双白横条检测次数 */
static uint8_t           g_moving_backward;  /* 1=后退模式 */
static uint32_t          g_small_blind_end;  /* 小断点盲区截止时间 (ms) */
static uint32_t          g_bean_blind_end;   /* ||后倒车盲区 (防再次触发||) */

/* 箱子/豆子 */
static RecognizedDigit_t g_box[NUM_TOTAL_BOXES];
static RecognizedDigit_t g_bean_label;
static uint8_t           g_stop_idx;         /* 当前小断点序号 0~4 */
static uint8_t           g_retry;

static uint32_t Ms(void) { return HAL_GetTick(); }

/*=================================================================
 * 状态名
 *=================================================================*/
const char* StateMachine_GetStateName(SystemState_t s)
{
    switch (s) {
        case STATE_INIT:              return "INIT";
        case STATE_LINE_FOLLOW_FORWARD: return "LF";
        case STATE_UPPER_CHECKPOINT:  return "UP";
        case STATE_T_TRANSLATE:       return "T";
        case STATE_SMALL_TRANSLATE:   return "SMALL";
        case STATE_LOWER_CHECKPOINT:  return "LO";
        case STATE_BEAN_DETECT:       return "B_DET";
        case STATE_BEAN_RECOG:        return "B_REC";
        case STATE_BEAN_DONE:         return "B_OK";
        case STATE_EXECUTE_LEFT:      return "EXL";
        case STATE_EXECUTE_RIGHT:     return "EXR";
        case STATE_EXECUTE_FORWARD:   return "EXF";
        case STATE_ARRIVED:           return "DONE";
        case STATE_ERROR:             return "ERR";
        default:                      return "?";
    }
}

static void Go(SystemState_t ns)
{
    g_last = g_s;
    g_s    = ns;
    g_tick = 0;
    g_entry = Ms();
    printf("\r\n[SM] %s→%s (c%d bw=%d)\r\n\r\n",
           StateMachine_GetStateName(g_last),
           StateMachine_GetStateName(g_s),
           g_cycle, g_moving_backward);
}

/*=================================================================
 * 视觉帧等待
 *=================================================================*/
static int8_t WaitSingle(RecognizedDigit_t* d)
{
    DecodedFrame_t f;
    while (Comm_DecodeNext(&f)) {
        if (f.is_vision && f.vision.detected) {
            *d = f.vision.digit;
            Comm_SendAck(f.vision.frame_id);
            return 1;
        }
    }
    if (Ms() - g_entry >= VISION_RESPONSE_TIMEOUT_MS) {
        g_retry++;
        if (g_retry >= VISION_MAX_RETRIES) return -1;
        Comm_TriggerRecognize();
        g_entry = Ms();
    }
    return 0;
}

static int8_t WaitMultiBox(DecodedFrame_t* f)
{
    while (Comm_DecodeNext(f)) {
        if (f->is_multibox) { Comm_SendAck(0); return 1; }
        if (f->is_vision && f->vision.detected) {
            Comm_SendAck(f->vision.frame_id);
            memset(f->box_labels, 0, sizeof(f->box_labels));
            f->box_labels[0] = f->vision.digit;
            f->missing_index = 0;
            f->is_multibox = 1;
            return 1;
        }
    }
    if (Ms() - g_entry >= VISION_RESPONSE_TIMEOUT_MS) {
        g_retry++;
        if (g_retry >= VISION_MAX_RETRIES) return -1;
        Comm_TriggerRecognize();
        g_entry = Ms();
    }
    return 0;
}

/*=================================================================
 * 箱子重排序
 *
 *   upper=1 (上断点): missing_label 放首位, 可见箱保持原序(左→右)
 *     g_box = [missing, vis[0], vis[1], vis[2], vis[3]]
 *
 *   upper=0 (下断点): missing_label 放首位, 可见箱反转
 *     (倒车时第一个遇到侧面看不到的箱=missing)
 *     g_box = [missing, vis[3], vis[2], vis[1], vis[0]]
 *=================================================================*/
static void ReorderBoxes(const DecodedFrame_t* f, uint8_t upper)
{
    RecognizedDigit_t ml  = f->missing_label;
    RecognizedDigit_t vis[NUM_VISIBLE_BOXES];
    memcpy(vis, f->box_labels, sizeof(vis));

    if (upper) {
        /* 上断点: 缺失放首位 */
        g_box[0] = ml;
        g_box[1] = vis[0];
        g_box[2] = vis[1];
        g_box[3] = vis[2];
        g_box[4] = vis[3];
    } else {
        /* 下断点: 缺失放首位, 可见箱反转 (倒车先遇侧箱) */
        g_box[0] = ml;
        g_box[1] = vis[3];
        g_box[2] = vis[2];
        g_box[3] = vis[1];
        g_box[4] = vis[0];
    }

    printf("[SM] Reorder %s: [%d %d %d %d %d] (miss=#%d lbl=%d)\r\n",
           upper ? "UP" : "LO",
           (int)g_box[0], (int)g_box[1], (int)g_box[2],
           (int)g_box[3], (int)g_box[4],
           (int)f->missing_index, (int)ml);
}

/*=================================================================
 * 舵机 — 抓豆/放豆
 *
 * TODO: 替换为实际 GPIO 控制
 *   SERVO_GPIO_PORT / SERVO_GPIO_PIN (config.h)
 *   典型: PWM 控制舵机角度
 *=================================================================*/
static void Servo_GrabBean(void)
{
    printf("[SERVO] ★★★ GRAB BEAN ★★★\r\n");
    /*
     * TODO: 实际舵机控制代码
     *
     * // 设置 PWM 占空比 → 舵机抓取
     * __HAL_TIM_SET_COMPARE(&htimX, TIM_CHANNEL_Y, SERVO_GRAB_PULSE);
     * HAL_Delay(SERVO_GRAB_MS);
     */
}

static void Servo_DropBean(void)
{
    printf("[SERVO] ▼▼▼ DROP BEAN ▼▼▼\r\n");
    /*
     * TODO: 实际舵机释放代码
     *
     * // 设置 PWM 占空比 → 舵机释放
     * __HAL_TIM_SET_COMPARE(&htimX, TIM_CHANNEL_Y, SERVO_DROP_PULSE);
     * HAL_Delay(SERVO_GRAB_MS);
     */
}

/*=================================================================
 * 放豆 (高层封装)
 *=================================================================*/
static void DropBean(void)
{
    printf("[SM] ╔══════════════════╗\r\n");
    printf("[SM] ║  ★★★ DROP ★★★  ║\r\n");
    printf("[SM] ╚══════════════════╝\r\n");
    Servo_DropBean();
}

/*=================================================================
 * 初始化
 *=================================================================*/
void StateMachine_Init(void)
{
    g_s      = STATE_INIT;
    g_last   = STATE_INIT;
    g_tick   = 0;
    g_entry  = Ms();
    g_cycle  = 0;
    g_upper_visits    = 0;
    g_white_double_cnt = 0;
    g_moving_backward  = 0;
    g_small_blind_end  = 0;
    g_bean_blind_end   = 0;
    g_bean_label = DIGIT_NONE;
    g_retry      = 0;
    g_stop_idx   = 0;
    memset(g_box, 0, sizeof(g_box));

    Sensor_Init();
    Motor_Init();
    LineFollow_Init();
    Comm_Init();

    printf("\r\n[SM] === Dual-Cycle 5-Box v2 ===\r\n");
    printf("[SM] C0: T→R Bean||→BW LO→L_SWP "
           "4th→SWAP White||#1=ignore UpperV1→C1\r\n");
    printf("[SM] C1: T→L Bean||→BW LO→L_SWP "
           "4th→SWAP White||#1=ignore UpperV2→END\r\n");
    printf("[SM] UP sort: miss→[0]  LO sort: miss→[0] + vis_rev\r\n\r\n");
}

/*=====================================================================
 * 主循环
 *=====================================================================*/
void StateMachine_Run(void)
{
    g_tick++;

    switch (g_s) {

    /*==========================================================*/
    case STATE_INIT:
        Motor_StopAll();
        if (Ms() - g_entry >= 3000) Go(STATE_LINE_FOLLOW_FORWARD);
        break;

    /*==============================================================
     * 主循迹状态 — 所有路面标记在此分发
     *
     * 进入时根据来源状态决定行驶方向:
     *   BEAN_DONE → ResumeBackward (||后反向)
     *   4th SmallStop → 方向已在 SMALL_TRANSLATE 内由 SwapSensors 翻转
     *   其他 → 沿用 g_moving_backward 或 SetForward
     *==============================================================*/
    case STATE_LINE_FOLLOW_FORWARD: {
        if (g_tick == 1) {
            if (g_last == STATE_BEAN_DONE) {
                /* ||抓豆完成, SwapSensors已切到REAR, 反向行驶 */
                LineFollow_ResumeBackward();
                g_moving_backward = 1;
                g_white_double_cnt = 0;  /* 新段重置双白计数 */
            } else if (g_last == STATE_INIT) {
                /* 首次启动: 前进 + FRONT */
                LineFollow_SetForward();
                g_moving_backward = 0;
                g_white_double_cnt = 0;
            } else {
                /* 其他 (T_TRANSLATE, SMALL_TRANSLATE, LOWER, UPPER):
                   保持 g_moving_backward 方向不变 */
                if (g_moving_backward) {
                    LineFollow_ResumeBackward();
                } else {
                    LineFollow_ResumeForward();
                }
            }
        }

        RoadFeature_t f;
        LineFollow_Update(&f);

        switch (f) {

        case ROAD_BLACK_DOUBLE:
            /* ||盲区内忽略 (倒车时避免同一||再触发) */
            if (Ms() < g_bean_blind_end) {
                printf("[SM] BEAN|| BLIND (ignored)\r\n");
                break;
            }
            /* 豆子箱|| — 停车, 进入识别流程 */
            LineFollow_Stop();
            printf("[SM] ★ BEAN|| (c%d)\r\n", g_cycle);
            Go(STATE_BEAN_DETECT);
            break;

        case ROAD_REFL_LARGE:
            /* 下断点 — 停车, 左扫识别 4+1 箱 */
            LineFollow_Stop();
            printf("[SM] ★ LOWER (c%d)\r\n", g_cycle);
            Go(STATE_LOWER_CHECKPOINT);
            break;

        case ROAD_UPPER_CHECK:
            /* 上断点 — 根据到访次数处理 */
            LineFollow_Stop();
            printf("[SM] ★ UPPER v%d (c%d)\r\n", g_upper_visits + 1, g_cycle);
            Go(STATE_UPPER_CHECKPOINT);
            break;

        case ROAD_T_JUNCTION: {
            /* T形口 — 平移方向由周期决定 */
            uint8_t d = (g_cycle == 0) ? DIR_RIGHT : DIR_LEFT;
            printf("[SM] T→%s (c%d)\r\n",
                   d == DIR_RIGHT ? "R" : "L", g_cycle);
            LineFollow_Stop();
            Motor_Translate(TRANSLATE_SPEED_RPM, (MoveDirection_t)d);
            g_act = Ms();
            Go(STATE_T_TRANSLATE);
            break;
        }

        case ROAD_REFL_SINGLE:
            /* 小断点 — 盲区内忽略 (防同一反光条重复触发) */
            if (Ms() < g_small_blind_end) {
                printf("[SM] SMALL#%d BLIND (ignored)\r\n", g_stop_idx);
                break;
            }
            printf("[SM] ★ SMALL#%d (c%d)\r\n", g_stop_idx, g_cycle);
            Go(STATE_SMALL_TRANSLATE);
            break;

        case ROAD_WHITE_DOUBLE:
            /* 双白横条 — 第一次忽略, 第二次当T形口处理 */
            g_white_double_cnt++;
            printf("[SM] ★ WHITE|| #%d (c%d)\r\n",
                   g_white_double_cnt, g_cycle);
            if (g_white_double_cnt == 1) {
                /* 第一次: 忽略, 继续循迹 */
                printf("[SM]   → IGNORE (1st)\r\n");
            } else {
                /* 第二次: 按T形口处理 */
                uint8_t d = (g_cycle == 0) ? DIR_RIGHT : DIR_LEFT;
                printf("[SM]   → T %s (2nd)\r\n",
                       d == DIR_RIGHT ? "R" : "L");
                LineFollow_Stop();
                Motor_Translate(TRANSLATE_SPEED_RPM, (MoveDirection_t)d);
                g_act = Ms();
                Go(STATE_T_TRANSLATE);
            }
            break;

        case ROAD_LOST_LINE:
            LineFollow_Stop();
            Go(STATE_ERROR);
            break;

        default:
            break;
        }
        break;
    }

    /*==========================================================*/
    case STATE_T_TRANSLATE:
        /* T形口平移 — 持续指定时间后回主路 */
        if (Ms() - g_act >= TRANSLATE_DURATION_MS) {
            Motor_StopAll();
            printf("[SM] T translate done\r\n");
            Go(STATE_LINE_FOLLOW_FORWARD);
        }
        break;

    /*==============================================================
     * 小断点 — 逐箱匹配放豆
     *
     * 流程:
     *   停车 → 检查当前箱标签 → 匹配则平移→放豆→平移回
     *   → 第4个断点(g_stop_idx==3完成后)执行 SwapSensors + 换向
     *   → 5个箱全走完回主路
     *
     * 周期方向:
     *   周期0: 向右平移找箱
     *   周期1: 向左平移找箱
     *==============================================================*/
    case STATE_SMALL_TRANSLATE: {
        uint8_t dir  = (g_cycle == 0) ? DIR_RIGHT : DIR_LEFT;
        uint8_t rdir = (g_cycle == 0) ? DIR_LEFT  : DIR_RIGHT;
        uint8_t matched = (g_bean_label != DIGIT_NONE
                           && g_box[g_stop_idx] == g_bean_label);

        /*
         * 阶段0 (t=1): 判断匹配
         *   匹配: 停车 → 平移放豆
         *   不匹配: 不停车, 直接跳过 (车继续前进/后退中)
         */
        if (g_tick == 1) {
            printf("[SM] SMALL#%d lbl=%d %s bean=%d\r\n",
                   g_stop_idx, (int)g_box[g_stop_idx],
                   matched ? "==" : "!=", (int)g_bean_label);
            if (!matched) {
                printf("[SM]   → skip (keep moving)\r\n");
                g_stop_idx++;
                goto small_stop_done;
            }
            /* 匹配: 停车, 短暂停顿后平移 */
            LineFollow_Stop();
            g_act = 0;
        }
        if (!matched) break;

        /*
         * 阶段1 (t=6, ~30ms): 开始向箱子平移
         */
        if (g_tick == 6) {
            printf("[SM]   → translate %s to box\r\n",
                   dir == DIR_RIGHT ? "R" : "L");
            Motor_Translate(TRANSLATE_SPEED_RPM, (MoveDirection_t)dir);
            g_act = Ms();  /* 阶段1→2的时间戳 */
        }

        /*
         * 阶段2: 到达箱子 → 放豆 → 反向平移回主路
         */
        if (g_act && g_act < 0x80000000UL
            && Ms() - g_act >= SMALL_STOP_TRANSLATE_MS) {
            DropBean();
            printf("[SM]   ← translate %s back\r\n",
                   rdir == DIR_RIGHT ? "R" : "L");
            Motor_Translate(TRANSLATE_SPEED_RPM, (MoveDirection_t)rdir);
            g_act = 0x80000000UL | Ms();  /* 高位标记: 阶段2→3 */
        }

        /*
         * 阶段3: 回到主路 → 下一断点
         */
        if (g_act >= 0x80000000UL
            && Ms() - (g_act & 0x7FFFFFFFUL) >= SMALL_STOP_TRANSLATE_MS) {
            printf("[SM]   back on main path\r\n");
            Motor_StopAll();
            g_stop_idx++;
            goto small_stop_done;
        }

        /*
         * 超时保护 (>5s 强制跳转)
         */
        if (Ms() - g_entry > 5000) {
            Motor_StopAll();
            printf("[SM] SMALL timeout → force next\r\n");
            g_stop_idx++;
            goto small_stop_done;
        }
        break;

small_stop_done:
        /*
         * 第4个断点 (g_stop_idx==4, 即已完成0,1,2,3共4个):
         *   执行主辅对调 + 换向
         */
        if (g_stop_idx == 4) {
            LineFollow_SwapSensors();
            g_moving_backward = !g_moving_backward;
            printf("[SM] ★ 4th stop done → SWAPPED! now %s ★\r\n",
                   g_moving_backward ? "BACKWARD" : "FORWARD");
        }

        if (g_stop_idx >= NUM_TOTAL_BOXES) {
            printf("[SM] All 5 stops done → resume\r\n");
        }

        /* 设盲区: 防止同一反光条被重复检测 */
        g_small_blind_end = Ms() + SMALL_BLIND_TICKS * SYSTICK_PERIOD_MS;

        Go(STATE_LINE_FOLLOW_FORWARD);
        break;
    }

    /*==============================================================
     * 豆子箱|| — 低头识别 → 抓取 → 主辅对调 → 反向行驶
     *==============================================================*/
    case STATE_BEAN_DETECT:
        if (g_tick == 1) {
            Comm_FlushRx();
            Comm_TriggerGimbal(GACT_HEAD_DOWN);
            Comm_TriggerRecognize();
            g_retry = 0;
            printf("[SM] Bean: head down + recognize...\r\n");
        }
        if (Ms() - g_entry >= 1000) Go(STATE_BEAN_RECOG);
        break;

    case STATE_BEAN_RECOG: {
        RecognizedDigit_t d;
        int8_t r = WaitSingle(&d);
        if (r == 1) {
            g_bean_label = d;
            printf("[SM] Bean = %d\r\n", (int)d);
            Go(STATE_BEAN_DONE);
        } else if (r == -1) {
            g_bean_label = DIGIT_NONE;
            printf("[SM] Bean fail → continue\r\n");
            Go(STATE_BEAN_DONE);
        }
        break;
    }

    case STATE_BEAN_DONE:
        if (g_tick == 1) {
            /* 舵机抓取豆子 */
            Servo_GrabBean();
            /* 云台回正+180扭头 */
            Comm_TriggerGimbal(GACT_RETURN_180);
            /* 主辅传感器对调 → 后传感器领路 */
            LineFollow_SwapSensors();
            printf("[SM] Done: grab+return180+swapped\r\n");
        }
        if (Ms() - g_entry >= 2000) {
            /* 设盲区: 倒车时避免再次触发同一个|| (后传感器会重新穿过) */
            g_bean_blind_end = Ms() + 4000;
            Go(STATE_LINE_FOLLOW_FORWARD);
        }
        break;

    /*==============================================================
     * 下断点 — 左扫拍照 → 收多箱帧 → 排序(missing放首位)
     *==============================================================*/
    case STATE_LOWER_CHECKPOINT:
        if (g_tick == 1) {
            Comm_FlushRx();
            Comm_TriggerGimbal(GACT_LEFT_SWEEP);
            Comm_TriggerRecognize();
            g_retry = 0;
            printf("[SM] Lower → LEFT SWEEP + photo\r\n");
        }
        {
            DecodedFrame_t mf;
            int8_t r = WaitMultiBox(&mf);
            if (r == 1) {
                ReorderBoxes(&mf, 0);  /* upper=0: missing放首位 + vis反转 */
                g_stop_idx = 0;
                g_white_double_cnt = 0;  /* 新段重置双白计数 */
                printf("[SM] Lower done → small stops ahead\r\n");
                Go(STATE_LINE_FOLLOW_FORWARD);
            } else if (r == -1) {
                printf("[SM] Lower fail → continue\r\n");
                Go(STATE_LINE_FOLLOW_FORWARD);
            }
        }
        break;

    /*==============================================================
     * 上断点
     *
     *   v1 (cycle0→cycle1): 收箱 → 排序(missing放首位) → T_LEFT平移
     *   v2 (cycle1结束):    收箱 → 排序(missing放首位) → RIGHT SWEEP → END
     *==============================================================*/
    case STATE_UPPER_CHECKPOINT:
        if (g_tick == 1) {
            g_upper_visits++;
            Comm_FlushRx();
            g_retry = 0;

            if (g_upper_visits == 2) {
                /* v2: 右转识别 → 放豆 → 结束 */
                Comm_TriggerGimbal(GACT_RIGHT_15);
                Comm_TriggerRecognize();
                printf("[SM] Upper v2 → RIGHT SWEEP + photo\r\n");
            } else {
                /* v1: 收箱排序 → 启动第二周期 */
                Comm_TriggerGimbal(GACT_RIGHT_15);
                Comm_TriggerRecognize();
                printf("[SM] Upper v1 → RIGHT + start CYCLE 2\r\n");
            }
        }

        {
            DecodedFrame_t mf;
            int8_t r = WaitMultiBox(&mf);
            if (r == 1) {
                ReorderBoxes(&mf, 1);  /* upper=1: missing放首位 */

                if (g_upper_visits == 1) {
                    /* v1: 启动第二周期 → T左平移去豆子3|| */
                    g_cycle = 1;
                    g_stop_idx = 0;
                    g_white_double_cnt = 0;
                    printf("[SM] → Cycle 2! T→LEFT to bean3||\r\n");
                    Motor_Translate(TRANSLATE_SPEED_RPM, DIR_LEFT);
                    g_act = Ms();
                    Go(STATE_T_TRANSLATE);
                } else {
                    /* v2: 最后一轮放豆 → 结束 */
                    printf("[SM] v2 → ALL DONE!\r\n");
                    Go(STATE_ARRIVED);
                }
            } else if (r == -1) {
                if (g_upper_visits == 1) {
                    g_cycle = 1;
                    g_stop_idx = 0;
                    g_white_double_cnt = 0;
                    Motor_Translate(TRANSLATE_SPEED_RPM, DIR_LEFT);
                    g_act = Ms();
                    Go(STATE_T_TRANSLATE);
                } else {
                    Go(STATE_ARRIVED);
                }
            }
        }
        break;

    /*==== 简单执行状态 ====*/
    case STATE_EXECUTE_LEFT:
        if (g_tick == 1) g_act = Ms();
        Motor_Translate(TRANSLATE_SPEED_RPM, DIR_LEFT);
        if (Ms() - g_act >= TRANSLATE_DURATION_MS) {
            Motor_StopAll();
            Go(STATE_ARRIVED);
        }
        break;

    case STATE_EXECUTE_RIGHT:
        if (g_tick == 1) g_act = Ms();
        Motor_Translate(TRANSLATE_SPEED_RPM, DIR_RIGHT);
        if (Ms() - g_act >= TRANSLATE_DURATION_MS) {
            Motor_StopAll();
            Go(STATE_ARRIVED);
        }
        break;

    case STATE_EXECUTE_FORWARD:
        if (g_tick == 1) { g_act = Ms(); LineFollow_SetForward(); }
        LineFollow_Update(NULL);
        if (Ms() - g_act >= FORWARD_AFTER_ACTION_MS) {
            LineFollow_Stop();
            Go(STATE_ARRIVED);
        }
        break;

    /*==== 终点 / 错误 ====*/
    case STATE_ARRIVED:
        if (g_tick == 1) {
            Motor_StopAll();
            printf("\r\n[SM] ★★★ ALL DONE (c%d) ★★★\r\n\r\n", g_cycle);
        }
        break;

    case STATE_ERROR:
        if (g_tick == 1) {
            Motor_StopAll();
            printf("[SM] ★ ERROR ★\r\n");
        }
        break;

    default:
        Go(STATE_ERROR);
        break;
    }
}

SystemState_t StateMachine_GetState(void) { return g_s; }
