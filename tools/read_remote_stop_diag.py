#!/usr/bin/env python3
"""Read an already-stopped mower's first-stop record via ST-Link/OpenOCD.

No firmware download, reset, function call on the target or unlock is performed.
The CPU is halted only for Flash verification and a coherent SRAM read, then
resumed through OpenOCD's independent Tcl channel even if GDB fails.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time

MAGIC = 0x52534431
JSON_MARKER = 'REMOTE_STOP_JSON:'
TARGET_STATE_QUERY = 'set t [target current]; $t arp_poll; $t curstate'
GATE_REASONS = ['permit', 'not_initialized', 'physical', 'software_lock',
                'remote_latch', 'link', 'run_permission', 'owner', 'stale_command', 'battery']
LATCH_REASONS = ['none', 'synchronizing', 'timeout', 'remote_stop',
                 'ownership_transition', 'safety_reject']
INPUT_FLAGS = ['connected', 'link_fresh', 'stable_online', 'timeout_history',
               'timeout_stop_active', 'remote_latched', 'run_permitted',
               'software_locked', 'physical_latched', 'input_physical',
               'emergency', 'collision', 'tilt', 'collision_latched',
               'collision_enabled', 'tilt_enabled']

# Runs inside GDB. Offsets and sizes come from this exact ELF's DWARF, never
# from an address table left over from a different firmware.
GDB_DECODER = r'''
import gdb, struct, json, hashlib, math
def decode_record(kind, data, offset=0):
    kind = kind.strip_typedefs()
    size = int(kind.sizeof)
    if offset < 0 or offset+size > len(data):
        raise RuntimeError('DWARF field outside record')
    if kind.code == gdb.TYPE_CODE_STRUCT:
        result = {}
        for field in kind.fields():
            if field.name is None or field.bitpos % 8 or field.bitsize:
                raise RuntimeError('Unsupported diagnostic field layout')
            result[field.name] = decode_record(field.type, data, offset+field.bitpos//8)
        return result
    if kind.code == gdb.TYPE_CODE_ARRAY:
        first,last = kind.range()
        item = kind.target()
        return [decode_record(item, data, offset+(i-first)*int(item.sizeof))
                for i in range(first,last+1)]
    if kind.code in (gdb.TYPE_CODE_INT,gdb.TYPE_CODE_ENUM,gdb.TYPE_CODE_BOOL):
        return int.from_bytes(data[offset:offset+size], 'little')
    if kind.code == gdb.TYPE_CODE_FLT and size == 4:
        value = struct.unpack('<f',data[offset:offset+size])[0]
        return value if math.isfinite(value) else str(value)
    raise RuntimeError('Unsupported diagnostic type: '+str(kind))
'''


class CaptureError(RuntimeError):
    pass


class RecoveryError(CaptureError):
    pass


def tcl_command(port, command, timeout=3.0):
    with socket.create_connection(('127.0.0.1', port), timeout=timeout) as connection:
        connection.settimeout(timeout)
        connection.sendall(command.encode('utf-8') + b'\x1a')
        result = bytearray()
        while b'\x1a' not in result:
            block = connection.recv(4096)
            if not block:
                raise CaptureError('OpenOCD Tcl connection closed')
            result.extend(block)
            if len(result) > 65536:
                raise CaptureError('OpenOCD Tcl response too large')
        return result.split(b'\x1a', 1)[0].decode('utf-8', errors='replace')


def capture_with_recovery(port, capture, rpc=tcl_command):
    """Restore running state through a channel independent of GDB's lifetime."""
    try:
        rpc(port, 'halt')
        # Global `poll` prints transport-dependent text; ST-Link HLA can return
        # only "background polling: off / TAP...". Refresh the target itself
        # and use its documented machine-readable state instead.
        if rpc(port, TARGET_STATE_QUERY).strip() != 'halted':
            raise CaptureError('CPU halt could not be confirmed')
        return capture()
    finally:
        try:
            rpc(port, 'resume')
            state = rpc(port, TARGET_STATE_QUERY).strip()
            if state != 'running':
                raise CaptureError('OpenOCD did not report a running CPU: '+state)
        except Exception as error:
            raise RecoveryError('CPU恢复运行未能确认，请检查ST-Link连接并恢复CPU；'
                                '本次读取不得视为成功。原因：'+str(error)) from error


