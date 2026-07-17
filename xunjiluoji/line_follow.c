/**
 * @file    line_follow.c
 * @brief   8 路巡线模块 ×2 — PID + 路面标记识别
 *
 * 传感器: 前(USART2) + 后(USART3) 各一个 8 路巡线模块
 * PID 输入: 活跃传感器的线位置 (0.0~7.0)
 * 误差 = (线位置 − 3.5) × LINE_POS_KP
 * 标记识别: 基于活跃传感器的 black_count / refl_count
 */

#include "line_follow.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

static LineFollower_t g_lf;

/*=================================================================
 * 黑通道 — 检测大面积黑 / T形 / 双黑脉冲 (||)
 *
 * 基于活跃传感器的 black_count
 * 状态机: IDLE(0) → IN_BLACK(1) → JUST_LEFT(2) → IDLE
 *
 * 单脉冲: ≥4路黑进入→ <4路黑离开→窗口超时→评估
 * 双脉冲: 离开→再进入→再离开→窗口超时→评估 (||豆子箱)
 *=================================================================*/
#define BK_ENTER_THRESHOLD    4           /* ≥4 路黑 → 进入黑脉冲 */
#define BK_LARGE_THRESHOLD    6           /* ≥6 路黑 → 大面积 (上断点) */
#define BK_PULSE_MAX          4

static uint8_t  g_bk_state;              /* 0=IDLE 1=IN_BLACK 2=JUST_LEFT */
static uint32_t g_bk_enter;              /* 进入黑区时刻 (ms) */
static uint32_t g_bk_leave;              /* 离开黑区时刻 (ms) */

static uint32_t g_bk_pulse_times[BK_PULSE_MAX];
static uint8_t  g_bk_pulse_cnt;

/*=================================================================
 * 反光通道 — 检测小断点 / 下断点 / 双白横条(||)
 *
 * 基于活跃传感器的 refl_count
 * 脉冲状态机 (和黑通道平行):
 *   IDLE(0) → IN_REFL(1) → JUST_LEFT(2) → 窗口超时→评估→IDLE
 *=================================================================*/
#define RF_ENTER_THRESHOLD    1           /* ≥1 路反光 → 进入反光脉冲 */
#define RF_LARGE_THRESHOLD    4           /* ≥4 路反光 → 大面积 (下断点) */
#define RF_PULSE_MAX          4

static uint8_t  g_rf_state;
static uint32_t g_rf_enter;
static uint32_t g_rf_leave;

static uint32_t g_rf_pulse_times[RF_PULSE_MAX];
static uint8_t  g_rf_pulse_cnt;

/*=================================================================
 * 脱线
 *=================================================================*/
static uint32_t g_lost_tick;

/*=================================================================
 * 内部: 获取活跃传感器的数据
 *=================================================================*/
static const SingleSensor_t* active_sensor_ptr(void)
{
    return (g_lf.active_sensor == ACTIVE_FRONT)
        ? Sensor_GetFront() : Sensor_GetRear();
}

/*=================================================================
 * 内部: 重置全部状态机
 *=================================================================*/
static void reset_all(void)
{
    g_bk_state = 0;   g_bk_enter = 0;  g_bk_leave = 0;
    memset(g_bk_pulse_times, 0, sizeof(g_bk_pulse_times));
    g_bk_pulse_cnt = 0;
    g_rf_state = 0;   g_rf_enter = 0;  g_rf_leave = 0;
    memset(g_rf_pulse_times, 0, sizeof(g_rf_pulse_times));
    g_rf_pulse_cnt = 0;
    g_lost_tick = 0;
}

/*=================================================================
 * 初始化
 *=================================================================*/
