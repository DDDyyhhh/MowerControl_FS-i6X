# Flipsky FTESC CAN blade-driver parameters

## Compile-time selection

The blade backend is independent from the wheel/chassis `MOTOR_DRIVER_USED` selector.

In `Core/Inc/blade_motor_config.h`:

```c
#define BLADE_MOTOR_DRIVER_PWM        1U
#define BLADE_MOTOR_DRIVER_FTESC_CAN  2U
#define BLADE_MOTOR_DRIVER_USED BLADE_MOTOR_DRIVER_PWM
```

`BLADE_MOTOR_DRIVER_USED` defaults to the PA1/TIM2_CH2 PWM adapter. Set it to
`BLADE_MOTOR_DRIVER_FTESC_CAN` for FTESC. Defining legacy/alternate selector
macros or selecting any other value is a compile-time error.

The common `BladeMotor_*` seam accepts the selected adapter's native command:
PWM duty percent in PWM builds, or the existing ERPM setpoint in FTESC builds.
Every nonzero command is authorized by `SafetyOwnershipGate` before the adapter
touches TIM/CAN. PWM has no separate enable GPIO: initialization and every zero
or denied command keep TIM2_CH2 enabled with `CCR2=0`, actively driving PA1 at
deterministic 0% duty. PE5 is not part of the PWM blade chain.
## FTESC protocol

- CAN frame type: 29-bit extended frame.
- Extended identifier: `(FTESC_ID << 8) | command`.
- Configured FTESC ID: `154`; keep it aligned with ESC Tool.
- Current CAN timing in `Core/Src/can.c`: approximately 500 kbit/s with the current APB1 clock; the ESC setting must match.
- Speed control command: command `0x18`, extended ID `0x00009A18`.
- DLC: 5.
- Data bytes 0..3: signed big-endian integer ERPM.
- Data byte 4: gear limit `0x00`.
- ERPM `0` is the normal stop request; stop is sent explicitly.

The existing hardware-validated ERPM levels remain 14,000, 15,500, and 16,000.

The feedback commands are enabled in FTESC configuration as required:

- `0x0B`: motor current and battery current, signed int32, scale `0.001`.
- `0x0C`: motor ERPM, signed int32, and duty, scale `0.001`.
- `0x0D`: battery voltage, motor temperature, and MOSFET temperature, signed int16, scale `0.01`.
- Feedback bytes 6..7 contain CRC16 over bytes 0..5.

## 48 V, sensorless motor commissioning

A 48 V motor rating does not define the battery maximum voltage. Configure FTESC from the real battery pack series count and measure the full-charge voltage. For reference, a 13S lithium pack is about 54.6 V full and a 14S pack is about 58.8 V full.

Before enabling the blade, record and configure:

- FTESC model and maximum input voltage.
- Actual battery series count and BMS limits.
- Motor rated current and allowable peak current.
- Battery continuous and peak current limits.
- Motor pole pairs and direction.
- Sensor mode: sensorless when neither encoder nor Hall sensors are present.
- Sensorless startup mode, startup current/duty, minimum ERPM, and open-loop ramp.
- Positive acceleration ramp and deceleration/coast behavior.
- Motor and MOSFET temperature limits.
- CAN command-loss timeout.

Do not copy generic current limits into this firmware. The correct limits depend on the exact motor, ESC, battery, BMS, wiring, and blade load. Start unloaded and increase limits gradually while recording current and temperature.

A sensorless motor has poor observability at zero and low ERPM. It can fail to start, lose synchronism, or stall under a heavy blade load. This backend intentionally does not use position control because the motor has no encoder. Do not send reverse or negative brake commands during initial testing. Regenerative braking on a 48 V battery or a DC supply can raise bus voltage; use coast/zero-current stop first and configure regen/brake limits conservatively.

The firmware continues to send normal application stop commands on collision, emergency, tilt, communication loss, and lockout. The FTESC CAN watchdog and a physical emergency-disconnect path must be tested independently before installing blades.
