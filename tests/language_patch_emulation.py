#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the in-memory language patch against stock 0.6d ARM9 code.

Usage: python3 tests/language_patch_emulation.py SNEmulDS.nds SNEmulDS.srl
Requires a host C compiler and Python unicorn. Console setup, config lookup,
and screen clear are stubbed. System detection executes the real getLanguage,
GUI_setLanguage and GUI_getConfig with modeled firmware bytes/RAM aliases;
this does not emulate firmware SPI, concurrent CPU startup or console hardware.
"""
import ctypes
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_ARM
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0,
                               UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP)

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


def run_selection(arm9, system_language, config_language, remap):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(BASE, 0x1000000)
    uc.mem_write(BASE, arm9)
    firmware = target(arm9, 0x1c178)
    init = target(arm9, 0x1c174)
    clear = target(arm9, 0x1c184)
    config_reader = target(arm9, 0x1e1c0)
    gui_string = u32(arm9, 0x1c13c) + 0x28
    # TGDS fills its high IPC buffer before shrinking RAM. Model only the
    # affected page's alias change; the low page still has zero at +0x634.
    uc.mem_write(0x02fff634, bytes([system_language | 0xf8]))
    if remap:
        uc.mem_write(0x02fff000, bytes(uc.mem_read(0x023ff000, 0x1000)))

    def stub(uc, address, size, user):
        if address == config_reader:
            key = bytes(uc.mem_read(uc.reg_read(UC_ARM_REG_R1), 32)).split(b'\0')[0]
            value = config_language if key == b'Language' else uc.reg_read(UC_ARM_REG_R2)
            uc.reg_write(UC_ARM_REG_R0, value & 0xffffffff)
        elif address not in (init, clear):
            return
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    uc.hook_add(UC_HOOK_CODE, stub)

    def call(entry):
        uc.reg_write(UC_ARM_REG_SP, 0x02ffe000)
        uc.reg_write(UC_ARM_REG_LR, RETURN)
        uc.emu_start(entry, RETURN, count=200)
        assert uc.reg_read(UC_ARM_REG_PC) == RETURN

    call(firmware)
    detected = uc.reg_read(UC_ARM_REG_R0)
    call(BASE + 0x1c09c)  # initial GUI_setLanguage(getLanguage())
    call(BASE + 0x1e194)  # actual GUI_getConfig, including Language == -1
    selections = [u32(uc.mem_read(gui_string, 4), 0)]
    for entry in (BASE + 0x1c16c, BASE + 0x1c188):
        call(entry)
        selections.append(u32(uc.mem_read(gui_string, 4), 0))
    return detected, selections


def check_system(arm9, fixed, system_patch, twl):
    # These literal-pool entries name the stock translation tables.
    tables = {0: 0x1c148, 1: 0x1c168, 2: 0x1c144, 3: 0x1c14c,
              4: 0x1c150, 5: 0x1c140, 106: 0x1c158, 107: 0x1c15c,
              108: 0x1c154, 109: 0x1c160, 110: 0x1c164}
    data = ctypes.create_string_buffer(fixed, len(fixed))
    if not twl:
        assert not system_patch(data, len(fixed), 1, True) and data.raw == fixed
    else:
        # Reproduce English firmware becoming Japanese through the RAM alias.
        assert run_selection(fixed, 1, -1, True) == (0, [u32(arm9, tables[0])] * 3)

    for language in range(8):
        data = ctypes.create_string_buffer(fixed, len(fixed))
        if twl:
            assert system_patch(data, len(fixed), language, False) and data.raw == fixed
            assert system_patch(data, len(fixed), language, True)
            assert data.raw[:0x27500] == fixed[:0x27500]
            assert data.raw[0x27508:] == fixed[0x27508:]
        for remap in ((False, True) if twl else (False,)):
            for config in (-1, *tables):
                selected = language if config == -1 else config
                expected = u32(arm9, tables.get(selected, tables[1]))
                assert run_selection(data.raw, language, config, remap) == (language, [expected] * 3), (twl, language, config, remap)


def check(path, expected_hash, patch, system_patch, twl):
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
    check_system(arm9, fixed, system_patch, twl)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with tempfile.TemporaryDirectory() as tmp:
        so = Path(tmp) / 'language_patch.so'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'include'), str(ROOT / 'source/tgds_patch.c'),
                        '-o', str(so)], check=True)
        lib = ctypes.CDLL(str(so))
        patch = lib.shim_tgds_language_patch
        patch.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_bool]
        patch.restype = ctypes.c_bool
        system_patch = lib.shim_tgds_twl_language_patch
        system_patch.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_bool]
        system_patch.restype = ctypes.c_bool
        for index, (path, digest) in enumerate(zip(sys.argv[1:], HASHES)):
            check(path, digest, patch, system_patch, index == 1)
    print('PASS: TWL system language survives modeled RAM remap; NTR detection and all CFG overrides preserved')
