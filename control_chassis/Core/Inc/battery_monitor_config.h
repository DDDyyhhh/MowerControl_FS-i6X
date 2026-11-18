#ifndef BATTERY_MONITOR_CONFIG_H
#define BATTERY_MONITOR_CONFIG_H

/* Raw BMS SOC and pack voltage are independent low-battery conditions.
 * Equality is healthy. These limits inhibit blades, never travel speed. */
#define BATTERY_LOW_SOC_PERCENT 20U
#define BATTERY_LOW_PACK_CV 4800U
#define BATTERY_LOW_CONFIRM_MS 2000U
#define BATTERY_DATA_RECOVERY_MS 2000U

#endif
