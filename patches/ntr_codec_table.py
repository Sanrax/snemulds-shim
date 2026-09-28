#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Translate the bundled, licensed Pico-loader DS codec sequence to a table.

No compiler/library linkage to Pico-loader is required. 0.6a/0.6d use its
normal homebrew volume setting, not the retail-game volume workaround.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
reference = root / 'third_party/pico-reference'
defines = {k: int(v, 0) for k, v in re.findall(
    r'^#define\s+(\w+)\s+(0x[0-9a-fA-F]+|\d+)\s*$',
    (reference / 'spiCodec.h').read_text(), re.M)}
defines['volLevel'] = 0xA7
def number(s):
    s = s.strip()
    return defines[s] if s in defines else int(s, 0)

body = (reference / 'DSMode.cpp').read_text().split('void DSMode::SwitchCodecToDSMode')[1]
body = body.split('bool DSMode::ShouldUseVolumeFix')[0]
page = None
table = []
for match in re.finditer(r'codec_(setPage|readRegister|writeRegister)\(([^)]*)\);', body):
    op, args = match.groups()
    fields = [number(x) for x in args.split(',')]
    if op == 'setPage':
        page = fields[0]
        continue
    assert page is not None
    reg = fields[0]
    value = fields[1] if op == 'writeRegister' else 0
    if op == 'readRegister': reg |= 0x80
    table.append((page, reg, value))
assert table[-1] == (255, 5, 0)
out = '/* Generated from Pico-loader DSMode.cpp (LNH team, Zlib). */\n'
out += 'static const unsigned char ntr_codec[][3] = {\n'
out += ''.join(f'    {{0x{p:02X}, 0x{r:02X}, 0x{v:02X}}},\n' for p, r, v in table)
out += '};\n'
target = root / 'include/ntr_codec_generated.h'
if not target.exists() or target.read_text() != out: target.write_text(out)
print(f'NTR codec: {len(table)} read/write operations from Pico-loader')
