#include "heartbeat.h"

/**
 * @brief 初始化心跳结构体
 */
void Heartbeat_Init(Heartbeat_Handle_t *hb)
{
    hb->last_packet_tick = HAL_GetTick();
    hb->state = LINK_LOST; // 默认未连接，直到收到第一个包
    hb->signal_level = 0;
    hb->lost_count = 0;
}

/**
 * @brief 喂狗：每当收到合法的串口数据包时调用一次
 */
void Heartbeat_Feed(Heartbeat_Handle_t *hb)
{
    uint32_t current_tick = HAL_GetTick();
    uint32_t interval = current_tick - hb->last_packet_tick;
    
    // 异常处理：如果间隔大得离谱(比如刚开机或断连很久)，限制最大值
    // 防止一个巨大的数值把平均值拉得太高，导致很久恢复不过来
    if (interval > SIGNAL_LEVEL_1_MS + 500) {
        interval = SIGNAL_LEVEL_1_MS + 500;
    }

    // --- 核心逻辑：断连后的第一次重连 ---
    // 如果之前是断连状态，或者间隔非常大，说明刚恢复连接。
    // 此时不能只填入一个数据，否则平均值还是很大(信号显示1格)。
    // 我们需要"预填充"缓冲区，让信号快速(但不是瞬间)恢复。
    if (hb->state == LINK_LOST || interval > 2000)
    {
        // 策略：如果是断连后第一包，我们暂时认为信号是"中等"(比如3格水平)，
        // 快速填满缓冲区，避免用户看到信号从0慢慢爬到4
        for(int i=0; i<HB_FILTER_SIZE; i++) {
            hb->interval_buf[i] = interval; // 或者设为 300ms
        }
    }
    else
    {
        // 正常情况：存入环形缓冲区
        hb->interval_buf[hb->buf_idx] = interval;
        hb->buf_idx++;
        if (hb->buf_idx >= HB_FILTER_SIZE) {
            hb->buf_idx = 0;
        }
    }

    // 更新时间戳和状态
    hb->last_packet_tick = current_tick;
    hb->state = LINK_ALIVE;

    // --- 计算平均值 ---
    uint32_t sum = 0;
    for(int i=0; i<HB_FILTER_SIZE; i++) {
        sum += hb->interval_buf[i];
    }
    hb->avg_interval = sum / HB_FILTER_SIZE;

    // --- 根据平均值评级 (非常稳定) ---
    if (hb->avg_interval < SIGNAL_LEVEL_4_MS)      hb->signal_level = 4;
    else if (hb->avg_interval < SIGNAL_LEVEL_3_MS) hb->signal_level = 3;
    else if (hb->avg_interval < SIGNAL_LEVEL_2_MS) hb->signal_level = 2;
    else if (hb->avg_interval < SIGNAL_LEVEL_1_MS) hb->signal_level = 1;
    else                                           hb->signal_level = 0; // 极差
}

/**
 * @brief 检查：在主循环中调用，判断是否超时
 */
void Heartbeat_Check(Heartbeat_Handle_t *hb)
{
    // 如果超过超时时间 (比如3秒) 没收到包
    if (HAL_GetTick() - hb->last_packet_tick > HEARTBEAT_TIMEOUT_MS)
    {
        hb->state = LINK_LOST;
        hb->signal_level = 0;
        
        // 关键：断连时，把缓冲区填满"烂数据"，
        // 这样下次重连时，不会因为收到一个包就瞬间跳到4格
        for(int i=0; i<HB_FILTER_SIZE; i++) {
            hb->interval_buf[i] = SIGNAL_LEVEL_1_MS + 1000;
        }
        hb->avg_interval = SIGNAL_LEVEL_1_MS + 1000;
    }
}

uint8_t Heartbeat_GetSignalLevel(Heartbeat_Handle_t *hb)
{
    if (hb->state == LINK_LOST) return 0;
    return hb->signal_level;
}