def validate_capture(result, expected_sha256):
    if result.get('verified_flash_sha256') != expected_sha256:
        raise CaptureError('Flash/BIN identity mismatch')
    if not result.get('locked'):
        raise CaptureError('目标尚未锁定。只在实际故障后读取；调试暂停可能触发额外超时，'
                           '请重新解锁复测，不把调试造成的超时作为原始证据。')
    record = result['record']
    if record.get('magic') != MAGIC or record.get('version') != 1:
        raise CaptureError('Diagnostic magic/version mismatch')
    if record.get('record_size') != result.get('type_size'):
        raise CaptureError('Diagnostic size does not match ELF type')
    if record.get('write_sequence', 1) & 1:
        raise CaptureError('Diagnostic publication interrupted (odd sequence)')
    if record.get('armed') != 0:
        raise CaptureError('First-stop capture not committed; wait until the real lock is visible')
    if record.get('valid') not in (0, 1) or (record['valid'] and not record.get('event_count')):
        raise CaptureError('Invalid diagnostic validity/count')
    if record['valid']:
        gate, latch = record['gate_reason'], record['latch_reason']
        result['interpretation'] = {
            'gate_reason': GATE_REASONS[gate] if gate < len(GATE_REASONS) else 'unknown',
            'authorization_latch': LATCH_REASONS[latch] if latch < len(LATCH_REASONS) else 'unknown',
            'inputs': {name: bool(record['input_flags'] & (1 << bit))
                       for bit, name in enumerate(INPUT_FLAGS)},
            'note': '仲裁原因可能是软件锁；需结合授权原因、输入及链路年龄判断首次触发源。',
        }
        if 'rx_at_authorization' in record and 'rx' in record:
            counts = ['expired_messages', 'queue_overflows', 'stream_resets',
                      'invalid_messages', 'crc_errors', 'invalid_channel_frames', 'uart_errors']
            result['interpretation']['rx_counter_increases_since_unlock'] = {
                name: record['rx'][name]-record['rx_at_authorization'][name] for name in counts}
            result['interpretation']['counter_note'] = '累计计数饱和时，零增量不证明没有新错误。'
    else:
        result['interpretation'] = {
            'note': '本次启动尚无首次撤权记录。若动作后重启，需结合启动复位标志及运行时间；'
                    '无记录不能排除供电或复位问题。'}
    flags = record['boot_reset_flags']
    result['boot_reset_causes'] = [name for bit, name in [
        (25, 'BOR'), (26, 'PIN'), (27, 'POR'), (28, 'SOFTWARE'),
        (29, 'IWDG'), (30, 'WWDG'), (31, 'LOW_POWER')] if flags & (1 << bit)]
    result['reset_note'] = '正常上电也可置BOR/POR；复位标志本身不能证明推杆导致掉压。'
    return result


def build_gdb_script(port, binary):
    # Paths are Python literals inside GDB's Python block, not shell commands.
    return ('set pagination off\nset confirm off\nset remotetimeout 5\n'
            'set trust-readonly-sections off\nset code-cache off\nset stack-cache off\n'
            f'target extended-remote 127.0.0.1:{port}\npython\n' + GDB_DECODER +
            f'expected = open({str(binary)!r}, "rb").read()\n' + r'''
inferior = gdb.selected_inferior()
actual = bytes(inferior.read_memory(0x08000000,len(expected)))
if actual != expected:
    raise RuntimeError('Flash differs from supplied BIN; no diagnostic decoded')
app = gdb.parse_and_eval('g_app')
value = app['remote_stop_diag']
kind = value.type
size = int(kind.sizeof)
if not 128 <= size <= 4096:
    raise RuntimeError('Unexpected diagnostic type size')
address = int(value.address)
first = bytes(inferior.read_memory(address,size))
second = bytes(inferior.read_memory(address,size))
if first != second:
    raise RuntimeError('SRAM reads differ while halted; reject unstable readback')
result = {
    'verified_flash_sha256': hashlib.sha256(actual).hexdigest(),
    'locked': bool(int(app['lock']) or int(app['physical_safety_latched'])),
    'type_size': size, 'record_address': hex(address),
    'uptime_ms': int(gdb.parse_and_eval('uwTick')),
    'record': decode_record(kind,first), 'record_bytes_hex': first.hex(),
}
print('REMOTE_STOP_JSON:'+json.dumps(result,ensure_ascii=False,allow_nan=False))
end
disconnect
quit
''')


def unused_port():
    with socket.socket() as connection:
        connection.bind(('127.0.0.1', 0))
        return connection.getsockname()[1]


def find_gdb():
    # Arm GNU's distributed GDB may omit Python; gdb-multiarch supplies both.
    multiarch = shutil.which('gdb-multiarch')
    if multiarch:
        return multiarch
    return find_arm_tool('arm-none-eabi-gdb')


def find_arm_tool(name):
    root = Path(os.environ.get('ARM_GNU_TOOLCHAIN_ROOT',
                str(Path.home()/'.local/opt/arm-gnu-toolchain-15.3.rel1-x86_64-arm-none-eabi')))
    candidate = root/'bin'/name
    return str(candidate) if candidate.is_file() else shutil.which(name)


def verify_local_artifacts(elf, binary, objcopy, destination):
    if not objcopy:
        raise CaptureError('需要arm-none-eabi-objcopy核对ELF与BIN，可用 --objcopy 指定')
    result = subprocess.run([objcopy, '-O', 'binary', str(elf), str(destination)],
                            capture_output=True, text=True, timeout=10)
    if result.returncode:
        raise CaptureError('ELF提取失败：'+result.stderr[-1500:])
    if destination.read_bytes() != binary.read_bytes():
        raise CaptureError('ELF与BIN不是同一镜像；未连接设备')