void LineFollow_Init(void)
{
    memset(&g_lf, 0, sizeof(g_lf));
    g_lf.direction     = DIR_FORWARD;
    g_lf.active_sensor = ACTIVE_FRONT;
    g_lf.base_speed    = BASE_SPEED_FORWARD_RPM;
    g_lf.pid.kp        = KP_FORWARD;
    g_lf.pid.kd        = KD_FORWARD;
    g_lf.is_active     = 0;
    reset_all();

#if DEBUG_ENABLE
    printf("[LF] 8ch×2 modules. Active=%s\r\n",
           g_lf.active_sensor == ACTIVE_FRONT ? "FRONT" : "REAR");
    printf("[LF]   Kp=%.2f Kd=%.2f PosTarget=%.1f PosKp=%.1f\r\n",
           (double)KP_FORWARD, (double)KD_FORWARD,
           LINE_POS_TARGET, LINE_POS_KP);
    printf("[LF]   BK: enter>=%d large>=%d | RF: enter>=%d large>=%d\r\n",
           BK_ENTER_THRESHOLD, BK_LARGE_THRESHOLD,
           RF_ENTER_THRESHOLD, RF_LARGE_THRESHOLD);
#endif
}

/*=================================================================
 * PID — 8 路线位置 → 修正量
 *
 * 线位置 0(左)~7(右), 目标 3.5(中心)
 * 误差 = (线位置 − 目标) × Kp_scaler
 *=================================================================*/
static int16_t PID_Compute(float line_pos)
{
    float err   = (line_pos - LINE_POS_TARGET) * LINE_POS_KP;
    float d_err = err - g_lf.pid.last_error;
    float corr  = g_lf.pid.kp * err + g_lf.pid.kd * d_err;

    g_lf.pid.last_error = err;

    if (corr > (float)MAX_CORRECTION_RPM) corr = (float)MAX_CORRECTION_RPM;
    if (corr < -(float)MAX_CORRECTION_RPM) corr = -(float)MAX_CORRECTION_RPM;

    g_lf.pid.correction = (int16_t)corr;
    return g_lf.pid.correction;
}

/*=================================================================
 * 主循环 — 循迹更新 + 标记检测
 *=================================================================*/
