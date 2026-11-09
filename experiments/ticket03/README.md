# Ticket 03 Experiment 3 — isolated bidirectional messages

Status: **host PASS; matching ARM images built; PWM bench functional checks and
6-7 second Confirm/Cancel waits reported PASS by the field operator (2026-09-30)**.
This is bench evidence, not production release acceptance. Complete detailed-log
capture without ring overflow and production integration acceptance remain outstanding.
The base Experiment3 image remains an experimental transport without an actuator entry
point. A separate PWM bench-test image adds a CH4/OK test UI and a tightly gated adapter
to the existing BladeManualControl path. It does not change SafetyOwnershipGate or
RemoteAuthorizationFSM, and CH6 confirmation is disabled in that image.
`EXP3_ENABLED=0` and `PCBI6X_EXP3=OFF` are the defaults. No EEPROM/model edits.

## Structure and experimental encoding

- `exp3.c/.h`: explicit byte codec, CRC, bounded reassembly, test endpoint.
- `exp3_link.c/.h`: channel/word adaptation and one in-flight transmission.
- `exp3_hil.c/.h`: test-only mailbox, matching result/RTT, bounded diagnostic ring.
- `exp3_auto_session.c/.h`: PWM bench-only five-word boot identity exchange; no debugger
  session provisioning for the paired PWM images.
- `stm32_adapter.c`: Task_Comm owns protocol state; USART2 ISR captures only raw SENS bytes.
- `openi6x_adapter.c`: RF ISR copies at most 37 bytes into a two-slot SPSC queue;
  `telemetryWakeup()` performs protocol processing. ISR reads published CH11/12 values.
- `fixture.py`: optional live capture; base Experiment3 images still use debugger session
  provisioning. The PWM bench images establish their session on the wire.

All sizes/deadlines below are **experiment parameters**, not a frozen production wire profile.

| Complete message bytes | Meaning |
| --- | --- |
| 0, 1 | independent experiment marker `E3`, experiment format/version `31` |
| 2, 3 | kind (Confirm=1, Cancel=2, Query=3, Result=4), sender (HMI=1/controller=2) |
| 4..27 | six LE uint32: controller session, HMI session, request, action, context, generation |
| 28..29 | LE uint16 target |
| 30, 31 | result status, echoed command kind (both zero in a command) |
| 32..35 | CRC-32/ISO-HDLC of bytes 0..31 |

Statuses: Accepted=1 (test record only), Cancelled=2, Unknown=3, Stale=4,
Conflict=5, Session=6. Sessions, request, action, generation and target are nonzero.
Context/target are opaque test data, never actual actuator permission.
CRC detects corruption; it is **not authentication** and does not eliminate collision risk.

Each 18-byte fragment contains transfer uint32 LE, index uint8, count uint8 (=5),
8 payload bytes, CRC32 of the preceding 14 bytes. Last fragment has four zero padding bytes.
Each fragment is serialized as 18 uint16 words: `0xA000 | byte_index << 8 | byte`.
Thus one complete message needs **90 words**. Fragment order is strict; loss/reordering
fails closed; identical already-seen fragments are ignored, conflicting duplicates abort.
Transfer IDs increase without wrap. Expired/rejected/completed IDs remain retired in
that receiver instance; retries use a new transfer ID and the same logical identity.
A complete message CRC protects against mixing fragment payloads from different sessions.

Uplink CH11=`1000+3*(word&255)`, CH12=`1000+3*(word>>8)`; range 1000..1765.
1500/1500 is not a valid encoded word. Static/repeated words cannot form a complete message.
Only STICKS packet CH11/12 are replaced; the PWM bench image also uses them for its
test-only boot exchange before application binding. CH1–10,
failsafe packets, addr1–8 and their telemetry mapping are unchanged.
Downlink addr9 retains raw sensor ID E1/instance9 and publishes one word per measurement.
Idle is word zero (invalid application transport). Old fixed A59C is not a valid complete message.

