#!/usr/bin/env python3
"""Run the actual firmware's first live-sensor call/consumer in Thumb emulation.

Requires Unicorn (can be installed in a temporary PYTHONPATH) and Arm objdump/nm.
The fixture models the unchanged I6X EEPROM v223/telemetry RAM ABI, not the C
implementation of mowerSensor. No hardware access; no source is reimplemented.
"""
import argparse
import re
import struct
import subprocess
from pathlib import Path
try:
    from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_MEM_READ
except ImportError:
    raise SystemExit("ARM firmware regression requires Unicorn; install it in a temporary PYTHONPATH (see radio boot-fix release README).")
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_SP, UC_ARM_REG_PC

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('elf',type=Path);p.add_argument('image',type=Path)
a=p.parse_args()
nm=subprocess.check_output(['arm-none-eabi-nm','-C',str(a.elf)],text=True)
symbols={parts[2]:int(parts[0],16) for line in nm.splitlines() if len(parts:=line.split(maxsplit=2))==3 and re.fullmatch('[0-9a-fA-F]+',parts[0])}
dis=subprocess.check_output(['arm-none-eabi-objdump','-d','-C',str(a.elf)],text=True)
menu=re.search(r'^[0-9a-f]+ <menuMainView\(unsigned char\)>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)',dis,re.M|re.S)[1]
call=re.search(r'^\s*([0-9a-f]+):.*\bbl\s+[0-9a-f]+ <mowerSensor\(',menu,re.M)
assert call, 'Live telemetry accessor must remain available for target regression'
address=int(call[1],16)
# In these target images the snapshot is in r4, and the accessor's local output
# pointer (where applicable) in r5/r1. Stop after its first voltage-field store.
consumer=re.search(r'^\s*([0-9a-f]+):.*\bstrh\s+\w+, \[r4, #4\]',menu[call.end():],re.M)
assert consumer, 'Update fixture if compiler changes the snapshot register'
end=int(consumer[1],16)+2
for value in (0,1234,65535):
 uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB)
 uc.mem_map(0x08000000,0x20000);uc.mem_write(0x08000000,a.image.read_bytes())
 # M0 boots with the Flash alias at zero; trap its unsupported unaligned loads.
 uc.mem_map(0,0x20000);uc.mem_write(0,a.image.read_bytes())
 uc.mem_map(0x20000000,0x10000)
 uc.mem_write(symbols['telemetryData'],b'\x01'*16)
 if 'telemetryStreaming' in symbols:uc.mem_write(symbols['telemetryStreaming'],b'\x01')
 # v223 packed TelemetrySensor records: 14 bytes, id at +1, custom type at +9.
 sensors=symbols['g_model']+0x938
 for i in range(40):uc.mem_write(symbols['telemetryItems']+i*32+12,b'\xff')
 uc.mem_write(sensors+1,struct.pack('<H',3))
 uc.mem_write(symbols['telemetryItems'],struct.pack('<I',value))
 uc.mem_write(symbols['telemetryItems']+12,b'\x00')
 output=0x20003e00;snapshot=0x20003c00
 uc.reg_write(UC_ARM_REG_SP,0x20003d00)
 uc.reg_write(UC_ARM_REG_R0,3);uc.reg_write(UC_ARM_REG_R1,output)
 uc.reg_write(UC_ARM_REG_R4,snapshot);uc.reg_write(UC_ARM_REG_R5,output)
 # Preserve the caller's own argument backing register where it used one.
 # v35 passes the output pointer from r7; affected v36 uses caller-saved r1.
 backing=re.findall(r'\bmovs?\s+r1, r([4-7])\b','\n'.join(menu[:call.start()].splitlines()[-12:]))
 if backing:
  uc.reg_write({4:UC_ARM_REG_R4,5:UC_ARM_REG_R5,6:UC_ARM_REG_R6,7:UC_ARM_REG_R7}[int(backing[-1])],output)
 faults=[]
 def aligned(cpu,access,where,size,data,unused):
  if size in (2,4) and where%size:
   faults.append(f'Cortex-M0 unaligned {size}-byte read at {where:#x}, PC={cpu.reg_read(UC_ARM_REG_PC):#x}')
   cpu.emu_stop()
 uc.hook_add(UC_HOOK_MEM_READ,aligned)
 try:uc.emu_start(address|1,end,count=10000)
 except (UcError,RuntimeError) as error:
  raise SystemExit(f'FAIL actual Thumb sensor path: {error}; PC={uc.reg_read(UC_ARM_REG_PC):#x}; r1={uc.reg_read(UC_ARM_REG_R1):#x}')
 if faults:raise SystemExit('FAIL actual Thumb sensor path: '+faults[0])
 actual=struct.unpack('<H',uc.mem_read(snapshot+4,2))[0]
 assert actual==value,(value,actual)
 print(f'PASS actual Thumb live voltage={value}; snapshot={actual}')