int32_t LineFollow_Update(RoadFeature_t* feature_out)
{
    if (feature_out) *feature_out = ROAD_NORMAL;
    if (!g_lf.is_active) return 0;

    /* 1. 更新传感器 */
    Sensor_Update();
    const SingleSensor_t* s = active_sensor_ptr();
    float    line_pos = s->line_position;
    uint8_t  bk_cnt   = s->black_count;
    uint8_t  rf_cnt   = s->refl_count;
    uint32_t now      = HAL_GetTick();

    RoadFeature_t fea = ROAD_NORMAL;

    /*=================================================================
     * 通道1: 黑色检测 (脉冲状态机)
     *
     * 进入条件: bk_cnt >= BK_ENTER_THRESHOLD (4路黑)
     * 离开条件: bk_cnt <  BK_ENTER_THRESHOLD
     *=================================================================*/
    switch (g_bk_state) {

    case 0: /* IDLE */
        if (bk_cnt >= BK_ENTER_THRESHOLD) {
            g_bk_state = 1;
            g_bk_enter = now;
        }
        break;

    case 1: /* IN_BLACK */
        if (bk_cnt < BK_ENTER_THRESHOLD) {
            g_bk_state = 2;
            g_bk_leave = now;
            if (g_bk_pulse_cnt < BK_PULSE_MAX) {
                g_bk_pulse_times[g_bk_pulse_cnt++] = now;
            }
        }
        break;

    case 2: /* JUST_LEFT — 等窗口关闭或再次进入 */
        if (bk_cnt >= BK_ENTER_THRESHOLD) {
            g_bk_state = 1;
            g_bk_enter = now;
        } else {
            if (now - g_bk_leave > RF_DOUBLE_WINDOW_MS) {
                goto evaluate_black;
            }
        }
        break;
    }

    goto after_black;

evaluate_black:
    {
        uint32_t dur = (g_bk_leave > g_bk_enter)
            ? (g_bk_leave - g_bk_enter) : 0;

#if DEBUG_ENABLE
        if (g_bk_pulse_cnt >= 2) {
            printf("[LF] BLACK: %d pulses, dur=%lums\r\n",
                   g_bk_pulse_cnt, (unsigned long)dur);
        }
#endif

        /* ── 大面积黑: ≥6路黑 >BK_LARGE_MIN_MS → 上断点 ── */
        if (dur >= BK_LARGE_MIN_MS) {
            fea = ROAD_UPPER_CHECK;
        }
        /* ── T形: 4-5路黑 时间在窗口内 ── */
        else if (dur >= BK_T_JUNCTION_MIN_MS && dur <= BK_T_JUNCTION_MAX_MS) {
            fea = ROAD_T_JUNCTION;
        }

        /* ── 双黑脉冲 (||豆子箱): 2次脉冲在窗口内 ── */
        if (g_bk_pulse_cnt >= 2 && fea == ROAD_NORMAL) {
            if (now - g_bk_pulse_times[0] <= RF_DOUBLE_WINDOW_MS) {
                fea = ROAD_BLACK_DOUBLE;
#if DEBUG_ENABLE
                printf("[LF] ★★ BEAN|| (2 pulses in %lums) ★★\r\n",
                       (unsigned long)(now - g_bk_pulse_times[0]));
#endif
            }
        }

        /* 重置黑通道 */
        g_bk_state = 0;
        g_bk_pulse_cnt = 0;
        memset(g_bk_pulse_times, 0, sizeof(g_bk_pulse_times));
    }

after_black:

    /*=================================================================
     * 通道2: 反光检测 (脉冲状态机)
     *
     * 进入条件: rf_cnt >= RF_ENTER_THRESHOLD (1路反光)
     *=================================================================*/
    switch (g_rf_state) {

    case 0: /* IDLE */
        if (rf_cnt >= RF_ENTER_THRESHOLD) {
            g_rf_state = 1;
            g_rf_enter = now;
        }
        break;

    case 1: /* IN_REFL */
        if (rf_cnt < RF_ENTER_THRESHOLD) {
            g_rf_state = 2;
            g_rf_leave = now;
            if (g_rf_pulse_cnt < RF_PULSE_MAX) {
                g_rf_pulse_times[g_rf_pulse_cnt++] = now;
            }
        }
        break;

    case 2: /* JUST_LEFT — 等窗口关闭或再次进入 */
        if (rf_cnt >= RF_ENTER_THRESHOLD) {
            g_rf_state = 1;
            g_rf_enter = now;
        } else {
            if (now - g_rf_leave > RF_DOUBLE_WINDOW_MS) {
                goto evaluate_reflective;
            }
        }
        break;
    }

    goto after_reflective;

evaluate_reflective:
    {
        uint32_t dur = (g_rf_leave > g_rf_enter)
            ? (g_rf_leave - g_rf_enter) : 0;

#if DEBUG_ENABLE
        if (g_rf_pulse_cnt >= 2) {
            printf("[LF] REFL: %d pulses, dur=%lums\r\n",
                   g_rf_pulse_cnt, (unsigned long)dur);
        }
#endif

        /* ── 双白横条(||): 优先判断 ── */
        if (g_rf_pulse_cnt >= 2) {
            if (now - g_rf_pulse_times[0] <= RF_DOUBLE_WINDOW_MS) {
                fea = ROAD_WHITE_DOUBLE;
#if DEBUG_ENABLE
                printf("[LF] ★★ WHITE|| (2 pulses in %lums) ★★\r\n",
                       (unsigned long)(now - g_rf_pulse_times[0]));
#endif
                goto rf_done;
            }
        }

        /* ── 大面积反光: ≥4路 >RF_LARGE_MIN_MS → 下断点 ── */
        if (dur >= RF_LARGE_MIN_MS) {
            fea = ROAD_REFL_LARGE;
        }
        /* ── 小断点: 1-3路反光 30~400ms ── */
        else if (dur >= RF_SINGLE_MIN_MS && dur <= RF_SINGLE_MAX_MS) {
            if (fea == ROAD_NORMAL) {
                fea = ROAD_REFL_SINGLE;
            }
        }

rf_done:
        /* 重置反光通道 */
        g_rf_state = 0;
        g_rf_pulse_cnt = 0;
        memset(g_rf_pulse_times, 0, sizeof(g_rf_pulse_times));
    }

after_reflective:

    /*=================================================================
     * 脱线检测 — 0 路黑持续超时
     *=================================================================*/
    if (bk_cnt == 0) {
        g_lost_tick++;
        if (g_lost_tick >= (LOST_LINE_TIMEOUT_MS / SYSTICK_PERIOD_MS)) {
            fea = ROAD_LOST_LINE;
            g_lost_tick = 0;
        }
    } else {
        g_lost_tick = 0;
    }

    /*=================================================================
     * Debug 输出
     *=================================================================*/
#if DEBUG_ENABLE
    static uint32_t dbg_cnt = 0;
    if (++dbg_cnt % 40 == 0) {
        printf("[LF] %s pos=%.2f bk=%d rf=%d err=%.1f\r\n",
               g_lf.active_sensor == ACTIVE_FRONT ? "F" : "R",
               (double)line_pos, (int)bk_cnt, (int)rf_cnt,
               (double)g_lf.pid.last_error);
    }
    if (fea != ROAD_NORMAL) {
        printf("[LF] ★ %s (pos=%.2f bk=%d rf=%d active=%s)\r\n",
               LineFollow_FeatureName(fea),
               (double)line_pos, (int)bk_cnt, (int)rf_cnt,
               g_lf.active_sensor == ACTIVE_FRONT ? "F" : "R");
    }
#endif

    if (feature_out) *feature_out = fea;

    /*=================================================================
     * PID 控制 → 电机
     *
     * 修正取反:
     *   活跃传感器 == 预期传感器 → 正常
     *   活跃传感器 != 预期传感器 → 取反
     *   预期传感器: 前进→FRONT, 后退→REAR
     *=================================================================*/
    int16_t corr;
    ActiveSensor_t expected = (g_lf.direction == DIR_FORWARD)
        ? ACTIVE_FRONT : ACTIVE_REAR;

    if (bk_cnt == 0) {
        corr = g_lf.pid.correction;        /* 脱线惯性保持 */
    } else if (bk_cnt >= BK_LARGE_THRESHOLD || rf_cnt >= RF_LARGE_THRESHOLD) {
        corr = 0;                          /* 大面积黑/反光 → 直走 */
    } else {
        corr = PID_Compute(line_pos);
    }

    if (g_lf.active_sensor != expected) {
        corr = -corr;
    }

    Motor_MoveWithCorrection(g_lf.base_speed, corr, g_lf.direction);
    return (int32_t)g_lf.pid.last_error;
}

