/* Included after the production queue adapters by test_remote_rx_queue.py. */
static uint16_t selected_ch4=1500U, selected_ch5=2000U;
static uint32_t Clock(void *context){(void)context;return clock_ms;}
static void Reset(void){
 protocol_last_ms=0;clock_ms=0;qread=qwrite=qcount=protocol_calls=pairing_calls=0;selected_ch4=1500;selected_ch5=2000;
 RemoteBackend_Init((RemoteBackendClock_t){Clock,NULL});SafetyOwnershipGate_Reset();
}
static unsigned Frame(uint8_t *bytes,uint16_t pulse){
#if REMOTE_BACKEND_FLYSKY_IBUS
 memset(bytes,0,32);bytes[0]=0x20;bytes[1]=0x40;uint16_t checksum=0xffff;
 for(unsigned i=0;i<14;i++){uint16_t p=i==0?pulse:i==3?selected_ch4:i==4?selected_ch5:1500;bytes[2+i*2]=(uint8_t)p;bytes[3+i*2]=(uint8_t)(p>>8);}
 for(unsigned i=0;i<30;i++)checksum-=bytes[i];
 bytes[30]=(uint8_t)checksum;bytes[31]=(uint8_t)(checksum>>8);return 32;
#else
 const uint8_t frame[10]={0xab,0x08,0,0x08,0,0x08,0,0x08,0,0xba};memcpy(bytes,frame,10);
 bytes[1]=(uint8_t)(pulse>>8);bytes[2]=(uint8_t)pulse;return 10;
#endif
}
static void ReceiveBytes(uint32_t time,const uint8_t *bytes,unsigned n){
 assert(n<=64);clock_ms=time;memcpy(rx_dma_buffer,bytes,n);
 unsigned before=protocol_calls;
 HAL_UARTEx_RxEventCallback(REMOTE_BACKEND_FLYSKY_IBUS?&huart1:&huart3,(uint16_t)n);
 assert(protocol_calls==before); /* ISR only queues bytes; no protocol callbacks. */
}
static void ReceiveFrame(uint32_t time,uint16_t pulse){uint8_t bytes[64];unsigned n=Frame(bytes,pulse);ReceiveBytes(time,bytes,n);}
static RemoteCommand_t Read(void){RemoteCommand_t c={0};RemoteBackend_Advance(clock_ms,(RemoteSafetyInput_t){0});RemoteBackend_GetCommand(&c);return c;}
static uint32_t Limit(void){return REMOTE_BACKEND_FLYSKY_IBUS?100U:300U;}
static void TestExpiredQueueDoesNotRefresh(void){
 Reset();ReceiveFrame(10,1500);RemoteBackend_Process();ReceiveFrame(20,1600);clock_ms=1000;assert(clock_ms>Limit());
 RemoteBackend_Process();RemoteCommand_t c=Read();
 printf("old frame: received=20 processed=1000 last_valid=%u fresh=%d\n",c.last_valid_ms,c.link_fresh);fflush(stdout);
 assert(!c.link_fresh&&c.last_valid_ms==10);
}