Initial dwell: uplink 20 ms/word, downlink 250 ms/word. Assembly and result wait are
60 s experiment limits. Duplicates do not extend assembly time. Neither dwell nor 60 s
is a production safety deadline or single-direction loss policy. RF repeats/drops may
require different dwell or additional new addresses. Application messages do not retry
automatically; the PWM bench boot identity advertisement repeats until paired.

The PWM bench controller now sends a lossless short RESULT on addr9 when context is
`EXP3_PWM_CONTEXT`, target is 1, request equals action, and action/generation both fit
16 bits. Its 17 words are `0xE000 | slot << 8 | byte`, with strict slots 0..16.
Bytes 0..3 and 4..7 retain the full 32-bit controller and HMI boot identities;
8..9 carry action/request, 10..11 generation (LE16), 12 packs status in the low nibble
and command in the high nibble, and 13..16 retain the original full-message CRC32.
The receiver expands the implied fixed fields and zero upper identity bits, then runs
the existing full 36-byte decoder and exact pending-message matcher. This is not a
truncated CRC acknowledgement or local UI success prediction. Unsupported/wide values
fall back to the original 90-word result without truncation. Only the opt-in PWM bench
controller enables short result transmission; Confirm/Cancel uplink and normal safety
checks are unchanged. The base Experiment3 images still transmit full results.

Keeping 250 ms per downlink word avoids reducing the dwell below the measured RF refresh
margin. With the short result and a host model sampling addr9 every 120 ms, Confirm and
Cancel round trips are about 5.9 seconds rather than about 24 seconds. Actual RF loss and
scheduler delays can increase that time; 60 s remains the failure timeout. These are
experimental transport measurements, not a production response-time guarantee.

Endpoint state has four result/tombstone records, a session pair, and an action high-water.
Within a session, new actions must increase (no wrap); out-of-order lower actions reject.
Query never inserts a record or advances the watermark. Cancel-before-Confirm leaves a
Cancelled record. Cached duplicates preserve the result; changed request/target/context/
generation under the same action returns Conflict. Evicted action IDs remain Stale for
Confirm/Cancel; queries of missing/evicted entries return Unknown. This deliberately
conservative experiment rule does not resolve production G1/G2.

## Host test matrix

All following rows PASS through `./tools/verify.sh host` (10 suites total):

| Seam | Cases |
| --- | --- |
| Codec | identity round-trip; independent Python struct/zlib vs C golden vectors for all four kinds; bad version, truncation, corruption |
| Reassembly | complete, missing fragment, out-of-order, duplicate, conflicting duplicate, mixed-session payloads, old transfer replay, timeout, uint32 clock wrap |
| Endpoint | Confirm once; duplicate Confirm; Query-before-Confirm Unknown; Cancel-before-Confirm; Cancel-after-Confirm; conflicting identity; old session; session change; cache eviction; action exhaustion |
| Channel/word | end-to-end serialized message; static/repeated words; 1500/1500 rejection |
| Paired HIL | lost uplink; lost downlink; timeout; retry after lost result creates no second action; mid-message failsafe; controller reboot unbound; paired fresh-session recovery; old-session replay rejection |
| Automatic PWM pairing | no fixture arm; five-word boot exchange through both adapters; loss/retry; static 1500/1500; out-of-order/CRC rejection; HMI/controller reboot and session replacement; old application result cleared; full Confirm→Result after pairing |
| Diagnostics | ring saturation/visible overflow; simulated live drain with zero overflow; matching result RTT from HMI clock |
| Real adapters on host | AA/AC RF layout and truncated bounds; actual SENS responder addr9 discovery/type/value; addr1–8 unchanged discovery/measurements |
| Disabled | no core symbols; full normal chassis build; OpenI6X disabled image has no exp3 symbols and retains 123992 B Flash / 15020 B RAM |

C99 `-Wall -Wextra -Werror`, ASan and UBSan pass. LeakSanitizer is disabled because this
sandbox uses ptrace; the implementation performs no dynamic allocation.
`./tools/verify.sh all` passes. SVN preflight passes with existing workspace warnings
(build artifacts, broad pre-existing app_Comm diff and high-risk files); manifest check passes.
There are no new production .c files in Core: CMake includes experiment sources only in
the opt-in Experiment3 presets, leaving normal firmware builds unchanged.

