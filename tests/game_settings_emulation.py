#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the in-memory per-ROM settings call on stock 0.6d ARM9 images.

Usage: python3 tests/game_settings_emulation.py SNEmulDS.nds SNEmulDS.srl
The config reader, GUI, reset and SRAM routines are stubbed. This models
control flow and arguments; it does not test FatFs or console hardware.
"""
import ctypes
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R11, UC_ARM_REG_SP

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x02000000
TITLE = 0x03001004
SITE = 0x126AC
HASHES = (
    '09bc43aad372da4984155d77c24a30a8c0a8a668288aa834eeb0695f1359094b',
    'f2817e29db1958af6af2957d0ac7ccfce01b5a57f78e522527d1d508c121b7a2',
)


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def run_calls(arm9):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(BASE, 0x1000000)
    uc.mem_map(0x03000000, 0x10000)
    uc.mem_write(BASE, arm9)
    uc.mem_write(TITLE, b'SUPER MARIOWORLD\0')
    uc.mem_write(0x02FFEFE4, struct.pack('<I', 0x123456))  # [r11, #-28]
    uc.reg_write(UC_ARM_REG_R11, 0x02FFF000)
    uc.reg_write(UC_ARM_REG_SP, 0x02FFE000)
    events = []
    settings = {'SUPER MARIOWORLD': 1}

    def stub(uc, address, size, user):
        if address == BASE + 0x12D94:
            title = bytes(uc.mem_read(uc.reg_read(UC_ARM_REG_R0), 21)).split(b'\0')[0].decode()
            uc.mem_write(0x03002000, struct.pack('<I', settings[title]))
            events.append(('read', title))
        elif address == BASE + 0x1E204:
            value = u32(uc.mem_read(0x03002000, 4), 0)
            events.append(('gui', uc.reg_read(UC_ARM_REG_R0), value))
        elif address == BASE + 0x1AD20:
            events.append(('reset',))
        elif address == BASE + 0x12400:
            events.append(('sram',))
        else:
            return
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    uc.hook_add(UC_HOOK_CODE, stub)
    uc.emu_start(BASE + SITE - 4, BASE + SITE + 12, count=40)
    assert uc.reg_read(UC_ARM_REG_PC) == BASE + SITE + 12
    return events


def check(path, digest, patch):
    image = Path(path).read_bytes()
    assert hashlib.sha256(image).hexdigest() == digest, path
    offset, size = u32(image, 0x20), u32(image, 0x2c)
    arm9 = image[offset:offset + size]
    assert u32(arm9, 0x126d8) == TITLE  # changeROM's strlen(title) literal
    assert run_calls(arm9) == [('gui', 0x123456, 0), ('reset',), ('sram',)]
    data = ctypes.create_string_buffer(arm9, len(arm9))
    assert patch(data, len(arm9), False) and data.raw == arm9
    assert patch(data, len(arm9), True)
    fixed = data.raw
    assert all(a == b for a, b in zip(arm9[:0xaf0], fixed[:0xaf0]))
    assert all(a == b for a, b in zip(arm9[0xb08:SITE], fixed[0xb08:SITE]))
    assert all(a == b for a, b in zip(arm9[SITE + 4:], fixed[SITE + 4:]))
    assert u32(fixed, 0xaf0) == 0xE92D4001
    assert u32(fixed, 0xaf4) == 0xE59F0008
    assert u32(fixed, 0xafc) == 0xE8BD4001
    assert u32(fixed, 0xb04) == TITLE
    assert run_calls(fixed) == [('read', 'SUPER MARIOWORLD'), ('gui', 0x123456, 1),
                                ('reset',), ('sram',)]
    assert not patch(data, len(arm9), True) and data.raw == fixed
    for damaged_offset in (0xaf0, SITE, SITE + 4, 0x13548):
        damaged = bytearray(arm9)
        damaged[damaged_offset] ^= 1
        bad = ctypes.create_string_buffer(bytes(damaged), len(damaged))
        assert not patch(bad, len(damaged), True) and bad.raw == damaged


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with tempfile.TemporaryDirectory() as tmp:
        so = Path(tmp) / 'tgds_patch.so'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'include'), str(ROOT / 'source/tgds_patch.c'),
                        '-o', str(so)], check=True)
        patch = ctypes.CDLL(str(so)).shim_tgds_game_settings_patch
        patch.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_bool]
        patch.restype = ctypes.c_bool
        for path, digest in zip(sys.argv[1:], HASHES):
            check(path, digest, patch)
    print('PASS: stock NTR/TWL per-ROM settings read before GUI, reset and SRAM')
