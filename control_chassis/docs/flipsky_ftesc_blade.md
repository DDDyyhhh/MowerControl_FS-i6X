# Flipsky FTESC CAN blade-driver parameters

## Compile-time selection

The blade backend is independent from the wheel/chassis `MOTOR_DRIVER_USED` selector.

In `Core/Inc/blade_motor_config.h`:

```c
#define BLADE_MOTOR_DRIVER_PWM        1U
#define BLADE_MOTOR_DRIVER_FTESC_CAN  2U
#define BLADE_MOTOR_DRIVER_USED BLADE_MOTOR_DRIVER_FTESC_CAN
```

`BLADE_MOTOR_DRIVER_USED` defaults to the FTESC CAN adapter. An explicit compiler
definition of `BLADE_MOTOR_DRIVER_USED=BLADE_MOTOR_DRIVER_PWM` selects the
PA1/TIM2_CH2 PWM adapter for a separate PWM build. Defining legacy/alternate
selector macros or selecting any other value is a compile-time error.

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
These are electrical RPM setpoints, not measured mechanical blade RPM. CH7
2000/1500/1000 us selects low/medium/high respectively. CH4 start plus the
matching remote OK confirmation and `SafetyOwnershipGate` authorization remain
required. An unchanged command is refreshed every 200 ms; a changed command,
including zero after denied authorization, is sent immediately on the next
adapter call. `BladeMotor_Stop()` sends an explicit zero unconditionally.

## Reliable blade transmission

`can_tx_scheduler` owns accepted blade requests and all three CAN TX mailboxes.
CANTx alone advances delivery; Motion publishes blade intent. Initialization runs
in `User_CAN_Init` before tasks, and `Flipsky_CAN_Init` only resets feedback.
Mailbox 2 is reserved for blade commands; wheel control, queries, telemetry and
ordinary FIFO traffic use mailboxes 0/1 with their existing priority order.
ERPM/current/duty commands all use the dedicated blade interface; the ordinary
extended-frame enqueue rejects these IDs for this ESC. Wire formats are unchanged.

A zero request replaces queued nonzero intent and stays pending until its own
TXOK. Repeated zeros keep its original identity and age. While it is pending,
nonzero submissions fail and are not saved for automatic replay. `BladeMotor`
updates its suppression cache only after acceptance. The 200ms keepalive is not
the retry mechanism: submission errors, arbitration loss and transmission errors
retain the blade request, with retries no earlier than the following millisecond.
Nonzero intent expires at age 250ms without refreshing its age on retry. A blade
attempt still pending after 20ms is explicitly aborted before retry/replacement.

Each CANTx step settles mailbox results before reuse and aborts only obsolete
blade work. An already transmitting old run may finish, but zero is submitted
after its result; old nonzero work cannot follow that stop. The STM32 port selects
mailboxes explicitly, polls RQCP/TXOK/ALST/TERR without TX interrupts, and writes
only the intended RQCP or ABRQ bits rather than read-modify-writing TSR. The HAL
library and automatic retransmission/Bus-Off configuration are unchanged.
See [STM32 RM0090](https://www.st.com/resource/en/reference_manual/rm0090-stm32f407-advanced-armbased-32bit-mcus-stmicroelectronics.pdf),
bxCAN transmission handling and CAN_TSR, for completion and abort semantics.

`CAN_TxScheduler_ReadBladeDiagnostics` is a read-only view of stop pending/age,
mailbox activity, failures, retries, aborts, expiry and last CAN success. It does
not grant authorization or certify ESC execution. Hardware acceptance separately
measures request-to-CAN-success, request-to-physical-stop and Motion period under
load; see the local `blade-stop-reliable-tx` hardware ticket.

This backend switch does not validate realtime feedback:
`FLIPSKY_REALTIME_FEEDBACK_VALIDATED` remains `0`. The remote output-enabled
indicator describes the permitted output command, not actual motor rotation.
The 500 ms feedback freshness setting is not an ESC command-loss watchdog;
configure and test that watchdog on the FTESC itself. CAN interruption must not
be treated as a confirmed software safety stop or guaranteed fresh-start latch.

The current application defaults to `VEHICLE_ESTOP_ENABLED=1`: the PD1 vehicle
emergency-stop software path is enabled and active high. PD1 is an input with
an internal pull-up: normal operation requires a low level, and high/open input
triggers the existing latched stop. Verify the actual button wiring and signal
levels before enabling power. Initial commissioning still requires an independent
power-disconnect path. Revalidate actual direction, starting, stopping,
command-loss behavior and recovery on the newly configured hardware.

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
