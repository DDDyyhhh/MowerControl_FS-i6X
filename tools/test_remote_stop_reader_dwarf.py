#!/usr/bin/env python3
"""Offline release check: real ARM DWARF/GDB code, simulated target memory/RPC.

Usage: python3 tools/test_remote_stop_reader_dwarf.py firmware.elf firmware.bin
No OpenOCD is launched and no hardware is contacted.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import read_remote_stop_diag as reader


def main():
    elf, binary = [Path(arg).resolve() for arg in sys.argv[1:]]
    gdb = reader.find_gdb()
    reader.check_gdb(gdb)
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    capture_code = reader.build_gdb_script(1234, binary).split('python\n', 1)[1].rsplit('\nend\n', 1)[0]
    # GDB keeps the exact ELF's types and computes field offsets. Only memory
    # access, g_app values, and uwTick are replaced; execute the real capture.
    fixture = r'''
import gdb, struct
native_eval = gdb.parse_and_eval
kind = native_eval('g_app.remote_stop_diag').type.strip_typedefs()
size = int(kind.sizeof)
address = int(native_eval('&g_app.remote_stop_diag'))
raw = bytearray(size)
def set_field(path, value):
    offset, current = 0, kind
    for name in path.split('.'):
        field = next(f for f in current.strip_typedefs().fields() if f.name == name)
        offset += int(field.bitpos)//8
        current = field.type
    current = current.strip_typedefs()
    if current.code == gdb.TYPE_CODE_FLT:
        raw[offset:offset+4] = struct.pack('<f', value)
    elif current.code == gdb.TYPE_CODE_ARRAY:
        raw[offset:offset+int(current.sizeof)] = struct.pack('<14H', *value)
    else:
        raw[offset:offset+int(current.sizeof)] = int(value).to_bytes(int(current.sizeof), 'little')
for path,value in {'magic':0x52534431, 'version':1, 'record_size':size,
    'write_sequence':2, 'valid':1, 'event_count':1, 'gate_reason':3,
    'latch_reason':2, 'input_flags':24, 'pitch':2.5, 'roll':float('nan'),
    'channels':[1700]*14, 'rx_at_authorization.uart_errors':5,
    'rx.uart_errors':7}.items():
    set_field(path,value)
if scenario == 'odd': set_field('write_sequence',3)
if scenario == 'boot':
    set_field('valid',0)
    set_field('event_count',0)
image = open(binary_path,'rb').read()
class FakeRecord:
    type, address = kind, address
class FakeApp:
    def __getitem__(self,key):
        if key == 'remote_stop_diag': return FakeRecord()
        return int(scenario != 'unlocked') if key == 'lock' else 0
class FakeInferior:
    reads = 0
    def read_memory(self,where,length):
        if where == 0x08000000:
            return bytes([image[0]^1])+image[1:] if scenario == 'flash' else image
        assert where == address and length == size
        self.reads += 1
        if scenario == 'sram' and self.reads == 2:
            return bytes([raw[0]^1])+bytes(raw[1:])
        return bytes(raw)
fake_inferior = FakeInferior()
gdb.selected_inferior = lambda: fake_inferior
gdb.parse_and_eval = lambda expr: FakeApp() if expr == 'g_app' else 1000
exec(capture_code)
'''
    class Rpc:
        running = True

        def __call__(self, port, command):
            if command == 'halt': self.running = False
            if command == 'resume': self.running = True
            return 'running' if self.running else 'halted'

    with tempfile.TemporaryDirectory(prefix='stop-reader-dwarf-') as directory:
        work = Path(directory)
        reader.verify_local_artifacts(elf, binary, reader.find_arm_tool('arm-none-eabi-objcopy'), work/'extracted.bin')
        print('PASS local ELF/BIN correspondence and Python-capable ARM GDB preflight')
        for scenario in ['success', 'flash', 'sram', 'odd', 'unlocked', 'boot']:
            script = work/'test.gdb'
            prelude = f'scenario={scenario!r}\nbinary_path={str(binary)!r}\ncapture_code={capture_code!r}\n'
            script.write_text('python\n'+prelude+fixture+'\nend\nquit\n')
            rpc = Rpc()

            def capture():
                result = subprocess.run([gdb,'--batch','--nx','--quiet',str(elf),'-x',str(script)],
                                        capture_output=True,text=True,timeout=10)
                if result.returncode:
                    raise reader.CaptureError(result.stderr[-2000:])
                lines = [line[len(reader.JSON_MARKER):] for line in result.stdout.splitlines()
                         if line.startswith(reader.JSON_MARKER)]
                assert len(lines) == 1, result.stdout
                return reader.validate_capture(json.loads(lines[0]),digest)

            if scenario in ['success','boot']:
                result = reader.capture_with_recovery(1234,capture,rpc)
                record = result['record']
                assert result['type_size'] == len(bytes.fromhex(result['record_bytes_hex']))
                assert record['pitch'] == 2.5 and record['roll'] == 'nan'
                assert record['channels'][8] == 1700
                if scenario == 'success':
                    assert result['interpretation']['rx_counter_increases_since_unlock']['uart_errors'] == 2
                else:
                    assert not record['valid']
            else:
                try:
                    reader.capture_with_recovery(1234,capture,rpc)
                except reader.CaptureError:
                    pass
                else:
                    raise AssertionError(scenario+' was accepted')
            assert rpc.running
            print('PASS actual ARM ELF/GDB Python capture '+scenario+'; memory/RPC simulated; CPU recovery checked')


if __name__ == '__main__':
    main()
