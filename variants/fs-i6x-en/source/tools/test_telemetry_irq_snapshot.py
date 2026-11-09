#!/usr/bin/env python3
"""Simulate an ISR reader preempting the real snapshot publication writer."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=r'''
#include <assert.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include "SOURCE"
static void stalled(int sig) { (void)sig; _exit(2); }
int main(void) {
 TelemetrySources_t s={0};TelemetrySnapshot_t out;
 TelemetrySnapshot_Update(&s,1000U);
 s.authorization=mower_auth_pack(MOWER_AUTH_CONFIRM,true,4095U);
 TelemetrySnapshot_Update(&s,1100U);
 uint32_t expected=TelemetrySnapshot_ReadFastAuthorization();
 assert((expected>>16)==s.authorization);
 g_publication.sequence |= 1U; /* ISR interrupts the writer at its publish barrier. */
 signal(SIGALRM,stalled);alarm(1);
 assert(!TelemetrySnapshot_Read(&out));
 assert(TelemetrySnapshot_ReadFastAuthorization()==expected);
 alarm(0);g_publication.sequence++;
 assert(TelemetrySnapshot_Read(&out));
 TelemetryWheelDiagnostics_t diag;
 g_publication.diag_sequence |= 1U;
 assert(TelemetrySnapshot_Read(&out)); /* diagnostic publication cannot stall SENS */
 assert(!TelemetrySnapshot_ReadWheelDiagnostics(&diag));
 puts("PASS ISR telemetry read never spins on a preempted writer");
}
'''.replace('SOURCE',str(root/'control_chassis/Core/UserApp/telemetry_snapshot.c'))
with tempfile.TemporaryDirectory(prefix='mower-irq-snapshot-') as d:
 exe=str(Path(d)/'probe')
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I'+str(root/'control_chassis/Core/Inc'),'-x','c','-','-o',exe],input=source,text=True,check=True)
 p=subprocess.run([exe],timeout=2)
 if p.returncode==2: raise SystemExit('FAIL ISR snapshot reader spins while its writer is preempted')
 if p.returncode: raise SystemExit(p.returncode)