/*=================================================================
 * 模式设置 (不变)
 *=================================================================*/
void LineFollow_SetForward(void)
{
    g_lf.direction     = DIR_FORWARD;
    g_lf.active_sensor = ACTIVE_FRONT;
    g_lf.base_speed    = BASE_SPEED_FORWARD_RPM;
    g_lf.pid.kp        = KP_FORWARD;
    g_lf.pid.kd        = KD_FORWARD;
    g_lf.pid.last_error = 0.0f;
    g_lf.pid.correction = 0;
    g_lf.is_active      = 1;
    reset_all();
#if DEBUG_ENABLE
    printf("[LF] → FORWARD (active=%s base=%d)\r\n",
           g_lf.active_sensor == ACTIVE_FRONT ? "FRONT" : "REAR",
           g_lf.base_speed);
#endif
}

void LineFollow_SetBackward(void)
{
    g_lf.direction     = DIR_BACKWARD;
    g_lf.active_sensor = ACTIVE_REAR;
    g_lf.base_speed    = BASE_SPEED_BACKWARD_RPM;
    g_lf.pid.kp        = KP_BACKWARD;
    g_lf.pid.kd        = KD_BACKWARD;
    g_lf.pid.last_error = 0.0f;
    g_lf.pid.correction = 0;
    g_lf.is_active      = 1;
    reset_all();
#if DEBUG_ENABLE
    printf("[LF] → BACKWARD (active=%s base=%d)\r\n",
           g_lf.active_sensor == ACTIVE_FRONT ? "FRONT" : "REAR",
           g_lf.base_speed);
#endif
}

