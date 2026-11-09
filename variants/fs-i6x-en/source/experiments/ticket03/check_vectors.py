#!/usr/bin/env python3
"""Independent Python struct/zlib oracle; does not import or replicate the C codec."""
import struct
import subprocess
import sys
import zlib
actual = subprocess.check_output([sys.argv[1]], text=True).splitlines()
expected = []
version = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x31
for kind in range(1, 5):
    data = bytes([0xe3, version, kind, 2 if kind == 4 else 1])
    data += struct.pack('<6IHBB', 0x11223344, 0x55667788, 0x12345678, 0x90abcdef,
                        0x43210098, 3, 0x102, 2 if kind == 4 else 0, 3 if kind == 4 else 0)
    expected.append((data + struct.pack('<I', zlib.crc32(data))).hex())
assert actual == expected, (actual, expected)
print('PASS independent Python/C Confirm/Cancel/Query/Result golden vectors')
