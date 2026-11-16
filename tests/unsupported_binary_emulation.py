#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the built shim's preflight and error wrapping with modeled file reads.

Usage: python3 tests/unsupported_binary_emulation.py [stable.nds stable.srl dsp.nds dsp.srl sdd1.nds sdd1.srl]
Requires make, a host ARM toolchain and Python Unicorn. Optional arguments
also check successful preflight of the six supported 0.6d binaries. File I/O,
allocation and text output are stubbed; this does not test console hardware.
"""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys

from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0,
                               UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP)

ROOT = Path(__file__).resolve().parents[1]
PREFIX = Path(os.environ.get('WONDERFUL_TOOLCHAIN', '/opt/wonderful')) / 'toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-'
listing = subprocess.check_output([str(PREFIX)+'nm', str(ROOT/'build/snemulds-shim.elf')], text=True)
symbols = {p[2]: int(p[0], 16) for line in listing.splitlines() if len(p := line.split()) == 3}
raw = (ROOT/'snemulds-shim.nds').read_bytes()
offset, _, base, size = struct.unpack_from('<4I', raw, 0x20)
code = raw[offset:offset+size]
RETURN, STACK = 0x02FF0000, 0x02FFE000
REGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
BUILDS = (
    # Selected variant, TWL, 0.6d patches, expected build name and hash.
    (0, False, True, 'NTR', '77f82df3437eff94d35951c673523f2acd964d1a0400555bae58a55b4362a472'),
    (0, True, True, 'TWL', '83e3e4cd3e76a88ca089248eb375580d75cdff567aa490397aaf8dd07fc6812c'),
    (1, False, True, 'DSP-1 NTR', '292b70d62bb4092aa94e3723e3352bf3f309cba406bb02c665c9a7faac3a62c4'),
    (1, True, True, 'DSP-1 TWL', '36b84c717692ec4c5ae1388c545292de6ca43bff4b881df553d4bffb440c99c2'),
    (2, False, True, 'S-DD1 NTR', 'c4393f40ff41e51df95cd122819c3c9b0781c331bb0e73f08a91e3b235e3242a'),
    (2, True, True, 'S-DD1 TWL', '34a4063c98b099c4dabdb99c7b60c5c55f002c493e8e65a44f9fc0cbfd685e50'),
    (0, False, False, '0.6a NTR', '6ec30da30098d8b3632fc3107b1a9cbc594011fa69992fe04ac6a13cb6c4e599'),
)


def string(uc, address):
    return bytes(uc.mem_read(address, 256)).split(b'\0')[0].decode()


def check(image, profile, *, flashcart=True, accepted=False):
    variant, twl, patches, build, digest = profile
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.mem_map(0x02000000, 0x1000000)
    uc.mem_map(0x04000000, 0x10000)
    uc.mem_write(base, code)
    state = {'offset': 0, 'heap': 0x02400000, 'lines': []}
    functions = {symbols[name]: name for name in ('stat', 'fopen', 'fread', 'fseek',
                                                'fclose', '__malloc_malloc', '__malloc_free',
                                                'ui_text', 'printf', 'fputc')}

    def hook(uc, address, size, user):
        if address not in functions:
            return
        fn = functions[address]
        r = [uc.reg_read(reg) for reg in REGS]
        result = 0
        if fn == 'stat':
            # The built BlocksDS/picolibc stat ABI: inode, mode, file length.
            data = bytearray(104)
            for pos, value in ((4, 42), (8, 0x8000), (36, len(image))):
                struct.pack_into('<I', data, pos, value)
            uc.mem_write(r[1], bytes(data))
        elif fn == 'fopen':
            result = 0x020F0000
            state['offset'] = 0
        elif fn == 'fseek':
            assert r[2] == 0
            state['offset'] = r[1]
        elif fn == 'fread':
            begin = state['offset']
            data = image[begin:begin+r[1]*r[2]]
            uc.mem_write(r[0], data)
            state['offset'] += len(data)
            result = len(data)//r[1]
        elif fn == '__malloc_malloc':
            result = state['heap']
            state['heap'] += (r[0]+3) & ~3
        elif fn == 'ui_text':
            assert 5 <= r[0] < 18 and r[1] == 1
            line = string(uc, r[3])
            assert len(line) <= 30
            state['lines'].append(line)
        elif fn == 'printf':
            # ui_text may be inlined into ui_wrap by the release compiler.
            assert string(uc, r[0]) == '\x1b[%u;%uH\x1b[%um'
            assert 5 <= r[1] < 18 and r[2] == 1
            state['lines'].append('')
        elif fn == 'fputc':
            state['lines'][-1] += chr(r[0])
            assert len(state['lines'][-1]) <= 30
            result = r[0]
        uc.reg_write(UC_ARM_REG_R0, result)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    uc.hook_add(UC_HOOK_CODE, hook)

    def call(entry, args):
        uc.reg_write(UC_ARM_REG_SP, STACK)
        uc.reg_write(UC_ARM_REG_LR, RETURN | 1)
        for reg, value in zip(REGS, args):
            uc.reg_write(reg, value)
        if len(args) > 4:
            uc.mem_write(STACK, struct.pack('<'+'I'*(len(args)-4), *args[4:]))
        try:
            uc.emu_start(symbols[entry] | 1, RETURN, count=5000000)
        except Exception as error:
            raise AssertionError((entry, hex(uc.reg_read(UC_ARM_REG_PC)), [hex(uc.reg_read(reg)) for reg in REGS])) from error
        assert uc.reg_read(UC_ARM_REG_PC) == RETURN, 'Instruction budget exceeded'
        return uc.reg_read(UC_ARM_REG_R0)

    uc.mem_write(0x020E0000, b'fat:/emulator\0')
    result = call('check_nds', [0x020E0000, 0x020E0100, int(twl), int(twl and flashcart),
                              int(twl and flashcart), int(patches), int(patches), variant])
    if accepted:
        assert result == 0, string(uc, result)
        return
    assert result != 0
    message = string(uc, result)
    assert message.startswith(f'Unsupported SNEmulDS\n{build} binary.')
    assert f'Use the supported {build}\nbuild.' in message
    assert message.endswith('SHA256: '+digest)
    call('ui_wrap', [5, 13, 1, result])
    lines = state['lines']
    assert lines[:2] == ['Unsupported SNEmulDS', build+' binary.']
    assert ''.join(lines[6:]) == 'SHA256: '+digest, lines


for profile in BUILDS:
    check(bytes(512), profile)  # Wrong mode or invalid header, including 0.6a.
    if profile[2]:
        # Valid section layout with unsupported instructions reaches the patch
        # guards. TWL checks both flashcart storage and console-SD touch input.
        image = bytearray(0x3000)
        image[0x12] = 2 if profile[1] else 0
        for pos, off, dst in ((0x20, 0x1000, 0x02000000), (0x30, 0x2000, 0x02380000)):
            struct.pack_into('<4I', image, pos, off, dst, dst, 4)
        check(bytes(image), profile)
        if profile[1]:
            check(bytes(image), profile, flashcart=False)
if len(sys.argv) > 1:
    assert len(sys.argv) == 7, __doc__
    for path, profile in zip(sys.argv[1:], BUILDS):
        image = Path(path).read_bytes()
        assert hashlib.sha256(image).hexdigest() == profile[4], path
        check(image, profile, accepted=True)
print('PASS: NTR/TWL, DSP-1/S-DD1 and 0.6a unsupported-build messages; full expected hashes fit the error screen')
if len(sys.argv) > 1:
    print('PASS: all six supported 0.6d binaries still pass preflight')
