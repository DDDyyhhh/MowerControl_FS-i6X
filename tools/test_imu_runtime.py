#!/usr/bin/env python3
"""Exercise production IMU startup/runtime and the Motion sampling call site."""
from pathlib import Path
import subprocess
import tempfile
import os
from test_imu_fault import MAIN, CORE, ROOT

source=(CORE/'UserApp/app_Motion.c').read_text()
start=source.index('    // === 3. IMU')
end=source.index('#if MOWER_GRASS_BAG_CH6_ENABLED',start)
step=source[start:end]
TEST=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app_imu.h"
#include "bsp_imu_solver.h"
static uint32_t tick, init_calls, reads, solve_calls, init_pose_calls, gates, stops;
static int8_t init_status, read_status;
static unsigned bad_samples, sample_cost, fail_every;
static float last_dt;
static icm42688RealData_t next_a={0,0,1},next_g={0.1f,-0.2f,0.3f};
static struct {
 AppImu_t imu;icm42688RealData_t accval,gyroval;uint32_t last_imu_tick;
 uint8_t is_on_slope, lock, physical_safety_latched;
} g_app;
uint32_t HAL_GetTick(void){return tick;}
void osDelay(uint32_t n){tick+=n;}
int8_t bsp_Icm42688Init(void){++init_calls;return init_status;}
int8_t bsp_IcmGetRawData(icm42688RealData_t*a,icm42688RealData_t*g){
 ++reads;tick+=sample_cost;
 if(read_status)return read_status;
 if(bad_samples){--bad_samples;return 3;}
 if(fail_every&&reads%fail_every==0)return 2;
 *a=next_a;*g=next_g;return 0;
}
void RealSolverInit(float,float,float);
void RealSolverUpdate(float,float,float,float,float,float,float);
void IMU_Solver_Init(float x,float y,float z){++init_pose_calls;RealSolverInit(x,y,z);}
void IMU_Solver_Update(float x,float y,float z,float a,float b,float c,float dt){
 ++solve_calls;last_dt=dt;RealSolverUpdate(x,y,z,a,b,c,dt);
}
static void RefreshSafetyOwnershipGate(void){++gates;}
static void EnforceSafetyOwnershipStop(void){++stops;}
static void MotionStep(void){
@STEP@
}
static void Reset(void){
 memset(&g_app,0,sizeof(g_app));memset(&imu_data,0,sizeof(imu_data));imu_data.q0=1;
 tick=init_calls=reads=solve_calls=init_pose_calls=gates=stops=0;
 init_status=read_status=0;bad_samples=sample_cost=fail_every=0;
 next_a=(icm42688RealData_t){0,0,1};next_g=(icm42688RealData_t){0.1f,-0.2f,0.3f};
 g_app.is_on_slope=1;
}
static void Startup(void){AppImu_Init(&g_app.imu,&g_app.accval,&g_app.gyroval);}
int main(void){
 Reset();init_status=3;Startup();
 assert(g_app.imu.state==APP_IMU_INIT_FAILED&&!g_app.imu.pose_available&&reads==0);
 MotionStep();assert(reads==0&&init_calls==1&&gates==1&&stops==1&&g_app.is_on_slope==1);
 Reset();read_status=3;sample_cost=2;Startup();
 assert(g_app.imu.state==APP_IMU_CALIBRATION_FAILED&&tick<=2102&&reads>0);
 assert(!g_app.imu.calibrated&&!g_app.imu.pose_available&&init_pose_calls==0);
 unsigned old_reads=reads;read_status=0;MotionStep();assert(reads==old_reads&&init_calls==1);
 Reset();bad_samples=20;Startup();
 assert(g_app.imu.state==APP_IMU_READY&&g_app.imu.calibration_samples==200&&reads==220);
 assert(g_app.imu.failed_reads==20&&tick<2000&&init_pose_calls==1);
 assert(fabsf(imu_data.gyro_offset_x-0.1f)<0.00001f);
 assert(fabsf(imu_data.gyro_offset_y+0.2f)<0.00001f);
 assert(fabsf(imu_data.gyro_offset_z-0.3f)<0.00001f);
 /* Mixed failures never divide a partial sum by 200 and claim calibration. */
 Reset();fail_every=2;sample_cost=1;Startup();
 assert(g_app.imu.state==APP_IMU_CALIBRATION_FAILED&&g_app.imu.calibration_samples<200);
 assert(!g_app.imu.has_sample&&init_pose_calls==0);
 Reset();Startup();
 uint32_t last=g_app.imu.last_valid_ms;
 IMU_Data_t pose=imu_data;icm42688RealData_t old_a=g_app.accval;
 read_status=3;sample_cost=2;tick=last+97;MotionStep();
 assert(g_app.imu.pose_available&&g_app.imu.last_valid_ms==last&&solve_calls==0);
 assert(memcmp(&pose,&imu_data,sizeof(pose))==0&&memcmp(&old_a,&g_app.accval,sizeof(old_a))==0);
 sample_cost=0;tick=last+100;MotionStep();
 assert(!g_app.imu.pose_available&&g_app.imu.sample_max_ms==2);
 assert(gates==2&&stops==2&&g_app.lock==0&&g_app.physical_safety_latched==0);
 g_app.lock=g_app.physical_safety_latched=1;MotionStep();
 assert(g_app.lock==1&&g_app.physical_safety_latched==1);
 /* Recovery at 100ms reinitializes instead of integrating the missing interval. */
 next_a=(icm42688RealData_t){0.34202014f,0,0.93969262f};read_status=0;MotionStep();
 assert(init_pose_calls==2&&solve_calls==0&&g_app.imu.pose_available&&g_app.is_on_slope==1);
 assert(fabsf(imu_data.pitch+20)<0.1f&&fabsf(imu_data.roll)<0.01f);
 assert(g_app.imu.consecutive_failures==0&&g_app.imu.last_error==0);
 tick+=10;MotionStep();assert(solve_calls==1&&fabsf(last_dt-0.01f)<0.00001f);
 last=g_app.imu.last_valid_ms;read_status=1;tick+=10;MotionStep();
 read_status=0;tick+=10;MotionStep();assert(fabsf(last_dt-0.02f)<0.00001f);
 /* A successful sample just below the freshness boundary uses elapsed dt. */
 tick+=99;MotionStep();assert(fabsf(last_dt-0.099f)<0.00001f);
 /* Invalid successful samples also leave the solver untouched. */
 next_g.x=NAN;pose=imu_data;last=g_app.imu.last_valid_ms;tick+=10;MotionStep();
 assert(g_app.imu.last_error==ICM_ERROR_DATA&&g_app.imu.last_valid_ms==last);
 assert(memcmp(&pose,&imu_data,sizeof(pose))==0);
 next_g.x=0.1f;next_a=(icm42688RealData_t){0};tick+=10;MotionStep();
 assert(g_app.imu.last_error==ICM_ERROR_DATA);
 /* Wraparound age, including recovery exactly at 100 ms. */
 Reset();Startup();g_app.imu.last_valid_ms=UINT32_MAX-49;tick=49;read_status=3;MotionStep();
 assert(g_app.imu.pose_available);tick=50;MotionStep();assert(!g_app.imu.pose_available);
 read_status=0;MotionStep();assert(init_pose_calls==2&&g_app.imu.last_valid_ms==50);
 AppImu_RecordCycle(&g_app.imu,UINT32_MAX-4,4);assert(g_app.imu.motion_overruns==0);
 AppImu_RecordCycle(&g_app.imu,UINT32_MAX-4,5);assert(g_app.imu.motion_overruns==1&&g_app.imu.motion_max_ms==10);
 g_app.imu.motion_overruns=UINT32_MAX;AppImu_RecordCycle(&g_app.imu,0,11);assert(g_app.imu.motion_overruns==UINT32_MAX);
 /* Startup deadline also works across clock wrap. */
 Reset();tick=UINT32_MAX-500;read_status=3;uint32_t begin=tick;Startup();
 assert((uint32_t)(tick-begin)==2100&&g_app.imu.state==APP_IMU_CALIBRATION_FAILED);
 puts("PASS IMU runtime: init/calibration failure, valid-only averages, Motion always advances safety, freshness/recovery/wrap, diagnostics only");
}
'''
with tempfile.TemporaryDirectory(prefix='imu-runtime-') as td:
 p=Path(td)
 for name in ['app_imu.h','bsp_imu_solver.h','icm42688.h']:(p/name).write_bytes((CORE/'Inc'/name).read_bytes())
 (p/'main.h').write_text(MAIN)
 (p/'cmsis_os.h').write_text('#include <stdint.h>\nvoid osDelay(uint32_t);\n')
 (p/'test.c').write_text(TEST.replace('@STEP@',step))
 for opt in ['-O0','-O2']:
  base=[os.environ.get('CC','cc'),'-std=c99',opt,'-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-I'+str(p)]
  subprocess.run(base+['-DIMU_Solver_Init=RealSolverInit','-DIMU_Solver_Update=RealSolverUpdate','-c',str(CORE/'UserDev/bsp_imu_solver.c'),'-o',str(p/'solver.o')],check=True)
  subprocess.run(base+[str(p/'test.c'),str(CORE/'UserApp/app_imu.c'),str(p/'solver.o'),'-lm','-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True,timeout=10)