static RemoteRxDiagnostics_t Diagnostics(void){RemoteRxDiagnostics_t d;RemoteBackend_GetRxDiagnostics(&d);return d;}
static void TestAgeBoundariesAndZero(void){
 for(unsigned edge=0;edge<2;edge++){
  Reset();ReceiveFrame(10,1500);RemoteBackend_Process();ReceiveFrame(20,1600);
  clock_ms=20+Limit()-1+edge;RemoteBackend_Process();RemoteCommand_t c=Read();
  assert(c.link_fresh==!edge&&c.last_valid_ms==(edge?10U:20U));
  assert(Diagnostics().expired_messages==edge);
 }
 Reset();ReceiveFrame(0,1500);RemoteBackend_Process();assert(Read().link_fresh);
 clock_ms=Limit()-1;assert(Read().link_fresh);clock_ms++;assert(!Read().link_fresh);
}
static void TestWrapAndSequenceWrap(void){
 Reset();uint32_t start=UINT32_MAX-50;ReceiveFrame(start,1500);clock_ms=start+Limit()-1;
 RemoteBackend_Process();RemoteCommand_t c=Read();assert(c.link_fresh&&c.last_valid_ms==start);
 clock_ms++;assert(!Read().link_fresh);
 Reset();g_rx_sequence=UINT32_MAX-1;ReceiveFrame(1,1500);RemoteBackend_Process();
 ReceiveFrame(2,1600);RemoteBackend_Process();c=Read();assert(c.frame_sequence==2&&c.last_valid_ms==2);
 assert(Diagnostics().invalid_messages==0);
}
static void TestOldQueueAndMixedQueue(void){
 Reset();for(unsigned i=0;i<16;i++)ReceiveFrame(i+1,1500);assert(qcount==16);
 clock_ms=1000;for(unsigned i=0;i<16;i++)RemoteBackend_Process();
 RemoteCommand_t c=Read();assert(!c.link_fresh&&c.frame_sequence==0&&protocol_calls==0);
 assert(Diagnostics().expired_messages==16&&Diagnostics().max_queue_age_ms==999);
 Reset();ReceiveFrame(10,1600);ReceiveFrame(990,1700);clock_ms=1000;
 RemoteBackend_Process();assert(!Read().link_fresh);RemoteBackend_Process();
 c=Read();assert(c.last_valid_ms==990&&c.frame_sequence==1&&c.link_fresh&&(c.run_permitted==(REMOTE_BACKEND_FLYSKY_IBUS==0)));
 assert(Diagnostics().expired_messages==1);
}
static void TestDmaBufferReuse(void){
 Reset();ReceiveFrame(10,1100);ReceiveFrame(20,2000);RemoteBackend_Process();
 RemoteCommand_t c=Read();assert(c.channels[0]==1100);
 RemoteBackend_Process();c=Read();assert(c.channels[0]==2000&&c.last_valid_ms==20);
}
static void TestInvalidMessages(void){
 Reset();RemoteRxMsg_t msg={0};msg.len=REMOTE_RX_BUFFER_SIZE+1;
 RemoteBackend_ProcessRxMessage(&msg);assert(Diagnostics().invalid_messages==1);
 msg.len=1;msg.data[0]=0x55;msg.sequence=1;RemoteBackend_ProcessRxMessage(&msg);
 RemoteBackend_ProcessRxMessage(&msg);assert(Diagnostics().invalid_messages==2);
}
#if REMOTE_BACKEND_FLYSKY_IBUS
static void TestFragmentsAndGaps(void){
 uint8_t bytes[64];unsigned n;
 Reset();n=Frame(bytes,1500);ReceiveBytes(10,bytes,7);RemoteBackend_Process();
 ReceiveBytes(20,bytes+7,n-7);clock_ms=55;RemoteBackend_Process();
 RemoteCommand_t c=Read();assert(c.frame_sequence==1&&c.last_valid_ms==20&&protocol_last_ms==55);
 /* Fresh suffix cannot salvage a prefix whose bytes are already 100ms old. */
 Reset();n=Frame(bytes,1500);ReceiveBytes(10,bytes,7);RemoteBackend_Process();
 ReceiveBytes(20,bytes+7,n-7);clock_ms=110;RemoteBackend_Process();
 c=Read();assert(c.frame_sequence==0&&!c.link_fresh&&Diagnostics().stream_resets==1);
 ReceiveFrame(111,1500);RemoteBackend_Process();assert(Read().frame_sequence==1);
 /* Stream timestamps follow sliding-window byte removal, not just chunk starts. */
 Reset();uint8_t garbage=0x55;ReceiveBytes(10,&garbage,1);RemoteBackend_Process();
 n=Frame(bytes,1500);ReceiveBytes(20,bytes,7);RemoteBackend_Process();
 ReceiveBytes(30,bytes+7,n-7);clock_ms=119;RemoteBackend_Process();
 assert(Read().frame_sequence==1&&Read().last_valid_ms==30);
 Reset();n=Frame(bytes,1500);ReceiveBytes(10,bytes,7);RemoteBackend_Process();
 RemoteBackend_OnUartError(1);assert(g_flysky_ibus.stream_count==7); /* ISR never parses. */
 ReceiveBytes(20,bytes+7,n-7);RemoteBackend_Process();
 assert(Read().frame_sequence==0&&Diagnostics().stream_resets==1);
 /* Identical metadata with a duplicate sequence must not duplicate a frame. */
 Reset();RemoteRxMsg_t m={0};m.len=(uint16_t)Frame(m.data,1500);m.sequence=1;m.received_at_ms=0;
 RemoteBackend_ProcessRxMessage(&m);RemoteBackend_ProcessRxMessage(&m);assert(Read().frame_sequence==1);
}
static void TestOverflowIsBoundedAndBreaksFragments(void){
 Reset();uint8_t bytes[64],noise=0x55;unsigned n=Frame(bytes,1500);
 for(unsigned i=0;i<15;i++)ReceiveBytes(i+1,&noise,1);
 ReceiveBytes(16,bytes,7);assert(qcount==16);
 ReceiveBytes(17,bytes+7,n-7);assert(qcount==16&&Diagnostics().queue_overflows==1);
 clock_ms=18;for(unsigned i=0;i<16;i++)RemoteBackend_Process();
 assert(g_flysky_ibus.stream_count==7);
 ReceiveBytes(19,bytes+7,n-7);RemoteBackend_Process();
 assert(Read().frame_sequence==0&&Diagnostics().stream_resets==1);
 ReceiveFrame(20,1500);RemoteBackend_Process();assert(Read().frame_sequence==1);
}
static void TestStabilityUsesReceptionTime(void){
 Reset();uint8_t pair[64];Frame(pair,1500);memcpy(pair+32,pair,32);
 for(unsigned i=0;i<10;i++)ReceiveBytes(100+10*i,pair,64);
 clock_ms=199;for(unsigned i=0;i<10;i++)RemoteBackend_Process();
 RemoteCommand_t c=Read();assert(c.frame_sequence==20&&!c.stable_online&&c.last_valid_ms==190);
 clock_ms=250;RemoteBackend_Process();assert(!Read().stable_online); /* 150ms processing-clock shortcut forbidden. */
 ReceiveFrame(260,1500);RemoteBackend_Process();c=Read();assert(c.stable_online&&!c.run_permitted);
}
static RemoteCommand_t MotionGate(void){
 RemoteCommand_t c=Read();SafetyOwnershipGateInput_t input={
  false,false,c.remote_recoverable_latched,c.connected&&c.link_fresh&&c.stable_online,
  c.run_permitted,c.run_permitted?EXECUTION_OWNER_MANUAL_REMOTE:EXECUTION_OWNER_NONE,
  c.frame_sequence,c.command_invalidation_sequence};
 SafetyOwnershipGate_Update(&input);return c;
}
static void Step(uint32_t time){ReceiveFrame(time,1500);RemoteBackend_Process();(void)MotionGate();}
static void TestArmedBacklogStopsAndNeedsNewUnlock(void){
 Reset();for(unsigned t=10;t<=400;t+=10)Step(t);
 selected_ch4=1000;for(unsigned t=410;t<=620;t+=10)Step(t);
 assert(RemoteBackend_ConfirmUnlock());Step(630);Step(640);
 assert(SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE,true));
 (void)SafetyOwnershipGate_TakeStopRequest();
 ReceiveFrame(650,2000);clock_ms=1000;RemoteBackend_Process();
 RemoteCommand_t c=MotionGate();assert(!c.run_permitted&&c.timeout_latched);
 assert(!SafetyOwnershipGate_Authorize(EXECUTION_OWNER_MANUAL_REMOTE,true));
 assert(SafetyOwnershipGate_TakeStopRequest());
 assert(!RemoteBackend_ConfirmUnlock());
 /* Fresh traffic restores connectivity; it never replays the old left/OK. */
 for(unsigned t=1010;t<=1260;t+=10)Step(t);
 c=MotionGate();assert(c.stable_online&&!c.run_permitted&&!RemoteBackend_ConfirmUnlock());
 selected_ch4=1500;for(unsigned t=1270;t<=1500;t+=10)Step(t);
 selected_ch4=1000;Step(1510);assert(RemoteBackend_ConfirmUnlock());
 Step(1520);assert(Read().run_permitted);
}
#else
static void TestLegacyButtonsCannotRefreshAndExpiredConfigIsIgnored(void){
 Reset();ReceiveFrame(10,2048);RemoteBackend_Process();
 uint8_t buttons[15]={0xcc,1,2,3,4,5,6,7,8,3,3,4,0xff,2,0xdd};
 ReceiveBytes(20,buttons,sizeof(buttons));clock_ms=319;RemoteBackend_Process();
 RemoteCommand_t c=Read();assert(!c.link_fresh&&c.last_valid_ms==10); /* Button age=299 but stick age=309. */
 Reset();ReceiveFrame(10,2048);RemoteBackend_Process();ReceiveBytes(20,buttons,sizeof(buttons));
 clock_ms=320;RemoteBackend_Process();c=Read();
 assert(c.config_flags==0&&c.keys[0]==0&&c.operation_mode==0&&c.last_valid_ms==10);
 assert(Diagnostics().expired_messages==1);
}
#endif
static void TestUartErrorMetadata(void){
 Reset();
 UART_HandleTypeDef *uart=REMOTE_BACKEND_FLYSKY_IBUS?&huart1:&huart3;
 uart->ErrorCode=8U;
 RemoteBackend_OnUartError((uintptr_t)uart->Instance);
 assert(Diagnostics().uart_errors==1U && Diagnostics().last_uart_error==8U);
 RemoteBackend_OnUartError((uintptr_t)99U);
 assert(Diagnostics().uart_errors==1U);
 g_rx_diagnostics.uart_errors=UINT32_MAX;
 RemoteBackend_OnUartError((uintptr_t)uart->Instance);
 assert(Diagnostics().uart_errors==UINT32_MAX);
}
int main(void){
 TestUartErrorMetadata();
 TestExpiredQueueDoesNotRefresh();TestAgeBoundariesAndZero();TestWrapAndSequenceWrap();
 TestOldQueueAndMixedQueue();TestInvalidMessages();TestDmaBufferReuse();
#if REMOTE_BACKEND_FLYSKY_IBUS
 TestFragmentsAndGaps();TestOverflowIsBoundedAndBreaksFragments();
 TestStabilityUsesReceptionTime();TestArmedBacklogStopsAndNeedsNewUnlock();
#else
 TestLegacyButtonsCannotRefreshAndExpiredConfigIsIgnored();
#endif
 puts("PASS target UART callback / 16-slot queue / parser / authorization / safety gate; age, wrap, backlog, fragments and recovery");return 0;
}