## Resource measurements (2026-09-29, CMake controller build)

Same toolchain/config options, enabled vs disabled linked targets:

| Target | Disabled Flash / RAM | Experiment Flash / RAM | Delta Flash / BSS |
| --- | --- | --- | --- |
| OpenI6X | 123992 / 15020 B | 126356 / 15652 B | +2364 / +632 B |
| STM32 | 75040 / 51136 B | 78292 / 52664 B | +3252 / +1528 B |

The separate PWM bench-test images add the following over the base Experiment3 images
in the 2026-09-30 rebuild (the comparison table above remains the 2026-09-29 baseline):

| Target | Experiment3 Flash / RAM | PWM bench Flash / RAM | Additional delta |
| --- | --- | --- | --- |
| OpenI6X | 126772 / 15652 B | 129160 / 15788 B | +2388 / +136 B |
| STM32 | 78856 / 52672 B | 81456 / 53000 B | +2600 / +328 B |

OpenI6X has 1912 B Flash and 596 B statically accounted RAM left in this build. The UI
probe has one static bounded state record and uses roughly 150 B of temporary values while
`menuMainView` runs inside the already reserved 2240 B menu task stack. The STM32 adds one
diagnostic snapshot, one single-slot event mailbox and bounded protocol state; it adds
no dynamic allocation or growing cache. These are compile/link measurements and source-level
stack assumptions, not hardware stack high-water measurements.

Flash column uses `size` text+data from the matching CMake Release preset. Data is
unchanged (88 B OpenI6X, 452 B STM32), so RAM delta is BSS delta.
OpenI6X RAM accounting includes the linker-reserved 1024 B main stack; 596 B remains
outside those accounted sections. This is **not** a measured runtime stack high-water.

- Message value 32 B, serialized message 36 B.
- Core Rx state 52 B, including 40 B padded assembly buffer.
- Link 148 B: Rx plus TX message36, fragment18, raw fragment18 and timing/cursors.
- Four cache/tombstone records 128 B; controller endpoint including watermark 148 B.
- HMI `Exp3Hil`500 B, mailbox36 B; RF capture queue88 B plus publication/counters.
- STM32 `Exp3Hil`1268 B (64-entry ring); HMI ring has 16 entries of16 B. No growing caches.
- Isolated Cortex-M0 `-Os -fstack-usage` audit of the original protocol core: largest
  single function104 B; conservative receive call-chain sum about360 B, excluding the
  new boot exchange, callers, libraries and interrupts. Temporary foreground raw RF
  buffer37 B; message temporaries32 B. The boot exchange uses one bounded five-byte
  receive payload and one static session record per endpoint.
  Actual linked build uses LTO, so this is an audit estimate, not a final stack proof.
- CPU/control-cycle impact and stack watermark: **NOT MEASURED on hardware**.

## Build and images

`./experiments/ticket03/build_hil.sh` runs host checks then builds separate outputs.
No flashing is performed by any provided script.

- OpenI6X: `OpenI6X/FS-I6X/opentx/build-exp3-hil/firmware.bin` (with matching ELF).
- STM32: `control_chassis/build/exp3-hil/control_chassis.hex` (with matching ELF).
- OpenI6X PWM bench test: `OpenI6X/FS-I6X/opentx/build-exp3-pwm-hil/firmware.bin`.
- STM32 PWM bench test: `control_chassis/build/exp3-pwm-hil/control_chassis.hex`.
- Exact hashes are in `artifacts.json`; verify against the files before use.

### STM32 CMake build and ST-LINK/OpenOCD flashing

Run from the repository root. The two Experiment3 CMake presets use separate build
directories; their options default to OFF in normal `debug`/`release` builds.

```sh
cmake -S control_chassis --preset exp3-pwm-hil
cmake --build control_chassis/build/exp3-pwm-hil
sha256sum control_chassis/build/exp3-pwm-hil/control_chassis.hex
```

