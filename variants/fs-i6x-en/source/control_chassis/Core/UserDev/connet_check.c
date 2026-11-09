/**
 ******************************************************************************
 * @file           : connet_check.c
 * @brief          : Communication heartbeat monitor
 ******************************************************************************
 */

#include "connet_check.h"
#include <stdio.h>
#include "app_context.h"
#include "remote_backend.h"

#include "serial_port.h"

static uint8_t g_suspend_heartbeat_check = 0;

uint32_t GetLastReceiveTimeAgo(void)
{
    return RemoteBackend_LastReceiveAgeMs();
}

void Suspend_Heartbeat_Check(uint8_t suspend)
{
    g_suspend_heartbeat_check = suspend;
}

uint8_t CheckCommunicationStatus(void)
{
    static uint8_t last_reported_connected = 0xFF;
    static uint32_t last_dbg_tick = 0;

    if (g_suspend_heartbeat_check)
    {
        uint32_t now = HAL_GetTick();
        if ((last_reported_connected != 1U) || ((now - last_dbg_tick) >= 500U))
        {
            last_dbg_tick = now;
            last_reported_connected = 1U;
           // Serial_Printf(SERIAL_PORT_2, "[COMM] suspend=1 connected=1 now=%lu\r\n", now);
        }
        return 1;
    }

    (void)last_reported_connected;
    (void)last_dbg_tick;
    return RemoteBackend_IsConnected() ? 1U : 0U;
}
