#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the in-memory language patch against stock 0.6d ARM9 code.

Usage: python3 tests/language_patch_emulation.py SNEmulDS.nds SNEmulDS.srl
Requires a host C compiler and Python unicorn. The console setup, firmware
language read, and screen clear are stubbed; this is not hardware testing.
"""
import ctypes
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x02000000
RETURN = 0x02fff100
HASHES = (
    '09bc43aad372da4984155d77c24a30a8c0a8a668288aa834eeb0695f1359094b',
    'f2817e29db1958af6af2957d0ac7ccfce01b5a57f78e522527d1d508c121b7a2',
)


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def target(data, offset):
    insn = u32(data, offset)
    words = insn & 0xffffff
    if words & 0x800000:
        words -= 0x1000000
    return BASE + offset + 8 + words * 4


def run_switch(arm9, start, english, init, firmware, clear, gui_string):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(BASE, 0x1000000)
    uc.mem_write(BASE, arm9)
    uc.mem_write(gui_string, struct.pack('<I', english))
    uc.reg_write(UC_ARM_REG_SP, 0x02fff000)
    uc.reg_write(UC_ARM_REG_LR, RETURN)

    def stub(uc, address, size, user):
        if address in (init, firmware, clear):
            if address == firmware:
                uc.reg_write(UC_ARM_REG_R0, 0)  # Japanese firmware language
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    uc.hook_add(UC_HOOK_CODE, stub)
    uc.emu_start(start, RETURN, count=200)
    assert uc.reg_read(UC_ARM_REG_PC) == RETURN
    return u32(uc.mem_read(gui_string, 4), 0)


def check(path, expected_hash, patch):
    image = Path(path).read_bytes()
    assert hashlib.sha256(image).hexdigest() == expected_hash, path
    offset = u32(image, 0x20)
    load = u32(image, 0x28)
    size = u32(image, 0x2c)
    assert load == BASE
    arm9 = image[offset:offset + size]
    data = ctypes.create_string_buffer(arm9, len(arm9))
    assert patch(data, len(arm9), False) and data.raw == arm9
    assert patch(data, len(arm9), True)
    fixed = data.raw
    assert not patch(data, len(arm9), False)
    changed = {i for i, (a, b) in enumerate(zip(arm9, fixed)) if a != b}
    assert changed == set(range(0x1c17c, 0x1c180)) | set(range(0x1c198, 0x1c19c))
    assert u32(fixed, 0x1c17c) == u32(fixed, 0x1c198) == 0xe1a00000
    assert not patch(data, len(arm9), True) and data.raw == fixed
    bad = bytearray(arm9)
    bad[0x1c16c] ^= 1
    rejected = ctypes.create_string_buffer(bytes(bad), len(bad))
    assert not patch(rejected, len(bad), True) and rejected.raw == bad

    init = target(arm9, 0x1c174)
    firmware = target(arm9, 0x1c178)
    clear = target(arm9, 0x1c184)
    assert target(arm9, 0x1c17c) == target(arm9, 0x1c198) == BASE + 0x1c09c
    assert target(arm9, 0x1c190) == init
    assert target(arm9, 0x1c194) == firmware
    assert target(arm9, 0x1c1a0) == clear
    gui_string = u32(arm9, 0x1c13c) + 0x28
    english = u32(arm9, 0x1c168)
    japanese = u32(arm9, 0x1c148)
    assert english != japanese
    for entry in (BASE + 0x1c16c, BASE + 0x1c188):
        assert run_switch(arm9, entry, english, init, firmware, clear, gui_string) == japanese
        assert run_switch(fixed, entry, english, init, firmware, clear, gui_string) == english


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with tempfile.TemporaryDirectory() as tmp:
        so = Path(tmp) / 'language_patch.so'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'include'), str(ROOT / 'source/tgds_patch.c'),
                        '-o', str(so)], check=True)
        patch = ctypes.CDLL(str(so)).shim_tgds_language_patch
        patch.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_bool]
        patch.restype = ctypes.c_bool
        for path, digest in zip(sys.argv[1:], HASHES):
            check(path, digest, patch)
    print('PASS: stock NTR/TWL language switches preserve CFG selection after RAM patch')