With the correct STM32 board connected through ST-LINK and OpenOCD available, flash the
image through CMake's existing OpenOCD target:

```sh
cmake --build control_chassis/build/exp3-pwm-hil --target flash
```

The `flash` target programs and verifies the ELF, then resets the MCU. If multiple probes
are connected, configure `-DSTLINK_SERIAL=<probe serial>` in the preset build directory
before using `flash`. The plain Experiment3 build uses preset `exp3-hil` and output
`control_chassis/build/exp3-hil/`; do not mix a base image with a PWM bench image.
The normal build target never flashes either MCU; only the explicit `flash` target programs
the controller.

### APM32 OpenI6X build and USB DFU flashing record

The radio used for this experiment is the **APM32 variant** and is flashed through its
built-in USB DFU bootloader. All commands below are run from the repository root. The
documented toolchain used for the current image is CMake 3.22.1 and GNU Arm Embedded GCC
10.3.1. `dfu-util` 0.9 is installed on the current Linux host.

Prerequisites:

```sh
cmake --version
arm-none-eabi-gcc --version
dfu-util --version
```

To run the host tests and build both Experiment3 targets exactly as recorded above:

```sh
./experiments/ticket03/build_hil.sh
```

To configure and build only the OpenI6X Experiment3 image:

```sh
cmake \
  -S OpenI6X/FS-I6X/opentx \
  -B OpenI6X/FS-I6X/opentx/build-exp3-hil \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/OpenI6X/FS-I6X/opentx/i6x-toolchain-linux.cmake" \
  -DPCB=I6X \
  -DTRANSLATIONS=EN \
  -DPCBI6X_SENS_HIL_DIAG=YES \
  -DPCBI6X_MOWER_UI_SHELL=YES \
  -DPCBI6X_MOWER_UI_MOCK=NO \
  -DPCBI6X_TICKET03_HIL_MODEL=NO \
  -DPCBI6X_EXP3=ON

cmake --build OpenI6X/FS-I6X/opentx/build-exp3-hil --target firmware -j8
```

`PCBI6X_EXP3` defaults to `OFF`; the separate build directory above turns it on only for
this HIL image. The build links `firmware.elf` with its vector table at `0x08000000`, then
uses `arm-none-eabi-objcopy -O binary` to create `firmware.bin`. Keep the matching ELF for
OpenOCD symbol access; flash only the BIN to the radio. Before flashing, verify this build:

```sh
sha256sum OpenI6X/FS-I6X/opentx/build-exp3-hil/firmware.bin
```

Expected base Experiment3 result:

```text
6351e916d187dc468dd06c6da7ef1338fee07ff7e36a17e065068c243f9b419d  OpenI6X/FS-I6X/opentx/build-exp3-hil/firmware.bin
```

Enter USB DFU mode before issuing any read or write command. With OpenI6X already
installed, switch the radio off, hold both inner horizontal trim buttons, switch it on,
then connect a USB data cable. A radio still running the original FlySky firmware needs
the documented first-time R53 pad bridge; remove that bridge after flashing. Confirm that
the APM32 DFU device, VID:PID `314b:0106`, is present:

```sh
dfu-util -l
```

Do not continue if that VID:PID is absent. Save the current 128 KiB internal Flash before
the first Experiment3 write:

```sh
dfu-util \
  -a 0 \
  -s 0x08000000 \
  -d 314b:0106 \
  -Z 0x20000 \
  -U openi6x-apm32-before-exp3.bin
```

Check that the backup is 131072 bytes and retain it outside the build directory:

```sh
stat -c '%n %s bytes' openi6x-apm32-before-exp3.bin
```

This is a raw internal-Flash backup; preserve model/settings separately if they matter to
the test setup. Flash the verified Experiment3 image at the absolute application address:

```sh
dfu-util \
  -a 0 \
  -s 0x08000000:leave \
  -d 314b:0106 \
  -D OpenI6X/FS-I6X/opentx/build-exp3-hil/firmware.bin
```