def check_gdb(executable):
    probe = subprocess.run([executable, '--batch', '--nx', '-ex', 'set architecture arm',
                            '-ex', 'python print("REMOTE_STOP_PYTHON_OK")'],
                           capture_output=True, text=True, timeout=5)
    if probe.returncode or 'REMOTE_STOP_PYTHON_OK' not in probe.stdout:
        raise CaptureError('GDB需要支持ARM与Python。建议使用gdb-multiarch或通过 --gdb 指定；'
                           '当前调试器未通过检查，未连接设备。')


def run_capture(args):
    elf, binary = args.elf.resolve(), args.bin.resolve()
    if not elf.is_file() or not binary.is_file():
        raise CaptureError('需要本次配套ELF和BIN；请检查 --elf / --bin')
    image = binary.read_bytes()
    if not image or len(image) > 512*1024:
        raise CaptureError('Invalid chassis BIN length')
    if not args.gdb or not args.openocd:
        raise CaptureError('需要支持Python的ARM GDB和OpenOCD，可用 --gdb / --openocd 指定路径')
    check_gdb(args.gdb)
    digest = hashlib.sha256(image).hexdigest()
    elf_image = elf.read_bytes()
    gdb_port, tcl_port = unused_port(), unused_port()
    while tcl_port == gdb_port:
        tcl_port = unused_port()
    with tempfile.TemporaryDirectory(prefix='mower-stop-read-') as directory:
        work = Path(directory)
        frozen_elf, frozen_bin = work/'firmware.elf', work/'firmware.bin'
        frozen_elf.write_bytes(elf_image)
        frozen_bin.write_bytes(image)
        verify_local_artifacts(frozen_elf, frozen_bin, args.objcopy, work/'elf-image.bin')
        script = work/'read.gdb'
        script.write_text(build_gdb_script(gdb_port, frozen_bin))
        with (work/'openocd.log').open('w+') as log:
            command = [args.openocd, '-f', 'interface/stlink.cfg', '-f', 'target/stm32f4x.cfg',
                       '-c', 'bindto 127.0.0.1', '-c', f'gdb_port {gdb_port}',
                       '-c', f'tcl_port {tcl_port}', '-c', 'telnet_port disabled',
                       '-c', f'adapter speed {args.adapter_khz}', '-c', 'reset_config none']
            server = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic()+8.0
                while True:
                    if server.poll() is not None:
                        log.seek(0)
                        raise CaptureError('OpenOCD启动失败：\n'+log.read()[-3000:])
                    try:
                        tcl_command(tcl_port, 'version', timeout=0.2)
                        break
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise CaptureError('OpenOCD连接超时；未开始诊断暂停')
                        time.sleep(0.1)

                def read():
                    result = subprocess.run([args.gdb, '--batch', '--nx', '--quiet',
                                             str(frozen_elf), '-x', str(script)],
                                            capture_output=True, text=True, timeout=args.timeout)
                    if result.returncode:
                        raise CaptureError('GDB读取失败：\n'+(result.stdout+result.stderr)[-3000:])
                    lines = [line[len(JSON_MARKER):] for line in result.stdout.splitlines()
                             if line.startswith(JSON_MARKER)]
                    if len(lines) != 1:
                        raise CaptureError('GDB did not return one diagnostic record')
                    return validate_capture(json.loads(lines[0]), digest)

                captured = capture_with_recovery(tcl_port, read)
                captured['cpu_resumed'] = True
                captured['captured_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
                captured['elf'] = str(elf)
                captured['elf_sha256'] = hashlib.sha256(elf_image).hexdigest()
                return captured
            finally:
                server.terminate()
                try:
                    server.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=Path(__file__).with_name('control-chassis-timeout-recovery-stop-diag.elf'))
    parser.add_argument('--bin', type=Path, default=Path(__file__).with_name('control-chassis-timeout-recovery-stop-diag.bin'))
    parser.add_argument('--gdb', default=find_gdb())
    parser.add_argument('--objcopy', default=find_arm_tool('arm-none-eabi-objcopy'))
    parser.add_argument('--openocd', default=shutil.which('openocd'))
    parser.add_argument('--adapter-khz', type=int, default=100)
    parser.add_argument('--timeout', type=float, default=30.0)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not 1 <= args.adapter_khz <= 4000 or not 1 <= args.timeout <= 60:
        parser.error('adapter-khz must be 1..4000; timeout must be 1..60 seconds')
    output = args.output or Path('stop-diag-'+datetime.datetime.now(
        datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')+'.json')
    if output.exists():
        parser.error('Output exists; choose a new file to preserve evidence')
    try:
        captured = run_capture(args)
    except (CaptureError, OSError, subprocess.SubprocessError, ValueError) as error:
        print('读取失败：'+str(error), file=sys.stderr)
        return 1
    output.write_text(json.dumps(captured, ensure_ascii=False, indent=2, allow_nan=False)+'\n')
    print('已核对Flash并读取记录，CPU已恢复运行：'+str(output.resolve()))
    print(json.dumps(captured['interpretation'], ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
