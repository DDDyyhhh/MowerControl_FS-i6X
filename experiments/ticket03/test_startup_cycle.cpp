/* Receiver scheduling and physical values only; GUI and both firmwares are real. */
#include "remote_backend_config.h"
static SensIbusResponder_t sens;
static unsigned radio_at, board_at;
static bool drop_receipts, drop_runtime, drop_uplink;
static unsigned sensor_address;
static bool replaying;
static Exp3Link replay_link;
static Exp3UiState display;
static bool snapshot(void *,TelemetrySnapshot_t *s){return TelemetrySnapshot_Read(s);}
static bool response(void *,const uint8_t *b,uint8_t n){
 if(test_now<radio_at || n<6U)return true;
 unsigned address=b[1]&15U;
 if(address<9U || (drop_receipts&&(address==9U||address>=13U)) || (drop_runtime&&address==10U))return true;
 uint8_t bytes[37];memset(bytes,0xff,sizeof(bytes));
 bytes[0]=0xacU;bytes[9]=address==9U?0xe1U:address==10U?MOWER_FAST_STATUS_TYPE:
  address==11U?0xe3U:address==12U?0xe4U:(uint8_t)(EXP3_COMPLETION_TYPE+address-13U);
 bytes[10]=(uint8_t)address;bytes[11]=n-4U;memcpy(bytes+12,b+2,n-4U);
 exp3_board_capture_rf(bytes,37U,test_now);return true;
}
static uint16_t ch4(){return (uint16_t)(channelOutputs[3]/2+1500);}
static void tick(){
 ++test_now;ticks=test_now/10U;
 if(test_now==radio_at)exp3_board_auto_start(789U,test_now);
 if(test_now>=radio_at)exp3_board_poll(test_now);
 if(test_now>=board_at){
  if(test_now>=radio_at && !drop_uplink && test_now%10U==0U)test_receive(ch4(),replaying?([](){uint16_t a,b;uint16_t w=exp3_link_word(&replay_link,test_now,20U);exp3_channels(w,&a,&b);return (uint32_t)a|((uint32_t)b<<16);})():g_exp3_uplink_channels);
  if(test_now%10U==0U)test_controller_tick();
  if(test_now%20U==0U)controller_exp3_board_poll(test_now);
  if(test_now%10U==0U){
   unsigned address=1U+sensor_address++%15U;uint8_t c=(uint8_t)(0xa0U+address);
   uint8_t req[4]={4U,c,(uint8_t)(0xfbU-c),0xffU};SensIbusResponder_Feed(&sens,req,4U);
  }
 }
 if(test_now>=radio_at&&test_now%10U==0U)display=mowerExp3Step(0);
}
static void advance(unsigned n){while(n--)tick();}
static void center(){channelOutputs[3]=0;advance(1200U);}
static void start(){
 const unsigned began=test_now;
 const bool ready=!g_exp3_hil.waiting && mower_auth_phase(controller_g_exp3_hil.authority)==MOWER_AUTH_CENTER;
 channelOutputs[3]=-1000;
 unsigned until=test_now+1000U;
 while(test_now<until && display!=EXP3_UI_PROMPT)tick();
 if(display!=EXP3_UI_PROMPT){fprintf(stderr,"prompt timeout t=%u display=%u phase=%u token=%u bound=%u/%u auth=%04x\n",
 test_now,display,mower_auth_phase(controller_g_exp3_hil.authority),controller_g_exp3_hil.token,
 g_exp3_hil.bound,controller_g_exp3_hil.bound,controller_g_exp3_hil.authority);abort();}
 if(ready)assert(test_now-began<=200U);
}
static Exp3Message ok(){
 assert(display==EXP3_UI_PROMPT);
 display=mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER));assert(display==EXP3_UI_WAIT);
 return mowerUnlockDisplay?mowerUnlockProbe.pending:mowerExp3Probe.pending;
}
static void replay(const Exp3Message &message){
 replay_link=Exp3Link{};Exp3Message wire=message;
 if(!wire.kind)wire.kind=EXP3_CONFIRM;wire.sender=1U;wire.status=wire.command=0U;
 assert(exp3_link_send(&replay_link,&wire,test_now));replaying=true;
 while(replay_link.tx_active)tick();replaying=false;advance(500U);
}
static void completed(bool blade){
 unsigned until=test_now+9000U;
 while(test_now<until && !(blade?test_blade():test_permitted()))tick();
 if(!(blade?test_blade():test_permitted()))fprintf(stderr,"execution failure blade=%u display=%u ui=%u/%u reason=%u authority=%04x active=%u request=%u phase=%u result=%u pending=%u\n",blade,display,mowerUnlockProbe.state,mowerExp3Probe.state,controller_g_exp3_hil.intent_reason,controller_g_exp3_hil.authority,controller_g_exp3_hil.token_active,controller_g_exp3_hil.token_request,mower_auth_phase(controller_g_exp3_hil.authority),g_exp3_hil.last_result.status,g_exp3_hil.waiting);
 assert(blade?test_blade():test_permitted());
 advance(1200U);
 if(display!=EXP3_UI_IDLE && display!=EXP3_UI_CANCEL_WAIT) fprintf(stderr,
  "completion display=%u ui=%u/%u hmiWait=%u matches=%u result=%u command=%u token=%u boardtoken=%u authority=%04x runtimePerm=%u blade=%u\n",
  display,mowerUnlockProbe.state,mowerExp3Probe.state,g_exp3_hil.waiting,g_exp3_hil.matches,
  g_exp3_hil.last_result.status,g_exp3_hil.last_result.command,g_exp3_hil.last_result.generation,
  controller_g_exp3_hil.token,controller_g_exp3_hil.authority,test_permitted(),test_blade());
 assert(display==EXP3_UI_IDLE || display==EXP3_UI_CANCEL_WAIT);
}
int main(int argc,char **argv){
 unsigned scenario=argc>1?(unsigned)atoi(argv[1]):0U;
 radio_at=scenario==1U?600U:1U;board_at=scenario==0U?600U:1U;
 test_controller_init();SensIbusResponder_Init(&sens,snapshot,0,response,0);
 for(;test_now<2500U;){channelOutputs[3]=(test_now/50U&1U)?-1000:0;tick();}
 center();
 for(unsigned n=0;n<16000U && (!g_exp3_hil.bound||!controller_g_exp3_hil.bound);++n)tick();
 center();assert(g_exp3_hil.bound&&controller_g_exp3_hil.bound);assert(!test_permitted()&&!test_blade());
 if(scenario<=2U){
  for(unsigned retry=0;retry<3U;retry++){
   start();Exp3Message old=ok();test_safety(1U);advance(1000U);
   assert(!test_permitted()&&!test_blade());test_safety(0U);center();
   assert(display!=EXP3_UI_PROMPT);(void)old;
  }
  start();Exp3Message unlock=ok();completed(false);assert(!test_blade());
  /* A second physical gesture and explicit OK are required for the blade. */
  advance(1000U);assert(!test_blade());center();start();Exp3Message blade=ok();completed(true);
  assert(blade.context!=unlock.context&&blade.generation>unlock.generation);
  center();assert(!test_blade());
  printf("PASS startup order %u: early missed gestures, three rejected retries, fresh unlock, independent blade, no BMS\n",scenario);
 }else if(scenario==3U){
  start();unsigned generation=controller_g_exp3_hil.token;advance(10100U);
  assert(!test_permitted()&&!test_blade());assert(display!=EXP3_UI_PROMPT);
  center();start();assert(controller_g_exp3_hil.token>generation);ok();completed(false);
  puts("PASS 10s token expires while held left, center closes warning, next operation recovers");
 }else if(scenario==4U){
  start();uint32_t old_hmi=g_exp3_hil.hmi;drop_receipts=true;ok();advance(11000U);
  assert(!g_exp3_hil.bound || g_exp3_hil.hmi!=old_hmi);
  drop_receipts=false;center();advance(10000U);center();start();ok();completed(false);
  puts("PASS lost slow/fast receipts cause bounded automatic session renewal, no reboot required");
 }else if(scenario==5U){
  drop_runtime=true;channelOutputs[3]=-1000;advance(1200U);
  assert(mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER))!=EXP3_UI_WAIT);assert(!test_permitted());
  drop_runtime=false;advance(1500U);assert(display!=EXP3_UI_PROMPT);
  center();start();ok();completed(false);
  puts("PASS frozen/missing runtime discards early OK; feedback recovery never authorizes held-left intent");
 }else if(scenario==6U){
  /* An early OK while waiting for the new token is ignored, not queued and not
   * allowed to permanently erase a synchronized center. */
  channelOutputs[3]=-1000;display=mowerExp3Step(EVT_KEY_BREAK(KEY_ENTER));
  assert(display!=EXP3_UI_WAIT);advance(800U);assert(display==EXP3_UI_PROMPT);
  assert(!test_permitted());ok();completed(false);test_battery(1U);center();
  channelOutputs[3]=-1000;advance(800U);assert(display==EXP3_UI_BATTERY_LOW&&!test_blade());
  puts("PASS early OK consumes only that keypress; new token/new OK works; real LOW still denies blade");
 }else if(scenario==7U){
  start();ok();test_reboot();advance(13000U);center();start();ok();completed(false);
  puts("PASS controller identity restart during confirmation: stale OK rejected, explicit new operation recovers");
 }else if(scenario==8U){
  start();ok();exp3_board_restart_session(test_now);mowerExp3Initialized=false;
  advance(10000U);center();start();ok();completed(false);
  puts("PASS HMI identity restart during confirmation: paired rebind, fresh center and explicit new OK");
 }else if(scenario==9U){
  controller_g_exp3_hil.token=4094U;center();start();assert(controller_g_exp3_hil.token==4095U);
  center();channelOutputs[3]=-1000;advance(8000U);assert(display!=EXP3_UI_PROMPT);
  center();start();assert(controller_g_exp3_hil.token<4095U);ok();completed(false);
  puts("PASS 4095 boundary automatically renews real paired session, no truncation or power cycle");
 }else if(scenario==10U){
  start();Exp3Message old=ok();center();advance(5000U);assert(!test_permitted());
  center();start();replay(old);assert(!test_permitted());
  old.kind=EXP3_CANCEL;replay(old);assert(!test_permitted());
  assert(display==EXP3_UI_PROMPT);ok();completed(false);
  puts("PASS delayed old Confirm and old Cancel cannot execute or cancel a newer physical request");
 }else if(scenario==11U){
  start();drop_uplink=true;advance(500U);assert(!test_permitted());
  drop_uplink=false;advance(1500U);assert(display!=EXP3_UI_PROMPT);center();start();
  test_safety(2U);advance(400U);test_safety(0U);advance(1200U);
  assert(display!=EXP3_UI_PROMPT&&!test_permitted());center();start();
  test_mode(2U);advance(400U);test_mode(0U);advance(1200U);
  assert(display!=EXP3_UI_PROMPT&&!test_permitted());center();start();ok();completed(false);
  puts("PASS uplink timeout, released collision latch and ownership change require fresh gesture; no stale automatic execution");
 }else if(scenario==14U){
  start();ok();completed(false);
  for(unsigned attempt=0U;attempt<2U;++attempt){
   drop_uplink=true;advance(REMOTE_IBUS_TIMEOUT_MS+20U);assert(!test_permitted()&&!test_blade());
   assert(test_faults() & TELEMETRY_FAULT_IBUS_TIMEOUT);
   drop_uplink=false;center();assert(!test_permitted()&&!test_blade());
   assert(test_faults() & TELEMETRY_FAULT_IBUS_TIMEOUT);
   start();ok();completed(false);assert(test_timeout_history());
   assert(!(test_faults() & TELEMETRY_FAULT_IBUS_TIMEOUT));
   center();start();ok();completed(true);
   assert(!(test_faults() & TELEMETRY_FAULT_IBUS_TIMEOUT));
  }
  puts("PASS real SERVO timeout -> fresh re-unlock -> independent blade prompt/OK, history retained, second timeout still stops");
 }else if(scenario==13U){
  start();ok();completed(false);
  channelOutputs[3]=0;advance(50U);
  assert(!g_exp3_hil.waiting && !mowerUnlockProbe.cancel_pending);
  start();advance(12000U);
  assert(test_permitted() && !test_blade());
  center();assert(test_permitted() && !test_blade());
  start();ok();completed(true);center();assert(test_permitted()&&!test_blade());
  puts("PASS completed unlock retires without Cancel; unconfirmed blade hold/expiry/release preserves permission");
 }else{
  start();uint32_t old_hmi=g_exp3_hil.hmi;ok();drop_runtime=true;advance(11000U);
  assert(!g_exp3_hil.bound || g_exp3_hil.hmi!=old_hmi);
  assert(!test_permitted()&&!test_blade());
  drop_runtime=false;center();advance(10000U);center();start();ok();completed(false);
  puts("PASS Accepted without progressing runtime cannot wait forever; 10s safe session renewal and explicit retry recover");
 }
}