Wait for a successful download and leave/reset indication, disconnect USB, and power-cycle
the radio. If Linux reports USB permission denied, install an appropriate udev rule or run
the same narrowly scoped `dfu-util` command with `sudo`. Do not change the address to a
bootloader offset: this image contains a vector table at `0x08000000`. Do not flash the
chassis `control_chassis.hex` to the radio.

USB DFU is sufficient to install the image, but the full Experiment3 evidence procedure
below still requires a separately accessible, non-halting SWD/OpenOCD endpoint for the
OpenI6X MCU. The matching symbol file is
`OpenI6X/FS-I6X/opentx/build-exp3-hil/firmware.elf`.

## Isolated HIL procedure

This section applies to the **base Experiment3 images**, which retain debugger provisioning.
Use the previously established isolated fixture and T03HIL model. Keep existing actuator
isolation; this build does not disable or replace existing CH1–10 control logic.
No UI key starts Experiment3. This first fixture uses **two independently accessible,
non-halting SWD/OpenOCD Tcl endpoints**, one for each MCU. If the radio SWD connection
is unavailable, these images alone cannot run or capture the experiment; do not replace
that step with OK/CH6 or treat the LCD fixed-value display as result evidence.

1. Boot both images. They start **unbound**. Start two OpenOCD servers appropriate to
   the actual probes/targets with separate Tcl ports (example 6666 STM32, 6667 OpenI6X).
   Do not issue halt/reset while measuring. Probe selection/wiring is fixture-specific.
2. Create a new run file: `python3 experiments/ticket03/fixture.py new --session /tmp/e3-run.json`.
   It generates independent random nonzero session values and refuses overwrite. These are
   fixture-assigned experiment sessions, not a production persistent boot-counter protocol.
3. Start one `capture` process per endpoint before arming, using the matching ELF and port:
   `fixture.py capture --session /tmp/e3-run.json --elf <matching.elf> --port <port> --seconds 180 --output <new.jsonl>`.
   Keep collectors running during tests. They read ring entries and advance only `log_read`.
4. Run `fixture.py arm` for both endpoints using the same session file and corresponding
   `--elf/--port`. Verify event1 BIND with both sessions. Posting a mailbox command is not
   acknowledgment; verify the capture. Only one binding is allowed per boot.
5. On the **OpenI6X** endpoint run `fixture.py send ... --kind confirm --request 1 --action 1`.
   Expect TX, complete identity, STM32 test decision, transmitted Result, and HMI matching
   RTT (event12). RTT = HMI received time minus that HMI attempt's send time, 10 ms clock
   resolution. If retrying an identity, a late identical result can satisfy the wait; do not
   label such samples as an independently correlated new controller execution latency.
6. Query a never-confirmed identity (e.g. request2/action2), expect Unknown. Cancel a new
   identity (request3/action3), then Confirm the same identity, expect Cancelled both times.
   Query the original accepted identity, then retry Confirm: no second test action.
   Keep target/context/generation identical for operations on the same identity.
7. Inject faults: omit/reorder/duplicate words or fragments; disconnect each direction
   independently; RF failsafe; reboot one endpoint. Save independent timestamped SERVO/SENS
   line captures alongside software logs. After **either** reboot, the safe recovery sequence
   is stop capture, reboot **both**, create a **new** session file, bind both; never re-arm a
   rebooted endpoint using an old run file. Old peer/session traffic must not produce Accepted.
8. Record complete-message latency distribution and maximum observed latency (not a claimed
   universal worst case), direction-specific loss, refresh of addr1–8, CPU/control-cycle
   timing and stack watermark. Then decide whether to test more independent SENS addresses.

Diagnostics event IDs are in `exp3_hil.h`: session/request/context events reconstruct the
complete identity; Decision packs target/kind/echoed command and status; RawRx/RawTx contains
transport words; Channels preserves actual CH11 and CH12, including invalid/failsafe values.
SENS Request stores raw4 bytes, Response stores raw first4 and final2 bytes+length; these
are **generated responses before send_response**, not proof of successful UART transmission.
The external logic analyzer supplies actual bus delivery evidence.

