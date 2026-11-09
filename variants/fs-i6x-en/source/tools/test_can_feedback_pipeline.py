#!/usr/bin/env python3
"""Compile the production CAN scheduler, OID parser and wheel snapshots with fake HAL time."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
core=root/'control_chassis/Core'
def function(s,signature):
 a=s.index(signature); b=s.index('{',a); end=b+1; depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[a:end]
chassis=(core/'UserDev/chassis_control.c').read_text()
wheel=function(chassis,'bool chassis_control_read_wheel_feedback(')
convert=function(chassis,'static int16_t chassis_erpm_to_speed_mmps(')
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "odrive_can.h"
#include "oid_can.h"
#include "telemetry_snapshot.h"
#include "can_tx_port.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#define OID_NODE_FL 2
#define OID_NODE_FR 1
#define OID_NODE_RL 4
#define OID_NODE_RR 3
#define OID_POLE_PAIRS 10
#define GEAR_RATIO 1.0f
#define WHEEL_DIAMETER 0.24f
#define PI 3.14159265f
typedef struct { uint8_t valid; uint32_t last_good_tick; int32_t speed_mm_per_s; } ChassisWheelFeedback_t;
CAN_HandleTypeDef hcan1;
osMessageQueueId_t Queue_CanTxHandle=1, Queue_CanRxHandle=2;
static uint32_t now_ms;
static unsigned critical_depth, free_mailboxes=1, attempts;
static bool busy[3];
static int full, ordinary_pending, fail_send, inject_stop;
static CAN_TxMsg_t ordinary, sent[32];
uint32_t HAL_GetTick(void) { return now_ms; }
void CanTxTest_EnterCritical(void) { ++critical_depth; }
void CanTxTest_ExitCritical(void) { --critical_depth; }
uint32_t CAN_TxPort_GetError(void) { return 0; }
bool CAN_TxPort_IsEmpty(uint8_t m) { return free_mailboxes && !busy[m]; }
CAN_TxPortResult_t CAN_TxPort_Poll(uint8_t m) {
 if(!busy[m]) return CAN_TX_PENDING;
 busy[m]=false; return CAN_TX_SUCCESS;
}
void CAN_TxPort_Abort(uint8_t m) { (void)m; assert(0); }
osStatus_t osMessageQueuePut(osMessageQueueId_t q,const void *p,uint8_t pri,uint32_t timeout) {
 (void)q;(void)pri;(void)timeout; if(full) return osError; ordinary=*(const CAN_TxMsg_t*)p; ordinary_pending=1;return osOK;
}
osStatus_t osMessageQueueGet(osMessageQueueId_t q,void *p,uint8_t *pri,uint32_t timeout) {
 (void)q;(void)pri;(void)timeout; if(!ordinary_pending) return osError; *(CAN_TxMsg_t*)p=ordinary; ordinary_pending=0;return osOK;
}
bool CAN_TxPort_Submit(uint8_t m, const CAN_TxMsg_t *msg) {
 sent[attempts++] = *msg;
 if(inject_stop) { uint8_t zero[5]={2,0,0,0,0}; CAN_Enqueue_Control_Message(msg->stdId,zero,5,0);inject_stop=0; }
 if(fail_send) return false;
 busy[m]=true; return true;
}
'''
tests=r'''
static void step(void) { (void)CAN_TxScheduler_Process(); assert(!critical_depth); }
int main(void) {
 CAN_TxScheduler_Init();
 uint8_t control[5]={2,0,0,1,0}, payload[8]={0}; CAN_TxMsg_t msg;
 full=1; assert(!CAN_Enqueue_Message(0x507,payload,8,0));
 assert(OID_Req_Actual_Speed(1)); assert(g_oid_drives[1].speed_query_queued==1);
 now_ms=10; assert(OID_Req_Actual_Speed(1));
 assert(CAN_TryGet_SpeedQuery(&msg) && msg.queued_at_ms==10 && msg.stdId==1);
 assert(!CAN_TryGet_SpeedQuery(&msg));
 full=0; assert(CAN_Enqueue_Message(0x507,payload,8,0));
 assert(CAN_Enqueue_Telemetry_Message(0x50a,payload,8,0));
 assert(CAN_Enqueue_Control_Message(1,control,5,0)); assert(OID_Req_Actual_Speed(2));
 free_mailboxes=0;step();assert(attempts==0); /* no work popped while HAL busy */
 free_mailboxes=1;step();assert(sent[0].dlc==5 && sent[0].stdId==1);
 step();assert(sent[1].dlc==2 && sent[1].stdId==2);
 step();assert(sent[2].stdId==0x50a);step();assert(sent[3].stdId==0x507);
 assert(g_oid_drives[2].speed_mailbox_ok==1);
 assert(OID_Req_Actual_Speed(3));fail_send=1;step();assert(g_can_tx_mailbox_failures==1);
 assert(g_oid_drives[3].speed_mailbox_fail==1);fail_send=0;step();assert(sent[5].stdId==3);
 assert(CAN_Enqueue_Control_Message(1,control,5,0)); fail_send=1;inject_stop=1;step();fail_send=0;
 step();assert(sent[7].data[3]==0); /* failed old drive cannot overwrite a newer stop */
 now_ms=100;assert(OID_Req_Actual_Speed(4));now_ms=150;
 assert(!CAN_TryGet_SpeedQuery(&msg) && g_can_speed_query_expired==1);
 now_ms=0xfffffff0U;assert(OID_Req_Actual_Speed(4));now_ms=4;
 assert(CAN_TryGet_SpeedQuery(&msg)); /* enqueue clock wrap */
 uint8_t speed[6]={0x0f,1,0,0,3,0xe8};
 now_ms=500; OID_Parse_Rx_MessageAt(2,speed,6,300);
 assert(g_oid_drives[2].actual_speed_update_tick==300); /* parser lag cannot renew */
 assert(g_oid_drives[2].actual_speed_erpm==1000 && g_oid_drives[2].actual_speed_valid);
 OID_Parse_Rx_MessageAt(2,speed,5,450);assert(g_oid_drives[2].actual_speed_update_tick==300);
 OID_Parse_Rx_MessageAt(2,speed,6,450);assert(g_oid_drives[2].speed_max_gap_ms==150);
 ChassisWheelFeedback_t wheels[4];assert(chassis_control_read_wheel_feedback(wheels));
 assert(wheels[0].last_good_tick==450 && wheels[0].valid && wheels[0].speed_mm_per_s>0);
 TelemetrySources_t sources={0};TelemetrySnapshot_t snapshot;TelemetryWheelDiagnostics_t diag;
 for(unsigned i=0;i<4;i++) {sources.wheels[i].valid=true;sources.wheels[i].last_good_ms=450;}
 sources.transport.mailbox_failures=g_can_tx_mailbox_failures;
 TelemetrySnapshot_Update(&sources,600);TelemetrySnapshot_Read(&snapshot);
 assert(!(snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT]&TELEMETRY_FAULT_WHEEL_STALE));
 TelemetrySnapshot_Update(&sources,601);TelemetrySnapshot_Read(&snapshot);
 assert(snapshot.values[TELEMETRY_VALUE_DETAILED_FAULT]&TELEMETRY_FAULT_WHEEL_STALE);
 assert(TelemetrySnapshot_ReadWheelDiagnostics(&diag) && diag.fault_events==1 && diag.fault_stale_mask==15);
 assert(diag.fault_age_ms[0]==151 && diag.fault_transport.mailbox_failures==2);
 TelemetrySnapshot_Update(&sources,602);TelemetrySnapshot_ReadWheelDiagnostics(&diag);assert(diag.fault_events==1);
 puts("PASS CAN pipeline: busy retention, control/query/telemetry priority, coalescing, failure retry, newer stop, expiry/wrap, actual OID ingress time, coherent wheels, 150/151ms fault capture");
}
'''
with tempfile.TemporaryDirectory(prefix='mower-can-pipeline-') as d:
 d=Path(d)
 (d/'can.h').write_text('#ifndef CAN_FAKE_H\n#define CAN_FAKE_H\n#include <stdint.h>\ntypedef struct {uint32_t ErrorCode;} CAN_HandleTypeDef;\n#endif\n')
 (d/'stm32f4xx_hal.h').write_text('#include <stdint.h>\n#define CAN_ID_STD 0U\n#define CAN_ID_EXT 4U\nuint32_t HAL_GetTick(void);\n')
 (d/'odrive_can.h').write_text((core/'Inc/odrive_can.h').read_text())
 (d/'oid_can.h').write_text((core/'Inc/oid_can.h').read_text())
 text=prefix+'\n'+convert+'\n'+wheel+'\n'+tests
 (d/'pipeline.c').write_text(text)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-I'+str(d),'-I'+str(root/'control_chassis/host/fakes/can_tx'),'-I'+str(core/'Inc'),str(d/'pipeline.c'),str(core/'UserDev/can_tx_scheduler.c'),str(core/'UserDev/oid_can.c'),str(core/'UserApp/telemetry_snapshot.c'),'-o',str(d/'pipeline')],check=True)
 subprocess.run([str(d/'pipeline')],check=True)