void LineFollow_Stop(void)
{
    g_lf.is_active = 0;
    Motor_StopAll();
#if DEBUG_ENABLE
    printf("[LF] Stop.\r\n");
#endif
}

void LineFollow_ResumeForward(void)
{
    g_lf.direction     = DIR_FORWARD;
    g_lf.base_speed    = BASE_SPEED_FORWARD_RPM;
    g_lf.pid.kp        = KP_FORWARD;
    g_lf.pid.kd        = KD_FORWARD;
    g_lf.pid.last_error = 0.0f;
    g_lf.pid.correction = 0;
    g_lf.is_active      = 1;
    reset_all();
}

void LineFollow_ResumeBackward(void)
{
    g_lf.direction     = DIR_BACKWARD;
    g_lf.base_speed    = BASE_SPEED_BACKWARD_RPM;
    g_lf.pid.kp        = KP_BACKWARD;
    g_lf.pid.kd        = KD_BACKWARD;
    g_lf.pid.last_error = 0.0f;
    g_lf.pid.correction = 0;
    g_lf.is_active      = 1;
    reset_all();
}

void LineFollow_SwapSensors(void)
{
    g_lf.active_sensor = (g_lf.active_sensor == ACTIVE_FRONT)
        ? ACTIVE_REAR : ACTIVE_FRONT;
    g_lf.pid.last_error = 0.0f;
    g_lf.pid.correction = 0;
    reset_all();
#if DEBUG_ENABLE
    printf("[LF] ★ SWAPPED: active=%s ★\r\n",
           g_lf.active_sensor == ACTIVE_FRONT ? "FRONT" : "REAR");
#endif
}

void LineFollow_SetActiveSensor(ActiveSensor_t sensor)
{
    if (g_lf.active_sensor != sensor) {
        g_lf.active_sensor = sensor;
        g_lf.pid.last_error = 0.0f;
        g_lf.pid.correction = 0;
        reset_all();
    }
}

/*=================================================================
 * 平移模式下反光脉冲轮询 (小断点用)
 *=================================================================*/
static uint8_t  g_refl_pstate = 0;
static uint32_t g_refl_pulse_cnt = 0;

void LineFollow_ResetPulseCount(void)
{
    g_refl_pstate = 0;
    g_refl_pulse_cnt = 0;
}

uint8_t LineFollow_PollReflPulse(void)
{
    Sensor_Update();
    uint8_t rf = active_sensor_ptr()->refl_count;

    switch (g_refl_pstate) {
    case 0: /* IDLE */
        if (rf >= RF_ENTER_THRESHOLD) { g_refl_pstate = 1; }
        break;
    case 1: /* RISING */
        if (rf < RF_ENTER_THRESHOLD)  { g_refl_pstate = 2; }
        break;
    case 2: /* FALLING — 脉冲完成 */
        g_refl_pstate = 0;
        g_refl_pulse_cnt++;
        return 1;
    }
    return 0;
}

/*=================================================================
 * 辅助
 *=================================================================*/
float    LineFollow_GetError(void)           { return g_lf.pid.last_error; }
int16_t  LineFollow_GetCorrection(void)      { return g_lf.pid.correction; }
uint8_t  LineFollow_IsMovingBackward(void)   { return (g_lf.direction == DIR_BACKWARD) ? 1 : 0; }
ActiveSensor_t LineFollow_GetActiveSensor(void) { return g_lf.active_sensor; }

const char* LineFollow_FeatureName(RoadFeature_t f)
{
    switch (f) {
        case ROAD_NORMAL:       return "NORMAL";
        case ROAD_UPPER_CHECK:  return "UP_CHK";
        case ROAD_T_JUNCTION:   return "T_JUNC";
        case ROAD_REFL_SINGLE:  return "REFLx1";
        case ROAD_BLACK_DOUBLE: return "BLACKx2";
        case ROAD_REFL_LARGE:   return "REFL_BIG";
        case ROAD_LOST_LINE:    return "LOST";
        case ROAD_WHITE_DOUBLE: return "WHITEx2";
        default:                return "?";
    }
}
