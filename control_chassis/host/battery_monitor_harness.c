#include <assert.h>
#include <stdio.h>
#include "battery_monitor.h"
#include "safety_ownership_gate.h"

static BatterySample_t Sample(uint32_t tick, uint32_t cv, uint16_t soc)
{
    BatterySample_t s = {0};
    s.valid = true; s.sample_ms = tick; s.pack_cv = cv;
    s.current_da = -120; s.min_cell_mv = 3000; s.max_cell_mv = 3400;
    s.soc_percent = soc; s.fcc_cah = 10000; s.rc_cah = 2900; s.max_temp_dc = 280;
    return s;
}

static void Feed(BatteryMonitor_t *m, uint32_t tick, uint32_t cv, uint16_t soc)
{
    BatterySample_t s = Sample(tick, cv, soc);
    BatteryMonitor_Update(m, &s, tick);
}

int main(void)
{
    BatteryMonitor_t m;
    BatteryMonitor_Init(&m);
    BatteryMonitor_Update(&m,NULL,10000U);
    assert(!m.data_fresh && m.state==BATTERY_INVALID);
    assert(!m.stop_required && !m.stop_latched && !m.blade_inhibited);
    assert(m.reasons==BATTERY_REASON_DATA);
    const uint16_t socs[] = {19,20,21};
    const uint32_t volts[] = {4799,4800,4801};
    for (unsigned i=0;i<3;i++) for (unsigned j=0;j<3;j++) {
        BatteryMonitor_Init(&m);
        for (unsigned t=0;t<=2000;t+=500) Feed(&m,t,volts[j],socs[i]);
        bool low = socs[i]<20 || volts[j]<4800;
        assert(m.low_latched==low && m.blade_inhibited==low && !m.stop_required);
        assert(m.latest.soc_percent==socs[i]);
    }
    BatteryMonitor_Init(&m);
    Feed(&m,0,4700,29);
    assert(m.blade_inhibited && !m.stop_required && !m.low_latched); /* boot guard */
    for (unsigned t=500;t<=2000;t+=500) Feed(&m,t,4700,29);
    assert(m.low_latched && (m.reasons & BATTERY_REASON_PACK));
    for (unsigned t=2500;t<=4500;t+=500) Feed(&m,t,5400,100);
    assert(m.low_latched && m.blade_inhibited && !m.stop_required);
    assert(BatteryMonitor_Rearm(&m));
    assert(m.low_latched && m.blade_inhibited); /* unlock never clears LOW */

    BatteryMonitor_Init(&m);
    Feed(&m,0,5200,80); Feed(&m,500,4799,80); Feed(&m,1000,5200,80);
    assert(!m.low_latched && !m.blade_inhibited); /* transient sag */
    Feed(&m,1500,4799,80);
    BatteryMonitor_Update(&m,NULL,3500);
    assert(!m.low_latched); /* same sample cannot advance confirmation */
    Feed(&m,3500,4799,80); /* large sample gap resets the old candidate */
    assert(!m.low_latched);
    for (unsigned t=4000;t<=5500;t+=500) Feed(&m,t,4799,80);
    assert(m.low_latched && m.latest.soc_percent==80);

    BatteryMonitor_Init(&m);
    Feed(&m,UINT32_MAX-1000U,5200,19);
    Feed(&m,UINT32_MAX-500U,5200,19);
    Feed(&m,UINT32_MAX,5200,19); Feed(&m,499U,5200,19); Feed(&m,999U,5200,19);
    assert(m.low_latched && (m.reasons & BATTERY_REASON_SOC));
    BatteryMonitor_Update(&m,NULL,3998U); assert(m.data_fresh);
    BatteryMonitor_Update(&m,NULL,3999U);
    assert(!m.data_fresh && !m.stop_latched && !m.stop_required && m.blade_inhibited);
    assert((m.reasons & (BATTERY_REASON_DATA|BATTERY_REASON_SOC))==
           (BATTERY_REASON_DATA|BATTERY_REASON_SOC));
    for (unsigned t=4000;t<=6000;t+=500) Feed(&m,t,5200,80);
    assert(m.recovery_ready && !m.stop_required);
    assert(BatteryMonitor_Rearm(&m));
    assert(!m.stop_required && m.low_latched && m.blade_inhibited);

    BatteryMonitor_Init(&m); /* Only a reboot/init clears LOW. */
    assert(!m.stop_required && !m.blade_inhibited && !m.data_fresh);
    Feed(&m,0,5200,80); assert(!m.blade_inhibited && !m.low_latched);
    BatterySample_t invalid=Sample(500,5200,101);
    BatteryMonitor_Update(&m,&invalid,500);
    assert(!m.data_fresh && !m.stop_required && !m.blade_inhibited && m.state==BATTERY_INVALID);
    Feed(&m,1000,5200,80);
    BatteryMonitor_Update(&m,NULL,3999); assert(m.data_fresh);
    BatteryMonitor_Update(&m,NULL,4000);
    assert(!m.data_fresh && !m.stop_required && !m.blade_inhibited);
    Feed(&m,4500,5200,80); /* First good sample restores diagnostics immediately. */
    assert(m.data_fresh && !m.stop_required && !m.blade_inhibited && !m.reasons);
    invalid=Sample(5000,4700,19); invalid.min_cell_mv=0;
    BatteryMonitor_Update(&m,&invalid,5000);
    assert(!m.data_fresh && !m.low_latched && !m.blade_inhibited);

    BatteryMonitor_Init(&m);
    Feed(&m,0,4700,80); Feed(&m,1000,4700,80); Feed(&m,1999,4700,80);
    assert(m.blade_inhibited && !m.low_latched);
    Feed(&m,2000,4700,80); assert(m.low_latched);

    /* Feedback loss must not invalidate an already authorized command. */
    BatteryMonitor_Init(&m);
    for (unsigned owner=EXECUTION_OWNER_MANUAL_REMOTE;owner<=EXECUTION_OWNER_CRUISE;owner++) {
        SafetyOwnershipGateInput_t gate={false,false,false,true,true,(ExecutionOwner_t)owner,1,0};
        SafetyOwnershipGate_Reset(); SafetyOwnershipGate_Update(&gate);
        SafetyOwnershipGate_SetBatteryPolicy(m.stop_required,m.blade_inhibited);
        assert(SafetyOwnershipGate_Authorize((ExecutionOwner_t)owner,true));
        assert(SafetyOwnershipGate_AuthorizeBlade((ExecutionOwner_t)owner,true));
        assert(!SafetyOwnershipGate_TakeStopRequest());
    }

    for (unsigned owner=EXECUTION_OWNER_MANUAL_REMOTE;owner<=EXECUTION_OWNER_CRUISE;owner++) {
        SafetyOwnershipGateInput_t gate={false,false,false,true,true,(ExecutionOwner_t)owner,1,0};
        SafetyOwnershipGate_Reset(); SafetyOwnershipGate_Update(&gate);
        SafetyOwnershipGate_SetBatteryPolicy(false,true);
        assert(SafetyOwnershipGate_Authorize((ExecutionOwner_t)owner,true));
        assert(!SafetyOwnershipGate_AuthorizeBlade((ExecutionOwner_t)owner,true));
        assert(SafetyOwnershipGate_AuthorizeBlade((ExecutionOwner_t)owner,false));
        assert(!SafetyOwnershipGate_TakeStopRequest()); /* LOW must not stop travel */
        SafetyOwnershipGate_SetBatteryPolicy(true,true);
        assert(!SafetyOwnershipGate_Authorize((ExecutionOwner_t)owner,true));
        SafetyOwnershipGate_SetBatteryPolicy(false,true);
        assert(!SafetyOwnershipGate_Authorize((ExecutionOwner_t)owner,true));
        gate.command_sequence++; SafetyOwnershipGate_Update(&gate);
        assert(SafetyOwnershipGate_Authorize((ExecutionOwner_t)owner,true));
        assert(!SafetyOwnershipGate_AuthorizeBlade((ExecutionOwner_t)owner,true));
    }
    puts("PASS LOW: 19/20/21%, 47.99/48/48.01V, 2s, sag, frozen/gap/wrap, reboot, unlock retains blade latch, travel unchanged");
    return 0;
}
