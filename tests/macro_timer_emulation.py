#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the optional Macro countdown branch in stock 0.6d ARM9 images.

Usage: python3 tests/macro_timer_emulation.py SNEmulDS.nds SNEmulDS.srl
This models the countdown comparison and branch, not hardware timing.
"""
import ctypes
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_R7

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x02000000
GFX = 0x030030C0
BUILDS = (
    ('09bc43aad372da4984155d77c24a30a8c0a8a668288aa834eeb0695f1359094b', 0x4DE2C),
    ('f2817e29db1958af6af2957d0ac7ccfce01b5a57f78e522527d1d508c121b7a2', 0x4D50C),
)


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def next_pc(arm9, site, frames):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(BASE, 0x100000)
    uc.mem_map(0x03000000, 0x10000)
    uc.mem_write(BASE, arm9)
    uc.mem_write(GFX + 0x22C, struct.pack('<I', frames))
    uc.reg_write(UC_ARM_REG_R7, GFX)
    uc.emu_start(BASE + site - 12, 0, count=4)
    return uc.reg_read(UC_ARM_REG_PC)


def check(path, digest, site, patch):
    image = Path(path).read_bytes()
    assert hashlib.sha256(image).hexdigest() == digest, path
    offset, size = u32(image, 0x20), u32(image, 0x2C)
    arm9 = image[offset:offset + size]
    assert u32(arm9, site) == 0xCA000031
    assert next_pc(arm9, site, 0) == BASE + site + 4
    assert next_pc(arm9, site, 360) == BASE + site + 0xCC

    data = ctypes.create_string_buffer(arm9, len(arm9))
    assert patch(data, len(arm9), False) and data.raw == arm9
    assert patch(data, len(arm9), True)
    fixed = data.raw
    assert fixed[:site] == arm9[:site]
    assert fixed[site + 4:] == arm9[site + 4:]
    assert u32(fixed, site) == 0xEA000031
    assert next_pc(fixed, site, 0) == BASE + site + 0xCC
    assert next_pc(fixed, site, 360) == BASE + site + 0xCC
    assert not patch(data, len(arm9), True) and data.raw == fixed
    damaged = bytearray(arm9)
    damaged[site + 8] ^= 1
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
        patch = ctypes.CDLL(str(so)).shim_tgds_macro_timer_patch
        patch.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_bool]
        patch.restype = ctypes.c_bool
        for path, (digest, site) in zip(sys.argv[1:], BUILDS):
            check(path, digest, site, patch)
    print('PASS: stock NTR/TWL Macro countdown exits immediately only when patched')
