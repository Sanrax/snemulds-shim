#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the current ARM9 start stage and model SCFG RAM alias changes.

This does not emulate both CPUs or a whole DSi.
"""
from pathlib import Path
import os
import struct
import subprocess
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_PC

ROOT = Path(__file__).resolve().parents[1]
PREFIX = Path(os.environ.get('WONDERFUL_TOOLCHAIN', '/opt/wonderful')) / 'toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-'
START, TARGET = 0x02ffd000, 0x02004000

def stage_from_build(folder, binary):
    symbols = subprocess.check_output([str(PREFIX)+'nm', str(folder/'loader.elf')], text=True)
    entry = int(next(l.split()[0] for l in symbols.splitlines() if l.endswith(' startBinary_ARM9')), 16)
    return binary.read_bytes()[entry-0x06000000:entry-0x06000000+0x100]

def run(stage, unit, ram16):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x02000000, 0x1000000)
    uc.mem_map(0x04000000, 0x10000)
    uc.mem_write(START, stage)
    if not ram16:
        uc.mem_write(0x023fd000, stage) # same backing in NTR
    for base in (0x02fffe00, 0x027ffe00, 0x023ffe00):
        uc.mem_write(base+0x12, bytes([unit]))
        uc.mem_write(base+0x24, struct.pack('<I', TARGET))
    state = {'vcount': 0, 'remapped': False, 'forbidden': []}
    def read(uc, access, addr, size, value, user):
        if addr == 0x04000006:
            uc.mem_write(addr, struct.pack('<H', 191 if state['vcount'] == 0 else 192))
            state['vcount'] += 1
        elif addr == 0x02fffdfb:
            uc.mem_write(addr, b'\x01') # ARM7 completed header/argv copies
    def write(uc, access, addr, size, value, user):
        if 0x04004000 <= addr < 0x04004064:
            state['forbidden'].append((addr,value))
        if addr == 0x04004008 and ram16 and ((value >> 14) & 3) < 2:
            state['remapped'] = True
            uc.emu_stop()
    uc.hook_add(UC_HOOK_MEM_READ, read)
    uc.hook_add(UC_HOOK_MEM_WRITE, write)
    uc.emu_start(START, TARGET, count=10000)
    assert not state['remapped'], 'Handoff remapped live ARM9 code before transfer'
    assert uc.reg_read(UC_ARM_REG_PC) == TARGET, 'Stage failed to reach target entry'
    assert not state['forbidden'], 'Handoff must leave SCFG/MBK/clock setup to the target'

current = stage_from_build(ROOT/'build/loader', ROOT/'data/load.bin')
for unit, ram16 in [(0, False), (2, True)]:
    run(current, unit, ram16)
print('PASS: current NTR and TWL handoff reaches target with original RAM mapping')
print('LIMIT: isolated CPU instructions and RAM-alias model; physical boot still requires hardware.')
