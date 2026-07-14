/**
 * @file    state_machine.c
 * @brief   双周期五箱 — 小断点平移模式
 *
 * 小断点: 检测到ROAD_REFL_SINGLE后→周期0右平移/周期1左平移
 *         平移过程中传感器扫过反光条, 逐箱匹配标签→放豆
 *         全部5箱走完后回归正常循迹
 */

#include "state_machine.h"
#include "communication.h"
#include "main.h"

static SystemState_t     g_s=STATE_INIT,g_last=STATE_INIT;
static uint32_t          g_tick=0,g_entry=0,g_act=0;
static uint8_t           g_cycle;            /* 0周期2(T右),1周期2(T左) */
static uint8_t           g_upper_visits;     /* 上断点到访次数 */
static RecognizedDigit_t g_box[NUM_TOTAL_BOXES];
static RecognizedDigit_t g_bean_label;
static uint8_t           g_stop_idx;         /* 当前经过的箱子序号 0~4 */
static uint8_t           g_retry;

static uint32_t Ms(void){return HAL_GetTick();}

const char* StateMachine_GetStateName(SystemState_t s){switch(s){
    case STATE_INIT:return"INIT"; case STATE_LINE_FOLLOW_FORWARD:return"FWD";
    case STATE_UPPER_CHECKPOINT:return"UP"; case STATE_T_TRANSLATE:return"T";
    case STATE_SMALL_TRANSLATE:return"SMALL"; case STATE_LOWER_CHECKPOINT:return"LO";
    case STATE_BEAN_DETECT:return"B_DET"; case STATE_BEAN_RECOG:return"B_REC";
    case STATE_BEAN_DONE:return"B_OK"; case STATE_EXECUTE_LEFT:return"EXL";
    case STATE_EXECUTE_RIGHT:return"EXR"; case STATE_EXECUTE_FORWARD:return"EXF";
    case STATE_ARRIVED:return"DONE"; case STATE_ERROR:return"ERR"; default:return"?";}}

static void Go(SystemState_t ns){g_last=g_s;g_s=ns;g_tick=0;g_entry=Ms();
    printf("\r\n[SM]%s→%s(c%d)\r\n\r\n",
           StateMachine_GetStateName(g_last),StateMachine_GetStateName(g_s),g_cycle);}

/*==== 视觉帧等待 ====*/
static int8_t WaitSingle(RecognizedDigit_t*d){
    DecodedFrame_t f;
    while(Comm_DecodeNext(&f)){
        if(f.is_vision&&f.vision.detected){*d=f.vision.digit;Comm_SendAck(f.vision.frame_id);return 1;}
    }
    if(Ms()-g_entry>=VISION_RESPONSE_TIMEOUT_MS){g_retry++;if(g_retry>=VISION_MAX_RETRIES)return -1;Comm_TriggerRecognize();g_entry=Ms();}
    return 0;
}
static int8_t WaitMultiBox(DecodedFrame_t*f){
    while(Comm_DecodeNext(f)){
        if(f->is_multibox){Comm_SendAck(0);return 1;}
        if(f->is_vision&&f->vision.detected){Comm_SendAck(f->vision.frame_id);
            memset(f->box_labels,0,sizeof(f->box_labels));f->box_labels[0]=f->vision.digit;
            f->missing_index=0;f->is_multibox=1;return 1;}
    }
    if(Ms()-g_entry>=VISION_RESPONSE_TIMEOUT_MS){g_retry++;if(g_retry>=VISION_MAX_RETRIES)return -1;Comm_TriggerRecognize();g_entry=Ms();}
    return 0;
}

