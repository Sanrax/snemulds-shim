#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check final NDS header, loaded sections, and embedded loader bounds."""
from pathlib import Path
import hashlib
import struct
import sys

root = Path(__file__).resolve().parents[1]
b = (root / 'snemulds-shim.nds').read_bytes()
assert b[0x12] == 2, 'Shim must be DS + DSi enhanced'
assert b[0x1bf] & 1, 'Shim must request the TWL codec from the launcher'
assert struct.unpack_from('<I', b, 0x1b8)[0] == 0x80040407, 'Unexpected launch profile'

def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return crc

assert crc16(b[:0x15e]) == struct.unpack_from('<H', b, 0x15e)[0]
for name, pos in [('ARM9', 0x20), ('ARM7', 0x30), ('ARM9i', 0x1c0), ('ARM7i', 0x1d0)]:
    off, entry, dst, size = struct.unpack_from('<4I', b, pos)
    assert off >= 0x1000 and size and off + size <= len(b), name
    print(f'{name}: {size} bytes, destination 0x{dst:08x}')
loader = (root / 'data/load.bin').read_bytes()
h = struct.unpack_from('<14I', loader)
args_offset, dldi_offset = h[4], h[6]
assert h[9] == 0, 'Force-DLDI must be opt-in per launch'
assert h[10] == 0, 'Direct ARM9 I/O must be opt-in per launch'
assert h[11] == 0, 'NTR downgrade must be opt-in per launch'
assert h[12] == 0, '0.6d patches must be opt-in per launch'
assert h[13] == 0, 'Macro timer skip must be opt-in per launch'
assert len(loader) <= args_offset and args_offset + 236 <= 0x1f000
assert dldi_offset + (1 << loader[dldi_offset + 15]) <= len(loader)
assert loader in b, 'Built ROM must contain the current loader'
print(f'Loader: {len(loader)} bytes; argv offset 0x{args_offset:x}; fits VRAM')
print(f'SHA-256: {hashlib.sha256(b).hexdigest()}')
print('PASS: DS/DSi header, CRC, all four sections, loader bounds')