The collector and capture queues are bounded. A run with any `log_overflow` or
`g_exp3_capture_overflow` increment is incomplete evidence; fix acquisition capacity before
claiming loss/latency statistics. Real non-halting probe throughput has not been tested here.
A timeout means result unknown, not "controller did not receive". No runtime safety policy
is inferred from the 60 s experiment timeout. Production profile, G1/G2/G3 remain open.

## CH4/OK to PA1 PWM bench test

This is a separate, default-off HIL build. Use it only with the blade/ESC physically
disconnected. The LCD uses `START BLADE? > OK` because the selected 128x64 English font
does not contain the requested Chinese glyphs. The meaning is “是否开启割刀，按 OK 确认”.
On i6X the physical OK is also `KEY_MENU`. The bench build handles OK before the
main-view menu switch, so it does not open the `Save settings / Reset...` menu while
the experiment prompt or result is visible. Both updated images are required: the older
radio image cannot participate in automatic pairing. `E3 SYNC` means the boot exchange
has not completed; `E3 READY` means this radio has received the controller's identity.
Pressing OK while unbound displays `EXP3 UNBOUND` and transmits no Confirm.

Build it with `./experiments/ticket03/build_hil.sh`, then flash these matching images:

```text
OpenI6X/FS-I6X/opentx/build-exp3-pwm-hil/firmware.bin
control_chassis/build/exp3-pwm-hil/control_chassis.hex
```

Before either flash, run `python3 experiments/ticket03/verify_artifacts.py` from the
repository root. It must report `PASS` for both PWM images; a failure means the build
files and recorded manifest do not match and the pair must be reviewed together.

The radio BIN uses the same APM32 USB DFU command documented above, with only the input
path changed to `build-exp3-pwm-hil/firmware.bin`. Build and flash the controller using
the CMake/OpenOCD commands above. Verify the two hashes in `artifacts.json` before flashing.

No `fixture.py arm` or radio SWD connection is needed for the basic PWM bench test.
After both boards boot and normal RF/SENS traffic is present, the radio advertises its
boot identity over CH11/12. The controller binds that identity and sends its own via
addr9. The radio then shows `E3 READY`; this may take several seconds because addr9 is
polled slowly. If either MCU reboots, the exchange runs again with a new volatile boot
identity and the controller invalidates its previous bench confirmation. Put CH4 through
center **after** `E3 READY` before starting a new test. The identity uses boot timing and
device ID as experimental entropy; it is not a cryptographic nonce or a production
reboot counter. Verify that the controller `session=` diagnostic changes after reboot.

For detailed Ticket03 loss/latency evidence, the two optional non-halting OpenOCD
`fixture.py capture` collectors and independent SERVO/SENS captures remain useful.
They are not required to see the prompt, matching result, UART decision and PA1 waveform.

Connect a 3.3 V USB-UART receiver to controller PA9 (USART1 TX) and controller ground,
115200 baud, 8 data bits, no parity, 1 stop bit. Do not connect the USB-UART TX pin to
PA10 because PA10 remains the live i-BUS receiver input. The one-second UART diagnostic
is a bounded summary and never advances the SWD capture ring's read cursor:

```text
EXP3 bound=... wait=... session=... matches=... id=request/action gen=HMI/controller reqseq=... result=status/event ch4=... blade=... gate=... ccr2=... confirm=... reject=... deny=... raw=... frag=... log=read/write overflow=...
```

`deny` records the last Confirm adapter decision: 0 allowed/no Confirm rejection,
1 safety gate, 2 blade state not pending, 3 no pending request, 4 CH4 not at start,
5 context/target mismatch, 6 CH4 generation mismatch, 7 event-sequence exhaustion.
It is a decision snapshot, not a live replacement for `blade/gate/ccr2`.

Bench sequence:

1. Keep the blade/ESC disconnected. Connect the oscilloscope to PA1/TIM2_CH2 and ground.
   A multimeter can show average voltage, but an oscilloscope is the acceptance instrument.