/*==== 重排序 ====*/
static void ReorderBoxes(const DecodedFrame_t*f,uint8_t upper){
    RecognizedDigit_t ml=f->missing_label,vis[NUM_VISIBLE_BOXES];
    memcpy(vis,f->box_labels,sizeof(vis));
    if(upper){g_box[0]=ml;g_box[1]=vis[0];g_box[2]=vis[1];g_box[3]=vis[2];g_box[4]=vis[3];}
    else     {g_box[0]=ml;g_box[1]=vis[3];g_box[2]=vis[2];g_box[3]=vis[1];g_box[4]=vis[0];}
    printf("[SM]Reorder %s: [%d %d %d %d %d] (miss=#%d lbl=%d)\r\n",
           upper?"UP":"LO",(int)g_box[0],(int)g_box[1],(int)g_box[2],(int)g_box[3],(int)g_box[4],
           (int)f->missing_index,(int)ml);
}

/*==== 放豆动作 (以后替换为 GPIO拉高/电机伸出) ====*/
static void DropBean(void){
    printf("[SM] ╔══════════════════╗\r\n");
    printf("[SM] ║  ★★★ DROP ★★★  ║\r\n");
    printf("[SM] ╚══════════════════╝\r\n");
    /* TODO: 实际的放豆机构驱动代码 */
}

/*==== 进入小断点平移区域 ====*/
static void EnterSmallTranslate(void){
    uint8_t dir=(g_cycle==0)?DIR_RIGHT:DIR_LEFT;
    printf("[SM] Enter small stop zone → %s\r\n",dir==DIR_RIGHT?"RIGHT":"LEFT");
    LineFollow_Stop();
    LineFollow_ResetPulseCount();
    Motor_Translate(TRANSLATE_SPEED_RPM,(MoveDirection_t)dir);
    Go(STATE_SMALL_TRANSLATE);
}

/*==== 初始化 ====*/
void StateMachine_Init(void){
    g_s=STATE_INIT;g_last=STATE_INIT;g_tick=0;g_entry=Ms();
    g_cycle=0;g_upper_visits=0;g_bean_label=DIGIT_NONE;g_retry=0;
    g_stop_idx=0;memset(g_box,0,sizeof(g_box));
    Sensor_Init();Motor_Init();LineFollow_Init();Comm_Init();
    printf("\r\n[SM]===Dual-Cycle 5-Box===\r\n");
    printf("[SM]C0:SMALL→R T→R  C1:SMALL→L T→L\r\n");
    printf("[SM]UP miss→pos0(1→5)  LO miss→pos0(5→1)\r\n\r\n");
}

/*=====================================================================
 * 主循环
 *=====================================================================*/
