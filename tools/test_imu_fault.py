#!/usr/bin/env python3
"""Compile real IMU driver with deterministic SPI failures (no board required)."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'control_chassis/Core'
MAIN = '''#pragma once
#include <stdint.h>
typedef struct {int unused;} SPI_HandleTypeDef;
typedef enum {HAL_OK=0, HAL_ERROR=1, HAL_BUSY=2, HAL_TIMEOUT=3} HAL_StatusTypeDef;
#define GPIOA 0
#define GPIO_PIN_4 4
#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET 1
#define __NOP() ((void)0)
void HAL_GPIO_WritePin(int,int,int);
void HAL_Delay(uint32_t);
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef*,const uint8_t*,uint8_t*,uint16_t,uint32_t);
'''
DRIVER_TEST = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "icm42688.h"
#include "main.h"
SPI_HandleTypeDef hspi1;
static unsigned calls, budget, tick, size_seen, cs=1;
static HAL_StatusTypeDef status=HAL_OK;
static unsigned fail_at, fill=0x55, partial;
static uint8_t address_seen, written[128], custom_data[12];
static unsigned use_custom, bad_id;
void HAL_GPIO_WritePin(int a,int b,int c){(void)a;(void)b;cs=(unsigned)c;}
void HAL_Delay(uint32_t n){tick+=n;}
uint32_t HAL_GetTick(void){return tick;}
void Serial_Printf(int p,const char*s,...){(void)p;(void)s;}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef*h,const uint8_t*t,uint8_t*r,uint16_t n,uint32_t timeout){
 (void)h; assert(cs==0); ++calls; budget+=timeout; size_seen=n; address_seen=t[0];
 memset(r,fill,partial?n/2:n);
 if(n==2 && !(t[0]&0x80)) written[t[0]]=t[1];
 if(use_custom && n==13)memcpy(r+1,custom_data,12);
 if(!bad_id && (t[0]&0x80)){
#if ICM42688_TDK_DRIVER
 if((t[0]&0x7f)==ICM42688_WHO_AM_I && n>1)r[1]=0x47;
#else
 if((t[0]&0x7f)==ICM_REG_WHO_AM_I && n>1)r[1]=0x6a;
#endif
 }
 if(status!=HAL_OK && (!fail_at || calls==fail_at)){tick+=timeout;return status;}
 return HAL_OK;
}
static void reset_io(void){calls=budget=0;partial=0;fill=0x55;status=HAL_OK;fail_at=0;use_custom=bad_id=0;}
int main(void){
 icm42688RealData_t a,g;
 /* A timeout with nonzero/partial data must never become a successful sample. */
 for(unsigned s=1;s<=3;s++)for(unsigned p=0;p<2;p++){
  reset_io(); status=(HAL_StatusTypeDef)s; partial=p;
  a=(icm42688RealData_t){11,22,33};g=(icm42688RealData_t){44,55,66};
  int rc=bsp_IcmGetRawData(&a,&g);
  printf("failure=%u partial=%u calls=%u budget=%u rc=%d\n",s,p,calls,budget,rc);fflush(stdout);
  assert(rc!=0 && calls==1 && budget==2 && cs==1);
  assert(a.x==11&&a.y==22&&a.z==33&&g.x==44&&g.y==55&&g.z==66);
 }
 reset_io();assert(bsp_Icm42688Init()==0);unsigned init_calls=calls;
#if ICM42688_TDK_DRIVER
 assert(written[ICM42688_ACCEL_CONFIG0]==((AFS_4G<<5)|AODR_100Hz));
 assert(written[ICM42688_GYRO_CONFIG0]==((GFS_1000DPS<<5)|GODR_100Hz));
#else
 assert(written[ICM_REG_ACC_CONF]==0x84&&written[ICM_REG_GYR_CONF]==0x84);
 assert(written[ICM_REG_ACC_RANGE]==ICM_ACC_RANGE_4G&&written[ICM_REG_GYR_RANGE]==ICM_GYR_RANGE_2000DPS);
 assert(written[ICM_REG_PWR_CTRL]==0x0e);
#endif
 reset_io();bad_id=1;unsigned start_tick=tick;assert(bsp_Icm42688Init()!=0&&cs==1&&tick-start_tick<2200);

 /* Fail each init transaction, including config writes; never claim success. */
 for(unsigned at=1;at<=init_calls;at++){
  reset_io();status=HAL_TIMEOUT;fail_at=at;
  assert(bsp_Icm42688Init()!=0 && calls==at && cs==1);
 }
 reset_io();assert(bsp_Icm42688Init()==0);
 reset_io(); assert(bsp_IcmGetRawData(&a,&g)==0);
 assert(calls==1&&budget==2&&size_seen==13&&cs==1);
#if ICM42688_TDK_DRIVER
 assert(address_seen==(ICM42688_ACCEL_DATA_X1|0x80));
 assert(fabsf(a.x-((float)0x5555*4000.0f/32768.0f))<0.001f);
#else
 assert(address_seen==(ICM_REG_ACC_X_H|0x80));
 assert(fabsf(a.x-((float)0x5555*4.0f/32768.0f))<0.00001f);
#endif
 for(unsigned f=0;f<=255;f+=255){
  reset_io();fill=f;a.x=11;g.x=44;
  assert(bsp_IcmGetRawData(&a,&g)!=0&&a.x==11&&g.x==44&&cs==1);
 }
 reset_io();use_custom=1;memset(custom_data,0,sizeof(custom_data));
 custom_data[0]=0xff;custom_data[1]=0x00; /* -256, signed big endian. */
 custom_data[4]=0x20; /* Nonzero acceleration, stationary zero gyro is valid. */
 assert(bsp_IcmGetRawData(&a,&g)==0&&a.x<0&&g.x==0&&g.y==0&&g.z==0);
 reset_io();fill=0;int16_t zero_temp=0;icm42688RawData_t zero_g={1,2,3};
 assert(bsp_IcmGetGyroscope(&zero_g)==0&&zero_g.x==0&&zero_g.y==0&&zero_g.z==0);
 assert(bsp_IcmGetTemperature(&zero_temp)==0);
#if ICM42688_TDK_DRIVER
 assert(zero_temp==25);
#else
 assert(zero_temp==23);
#endif
 reset_io();assert(bsp_IcmGetRawData(NULL,&g)!=0&&calls==0);
 assert(bsp_IcmGetAccelerometer(NULL)!=0&&bsp_IcmGetGyroscope(NULL)!=0&&bsp_IcmGetTemperature(NULL)!=0);
 for(unsigned s=1;s<=3;s++){
  icm42688RawData_t raw={11,22,33};int16_t temp=99;
  reset_io();status=(HAL_StatusTypeDef)s;
  assert(bsp_IcmGetAccelerometer(&raw)!=0&&raw.x==11&&raw.y==22&&raw.z==33&&cs==1);
  assert(bsp_IcmGetGyroscope(&raw)!=0&&raw.x==11&&raw.y==22&&raw.z==33&&cs==1);
  assert(bsp_IcmGetTemperature(&temp)!=0&&temp==99&&cs==1);
 }
 puts("PASS IMU SPI transaction, error propagation, output commit, invalid data, initialization");
 return 0;
}
'''

def run():
    with tempfile.TemporaryDirectory(prefix='imu-fault-') as td:
        p=Path(td)
        for name in ['icm42688.h', 'bsp_imu_solver.h']:
            (p/name).write_bytes((CORE/'Inc'/name).read_bytes())
        (p/'main.h').write_text(MAIN)
        (p/'spi.h').write_text('#include "main.h"\nextern SPI_HandleTypeDef hspi1;\n')
        (p/'serial_port.h').write_text('#define SERIAL_PORT_2 2\nvoid Serial_Printf(int,const char*,...);\n')
        (p/'driver.c').write_text(DRIVER_TEST)
        for opt in ['-O0','-O2']:
            for tdk in [0,1]:
                cmd=[os.environ.get('CC','cc'),'-std=c99',opt,'-Wall','-Wextra','-Werror',
                     '-DICM42688_TDK_DRIVER='+str(tdk),'-DICM42688_HXY_DRIVER='+str(1-tdk),
                     '-I'+str(p),str(p/'driver.c'),str(CORE/'UserDev/icm42688.c'),'-lm','-o',str(p/'test')]
                subprocess.run(cmd,check=True)
                subprocess.run([str(p/'test')],check=True,timeout=10)

if __name__=='__main__':run()