2. Establish the normal receiver link and normal manual run permission. Wait for `E3 READY`
   at the top of the mower page and `EXP3 bound=1` on USART1. Then set CH4 to center near
   1500 us once. Select a valid CH7 blade gear: about 2000 us is low (45% duty),
   1500 us is medium (50%), and 1000 us is high (55%). CH10 controls travel speed,
   not blade PWM.
3. Move CH4 to about 988 us. The mower page must display `START BLADE?` and a blinking
   `> OK`. Powering up with CH4 already at 988 must not show the prompt until CH4 has first
   visited center.
4. Press OK once. The UI sends one full TEST_CONFIRM and displays `WAIT RESULT`. With the
   short-result format and existing dwell values, the 17-word downlink occupies about
   4.25 seconds; including the uplink, a clean round trip is expected around 6-7 seconds.
   The earlier 90-word result took roughly 25-30 seconds. Wide action/generation values
   above 65535 fall back to that original format. These are not production response-time
   targets. When
   the matching TEST_RESULT is Accepted it displays `TEST ACCEPTED / PWM VIA CH7`.
5. Confirm USART shows `confirm` increment once, `reject=0`, blade state RUNNING, gate 0,
   and nonzero `ccr2`. PA1 should be about 10 kHz, 3.3 V logic amplitude, with 45% to 55%
   duty according to CH7. Expected CCR2 values are approximately 3780, 4200 and 4620
   for low, medium and high respectively (ARR=8399). Sweep CH10 separately while
   holding CH7 fixed; blade PWM must not change.
6. Pressing OK again or receiving a duplicate result must not increment `confirm`. To start
   another test, move CH4 to center near 1500 us on the mower page and hold it for at least
   one second. PA1 stops immediately, while the UI shows `WAIT CANCEL` during the old
   action's cancellation round trip (expected around 6-7 seconds with short results).
   Wait for that overlay to disappear, then move CH4 left near 988 us. If CH4 is moved
   left before the Cancel reply completes, the UI keeps `WAIT CANCEL` and offers the new
   `START BLADE? > OK` only after the transport is free. It does not queue an OK pressed
   during that wait. The result screen
   remains latched while CH4 stays left; merely dismissing it cannot create a new edge.
   If CH4 was centered in a menu outside the main views, center it again on the mower page.
   The bench context/target and CH4 edge generation must match the controller's current pending
   edge, so an Accepted result delayed across center-to-start cycles is rejected. The
   controller also rejects replayed Accepted results after its bounded event cache evicts
   their entries. A queued Cancel is sent before another Confirm can be created. A delayed
   Cancel for an older CH4 generation cannot clear a newer pending request; a session
   reset still invalidates the blade unconditionally. CH4 edges are counted during the
   cancellation wait even when no prompt is visible. Moving
   CH4 back to center sends TEST_CANCEL and forces CCR2 to zero. RF failsafe (1500/1500),
   loss of run permission, emergency/collision/tilt/software lock or owner change must also
   leave PA1 at zero. Returning to center after a result timeout also sends Cancel for the
   unresolved identity.
7. Repeat the existing missing/out-of-order/duplicate/conflict/session/reboot tests. No
   rejected, stale, unknown, Query or Cancel result may make CCR2 nonzero. If `overflow`
   changes during an evidence run, retain the trace but do not count it as a complete log.
   The SWD capture process is the sole detailed-ring consumer; USART reports its cursor and
   overflow counters without consuming entries.

An Accepted TEST_RESULT proves that the test endpoint accepted the message identity. It
does not by itself prove the blade adapter accepted the event: check that `confirm` increments
once, `reject` stays zero, and `gen=HMI/controller` agrees. The
additional `blade=RUNNING`, `gate=PERMIT`, nonzero `ccr2`, and PA1 waveform are the separate
evidence that the existing guarded actuator path produced PWM. They do not establish a
production Blade Confirm wire profile or a universal RF latency bound.

## Review

Initial standards and spec reviews found ISR/log-consumer, transport validation, identity
binding, timeout Cancel, and replay-after-eviction issues. The implementation and host
tests now cover those findings. A second independent review could not finish; real HIL
acceptance is still outstanding.