void StateMachine_Run(void){g_tick++;
switch(g_s){

/*==========================================================*/
case STATE_INIT:
    Motor_StopAll();
    if(Ms()-g_entry>=3000)Go(STATE_LINE_FOLLOW_FORWARD);
    break;

/*==========================================================*/
case STATE_LINE_FOLLOW_FORWARD:{
    if(g_tick==1){
        if(g_last==STATE_BEAN_DONE){
            /* ||抓豆完成, SwapSensors已切到REAR, 反向后退行驶 */
            LineFollow_ResumeBackward();
        }else if(g_last==STATE_SMALL_TRANSLATE){
            LineFollow_ResumeForward();
        }else{
            LineFollow_SetForward();
        }
    }
    RoadFeature_t f;LineFollow_Update(&f);
    switch(f){
    case ROAD_BLACK_DOUBLE:
        LineFollow_Stop();printf("[SM]★BEAN||\r\n");Go(STATE_BEAN_DETECT);break;
    case ROAD_REFL_LARGE:
        LineFollow_Stop();printf("[SM]★LOWER(c%d)\r\n",g_cycle);Go(STATE_LOWER_CHECKPOINT);break;
    case ROAD_UPPER_CHECK:
        LineFollow_Stop();printf("[SM]★UPPER v%d(c%d)\r\n",g_upper_visits+1,g_cycle);Go(STATE_UPPER_CHECKPOINT);break;
    case ROAD_T_JUNCTION:{
        uint8_t d=(g_cycle==0)?DIR_RIGHT:DIR_LEFT;
        printf("[SM]T→%s\r\n",d==DIR_RIGHT?"R":"L");
        LineFollow_Stop();Motor_Translate(TRANSLATE_SPEED_RPM,(MoveDirection_t)d);
        g_act=Ms();Go(STATE_T_TRANSLATE);break;}
    case ROAD_REFL_SINGLE:
        EnterSmallTranslate();break;        /* ← 不停车, 直接切入平移 */
    case ROAD_LOST_LINE:
        LineFollow_Stop();Go(STATE_ERROR);break;
    default:break;}break;}

/*==== T平移 ====*/
case STATE_T_TRANSLATE:
    if(Ms()-g_act>=TRANSLATE_DURATION_MS){Motor_StopAll();Go(STATE_LINE_FOLLOW_FORWARD);}
    break;

/*=================================================================
 * SMALL_TRANSLATE — 平移经过5个箱
 *   进入时 g_stop_idx 是上次断点复位的 0
 *   先检查当前箱(刚探测到的反光条), 然后等脉冲
 *=================================================================*/
case STATE_SMALL_TRANSLATE: {
    uint8_t dir=(g_cycle==0)?DIR_RIGHT:DIR_LEFT;

    /* t=1: 刚进入, 检查刚触发的那个小断点 */
    if(g_tick==1){
        RecognizedDigit_t label=g_box[g_stop_idx];
        if(g_bean_label!=DIGIT_NONE && label==g_bean_label){
            printf("[SM] ★ SMALL#%d label=%d==BEAN%d → DROP! ★\r\n",
                   g_stop_idx,(int)label,(int)g_bean_label);
            DropBean();
        }else{
            printf("[SM] SMALL#%d label=%d ≠ bean%d → skip\r\n",
                   g_stop_idx,(int)label,(int)g_bean_label);
        }
        g_stop_idx++;
    }

    /* 持续平移 (已经在转了, 每次tick保持) */
    Motor_Translate(TRANSLATE_SPEED_RPM,(MoveDirection_t)dir);

    /* 轮询新的反光脉冲 */
    if(LineFollow_PollReflPulse()){
        if(g_stop_idx<NUM_TOTAL_BOXES){
            RecognizedDigit_t label=g_box[g_stop_idx];
            if(g_bean_label!=DIGIT_NONE && label==g_bean_label){
                printf("[SM] ★ SMALL#%d label=%d==BEAN%d → DROP! ★\r\n",
                       g_stop_idx,(int)label,(int)g_bean_label);
                DropBean();
            }else{
                printf("[SM] SMALL#%d label=%d ≠ bean%d\r\n",
                       g_stop_idx,(int)label,(int)g_bean_label);
            }
            g_stop_idx++;
        }
    }

    /* 5个箱全走完 → 回正常循迹 */
    if(g_stop_idx>=NUM_TOTAL_BOXES){
        Motor_StopAll();
        printf("[SM] Small stops done → resume line-follow\r\n");
        Go(STATE_LINE_FOLLOW_FORWARD);
    }
    break;}

/*=================================================================
 * 豆子箱
 *=================================================================*/
case STATE_BEAN_DETECT:
    if(g_tick==1){Comm_FlushRx();Comm_TriggerGimbal(GACT_HEAD_DOWN);Comm_TriggerRecognize();g_retry=0;
        printf("[SM] Bean head down...\r\n");}
    if(Ms()-g_entry>=1000)Go(STATE_BEAN_RECOG);
    break;

case STATE_BEAN_RECOG:{
    RecognizedDigit_t d;int8_t r=WaitSingle(&d);
    if(r==1){g_bean_label=d;printf("[SM] Bean=%d\r\n",(int)d);Go(STATE_BEAN_DONE);}
    else if(r==-1){g_bean_label=DIGIT_NONE;printf("[SM] Bean fail→continue\r\n");Go(STATE_BEAN_DONE);}
    break;}

case STATE_BEAN_DONE:
    if(g_tick==1){Comm_TriggerGimbal(GACT_RETURN_180);LineFollow_SwapSensors();
        printf("[SM] Done,return180+swapped\r\n");}
    if(Ms()-g_entry>=2000)Go(STATE_LINE_FOLLOW_FORWARD);
    break;

/*=================================================================
 * 下断点 → 左扫拍照 → 收多箱帧 → missing放首位 → 复位计数
 *=================================================================*/
case STATE_LOWER_CHECKPOINT:
    if(g_tick==1){Comm_FlushRx();Comm_TriggerGimbal(GACT_LEFT_SWEEP);Comm_TriggerRecognize();g_retry=0;
        printf("[SM] Lower→LEFT SWEEP+photo\r\n");}
    {DecodedFrame_t mf;int8_t r=WaitMultiBox(&mf);
    if(r==1){ReorderBoxes(&mf,0);g_stop_idx=0;printf("[SM]Lower done→small stops\r\n");Go(STATE_LINE_FOLLOW_FORWARD);}
    else if(r==-1){printf("[SM]Lower fail→continue\r\n");Go(STATE_LINE_FOLLOW_FORWARD);}}break;

/*=================================================================
 * 上断点 v1→收帧+cycle2+T左移  v2→收帧+终点
 *=================================================================*/
case STATE_UPPER_CHECKPOINT:
    if(g_tick==1){g_upper_visits++;Comm_FlushRx();Comm_TriggerGimbal(GACT_RIGHT_15);Comm_TriggerRecognize();g_retry=0;
        printf("[SM] Upper v%d→RIGHT SWEEP+photo\r\n",g_upper_visits);}
    {DecodedFrame_t mf;int8_t r=WaitMultiBox(&mf);
    if(r==1){ReorderBoxes(&mf,1);g_stop_idx=0;
        if(g_upper_visits==1){printf("[SM]→Cycle2!T→L\r\n");g_cycle=1;
            Motor_Translate(TRANSLATE_SPEED_RPM,DIR_LEFT);g_act=Ms();Go(STATE_T_TRANSLATE);}
        else{printf("[SM]v2→DONE\r\n");Go(STATE_ARRIVED);}}
    else if(r==-1){if(g_upper_visits==1){g_cycle=1;Motor_Translate(TRANSLATE_SPEED_RPM,DIR_LEFT);g_act=Ms();Go(STATE_T_TRANSLATE);}
        else Go(STATE_ARRIVED);}}break;

/*==== EXEC ====*/
case STATE_EXECUTE_LEFT:
    if(g_tick==1)g_act=Ms();Motor_Translate(TRANSLATE_SPEED_RPM,DIR_LEFT);
    if(Ms()-g_act>=TRANSLATE_DURATION_MS){Motor_StopAll();Go(STATE_ARRIVED);}break;
case STATE_EXECUTE_RIGHT:
    if(g_tick==1)g_act=Ms();Motor_Translate(TRANSLATE_SPEED_RPM,DIR_RIGHT);
    if(Ms()-g_act>=TRANSLATE_DURATION_MS){Motor_StopAll();Go(STATE_ARRIVED);}break;
case STATE_EXECUTE_FORWARD:
    if(g_tick==1){g_act=Ms();LineFollow_SetForward();}
    LineFollow_Update(NULL);
    if(Ms()-g_act>=FORWARD_AFTER_ACTION_MS){LineFollow_Stop();Go(STATE_ARRIVED);}break;

case STATE_ARRIVED:if(g_tick==1){Motor_StopAll();printf("\r\n[SM]★★★DONE★★★\r\n\r\n");}break;
case STATE_ERROR:if(g_tick==1){Motor_StopAll();printf("[SM]★ERROR★\r\n");}break;
default:Go(STATE_ERROR);break;}
}

SystemState_t StateMachine_GetState(void){return g_s;}
