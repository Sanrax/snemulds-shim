#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the shipped DSP-1/S-DD1 NTR/TWL binaries and execute patch sites.

Usage: python3 tests/alternate_builds_emulation.py dsp.nds dsp.srl sdd1.nds sdd1.srl
Requires a host C compiler and Unicorn. Config/GUI calls and the DLDI sector
driver are stubbed; this checks control flow, arguments, register restoration
and rejection of damaged code, not a physical card, audio or game compatibility.
Run twl_input_emulation.py separately on each TWL binary for codec/touch checks.
"""
import ctypes
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_ARM
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP,
                               UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
                               UC_ARM_REG_R4, UC_ARM_REG_R6, UC_ARM_REG_R11)
from language_patch_emulation import run_switch, target

ROOT = Path(__file__).resolve().parents[1]
BASE, RETURN, TITLE = 0x02000000, 0x02FF0000, 0x03001004
BUILDS = (
    # SHA-256, Macro site, native settings GUI call, TWL I/O read/FS functions.
    ('292b70d62bb4092aa94e3723e3352bf3f309cba406bb02c665c9a7faac3a62c4', 0x16120, 0x141A0, 0, 0),
    ('36b84c717692ec4c5ae1388c545292de6ca43bff4b881df553d4bffb440c99c2', 0x16120, 0x141A0, 0x5BBBC, 0x240E0),
    ('c4393f40ff41e51df95cd122819c3c9b0781c331bb0e73f08a91e3b235e3242a', 0x124D8, 0x1030C, 0, 0),
    ('34a4063c98b099c4dabdb99c7b60c5c55f002c493e8e65a44f9fc0cbfd685e50', 0x124D8, 0x1030C, 0x4D038, 0x1FE2C),
)


def u32(data, offset=0):
    return struct.unpack_from('<I', data, offset)[0]


def p32(value):
    return struct.pack('<I', value)


def machine(arm9):
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    for address, size in [(BASE, 0x1000000), (0x03000000, 0x10000), (0x04000000, 0x10000)]:
        uc.mem_map(address, size)
    uc.mem_write(BASE, arm9)
    uc.reg_write(UC_ARM_REG_SP, 0x02FFE000)
    uc.reg_write(UC_ARM_REG_LR, RETURN)
    return uc


def checked_patch(fn, original, damaged_offsets, changes=True, exact_size=True):
    data = ctypes.create_string_buffer(original, len(original))
    assert fn(data, len(original), False) and data.raw == original
    if exact_size:
        assert not fn(data, len(original)-1, True) and data.raw == original
    for offset in damaged_offsets:
        bad = bytearray(original)
        bad[offset] ^= 1
        buf = ctypes.create_string_buffer(bytes(bad), len(bad))
        assert not fn(buf, len(bad), True) and buf.raw == bad, hex(offset)
    assert fn(data, len(original), True)
    fixed = data.raw
    assert (fixed != original) == changes
    if changes:
        assert not fn(data, len(original), True) and data.raw == fixed
    return fixed


def check_language(original, fixed):
    changed = [i for i in range(0, len(original), 4) if original[i:i+4] != fixed[i:i+4]]
    assert len(changed) == 2 and changed[1] == changed[0] + 28
    start = changed[0] - 16
    init, firmware, language, clear = [target(original, start+i) for i in (8, 12, 16, 24)]
    japanese, english = [u32(original, language-BASE+i) for i in (0xAC, 0xCC)]
    gui_string = u32(original, language-BASE+0xA0) + 0x28
    assert japanese != english
    for entry in (BASE+start, BASE+start+28):
        assert run_switch(original, entry, english, init, firmware, clear, gui_string) == japanese
        assert run_switch(fixed, entry, english, init, firmware, clear, gui_string) == english


def check_native_settings(arm9, site):
    options = target(arm9, site+16)
    reads = [i for i in range(site+16, site+0x170, 4)
             if u32(arm9, i) >> 24 == 0xEB and target(arm9, i) == options]
    assert len(reads) == 2
    gui, reset, sram = [target(arm9, site+i) for i in (0, 4, 8)]
    exists = target(arm9, site+32)
    printer = target(arm9, reads[1]-8)
    uc = machine(arm9)
    uc.mem_write(TITLE, b'TEST GAME\0')
    uc.reg_write(UC_ARM_REG_R11, 0x02FFF000)
    uc.mem_write(0x02FFF000-28, p32(0x123456))
    events = []

    def hook(uc, address, size, user):
        if address == options:
            section = bytes(uc.mem_read(uc.reg_read(UC_ARM_REG_R0), 32)).split(b'\0')[0]
            events.append(('read', section))
        elif address == gui:
            events.append(('gui', uc.reg_read(UC_ARM_REG_R0)))
        elif address in (reset, sram):
            events.append(('reset' if address == reset else 'sram',))
        elif address == exists:
            assert uc.reg_read(UC_ARM_REG_R0) == TITLE
            uc.reg_write(UC_ARM_REG_R0, 1)
        elif address != printer:
            return
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    uc.hook_add(UC_HOOK_CODE, hook)
    uc.emu_start(BASE+site-4, BASE+reads[1]+4, count=200)
    assert uc.reg_read(UC_ARM_REG_PC) == BASE+reads[1]+4
    assert events == [('gui', 0x123456), ('reset',), ('sram',),
                      ('read', b'Global'), ('read', b'TEST GAME')], events


def check_macro(original, fixed, site):
    for image, frames, want in [(original, 0, site+4), (original, 360, site+0xCC),
                                (fixed, 0, site+0xCC), (fixed, 360, site+0xCC)]:
        uc = machine(image)
        uc.mem_write(0x0300322C, p32(frames))
        uc.reg_write(UC_ARM_REG_R6, 0x03003000)
        uc.emu_start(BASE+site-12, 0, count=4)
        assert uc.reg_read(UC_ARM_REG_PC) == BASE+want
    assert fixed[:site] == original[:site] and fixed[site+4:] == original[site+4:]


def check_io(fixed, read):
    for write in (False, True):
        for result in (0, 1):
            uc = machine(fixed)
            driver = BASE+0x90000
            uc.mem_write(BASE+0x1870+(4 if write else 0), p32(driver))
            uc.mem_write(0x04000204, struct.pack('<H', 0xEFFF))
            uc.mem_write(0x04000208, p32(1))
            uc.reg_write(UC_ARM_REG_R0, 42)
            uc.reg_write(UC_ARM_REG_R1, 3)
            uc.reg_write(UC_ARM_REG_R2, BASE+0xA0000)
            uc.reg_write(UC_ARM_REG_R4, 0x12345678)
            calls = []

            def hook(uc, address, size, user):
                if address != driver:
                    return
                calls.append([uc.reg_read(reg) for reg in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2)])
                assert int.from_bytes(uc.mem_read(0x04000204, 2), 'little') == 0xE7FF
                assert u32(uc.mem_read(0x04000208, 4)) == 1
                uc.reg_write(UC_ARM_REG_R0, result)
                uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

            uc.hook_add(UC_HOOK_CODE, hook)
            uc.emu_start(BASE+read+(0x194 if write else 0), RETURN, count=100)
            assert uc.reg_read(UC_ARM_REG_PC) == RETURN
            assert calls == [[42, 3, BASE+0xA0000]]
            assert uc.reg_read(UC_ARM_REG_R0) == result
            assert uc.reg_read(UC_ARM_REG_R4) == 0x12345678
            assert int.from_bytes(uc.mem_read(0x04000204, 2), 'little') == 0xEFFF
            assert u32(uc.mem_read(0x04000208, 4)) == 1


def check_twl_startup(arm9, arm7, get_language, dsp):
    # These are the actual startup writes that select 4 MiB before the GUI
    # reads firmware language. The test models the resulting stale low alias.
    ext7, ext9 = (0x70E0, 0x2804C) if dsp else (0x7040, 0x23D94)
    assert u32(arm7, ext7) == 0x90FF0B80
    assert u32(arm9, ext9) == 0x80070180
    site = 0x28380 if dsp else 0x240C8
    uc = machine(arm9)
    uc.mem_write(0x02FFF634, b'\x01')  # English firmware in the high IPC page.
    uc.mem_write(0x02FFF000, bytes(uc.mem_read(0x023FF000, 0x1000)))
    uc.emu_start(BASE+site, RETURN, count=4)
    assert uc.reg_read(UC_ARM_REG_R0) == 0  # Unpatched: stale Japanese alias.
    for language in range(8):
        data = ctypes.create_string_buffer(arm9, len(arm9))
        assert get_language(data, len(arm9), language, True)
        uc = machine(data.raw)
        uc.emu_start(BASE+site, RETURN, count=4)
        assert uc.reg_read(UC_ARM_REG_R0) == language

    # Actual SoundPowerON initializes NTR power/mixer/amp, but leaves TWL
    # SNDEXTCNT inherited. Stub the power-management SPI and IRQ helpers.
    sound = 0x7ECC if dsp else 0x7E2C
    uc = machine(b'')
    uc.mem_map(0x03800000, 0x10000)
    uc.mem_write(0x03800000, arm7)
    uc.mem_write(0x04004700, struct.pack('<H', 0x4000))
    uc.reg_write(UC_ARM_REG_R0, 127)
    callees = [target(arm7, sound+i)-BASE+0x03800000 for i in (0x1C, 0x2C, 0x44)]

    def hook(uc, address, size, user):
        if address in callees:
            uc.reg_write(UC_ARM_REG_R0, 0)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    uc.hook_add(UC_HOOK_CODE, hook)
    uc.emu_start(0x03800000+sound, RETURN, count=100)
    assert uc.reg_read(UC_ARM_REG_PC) == RETURN
    assert int.from_bytes(uc.mem_read(0x04004700, 2), 'little') == 0x4000
    assert int.from_bytes(uc.mem_read(0x04000500, 2), 'little') == 0x807F


if __name__ == '__main__':
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    with tempfile.TemporaryDirectory() as tmp:
        so = Path(tmp)/'patches.so'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        '-I'+str(ROOT/'include'), str(ROOT/'source/tgds_patch.c'),
                        str(ROOT/'source/tgds_io.c'), str(ROOT/'source/header.c'), '-o', str(so)], check=True)
        lib = ctypes.CDLL(str(so))
        for name in ('shim_tgds_language_patch', 'shim_tgds_game_settings_patch',
                     'shim_tgds_macro_timer_patch', 'shim_tgds_dspico_io', 'shim_tgds_twl_touch'):
            fn = getattr(lib, name)
            fn.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_bool]
            fn.restype = ctypes.c_bool
        dldi = lib.shim_tgds_dldi_patch_offset
        dldi.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        dldi.restype = ctypes.c_size_t
        for path, (digest, macro, gui, read, fs) in zip(sys.argv[1:], BUILDS):
            raw = Path(path).read_bytes()
            assert hashlib.sha256(raw).hexdigest() == digest, path
            offset, _, _, size = struct.unpack_from('<4I', raw, 0x20)
            arm9 = raw[offset:offset+size]
            switch = next(i for i in range(0, len(arm9)-56, 4)
                          if arm9[i:i+8] == struct.pack('<2I', 0xE92D4008, 0xE3A00000)
                          and arm9[i+28:i+36] == struct.pack('<2I', 0xE92D4008, 0xE3A00001))
            fixed = checked_patch(lib.shim_tgds_language_patch, arm9,
                                  [switch, switch+16, switch+44], exact_size=False)
            check_language(arm9, fixed)
            checked_patch(lib.shim_tgds_game_settings_patch, arm9, [gui, gui+16], changes=False)
            check_native_settings(arm9, gui)
            fixed_macro = checked_patch(lib.shim_tgds_macro_timer_patch, arm9, [macro, macro+8])
            check_macro(arm9, fixed_macro, macro)
            header = lib.shim_check_header
            header.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_bool]
            header.restype = ctypes.c_char_p
            assert header(raw[:512], 512, len(raw), bool(read)) is None
            if read:
                fixed_io = checked_patch(lib.shim_tgds_dspico_io, arm9, [read, read+0x194, fs, 0x1841])
                assert fixed_io[fs:fs+0x4C] == arm9[fs:fs+0x4C]  # no unnecessary diagnostic edit
                check_io(fixed_io, read)
                get_language = lib.shim_tgds_twl_language_patch
                get_language.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_bool]
                get_language.restype = ctypes.c_bool
                offset7, _, _, size7 = struct.unpack_from('<4I', raw, 0x30)
                arm7 = raw[offset7:offset7+size7]
                check_twl_startup(arm9, arm7, get_language, macro == 0x16120)
                branch = dldi(arm7, len(arm7))
                assert branch == (0x6494 if macro == 0x16120 else 0x63F4)
                # DSi (type 2) falls through to console SD in the stock code.
                # The replacement enters the existing DLDI path instead.
                for instruction, want in [(u32(arm7, branch), branch+4), (0xEA00000B, branch+52)]:
                    uc = machine(b'')
                    uc.mem_map(0x03800000, 0x10000)
                    uc.mem_write(0x03800000, arm7)
                    uc.mem_write(0x03800000+branch, p32(instruction))
                    uc.mem_write(0x02010000, p32(2))
                    uc.reg_write(UC_ARM_REG_R0, 0)
                    uc.reg_write(UC_ARM_REG_R3, 0x02010000)
                    uc.emu_start(0x03800000+branch-8, 0, count=3)
                    assert uc.reg_read(UC_ARM_REG_PC) == 0x03800000+want
                delta, vb, vc = (0x80, 0xDBE8, 0xDC70) if macro == 0x16120 else (0x120, 0xDAAC, 0xDB34)
                checked_patch(lib.shim_tgds_twl_touch, arm7,
                              [0x8AAC-delta, 0x920C-delta, 0x914C-delta, 0xA0EC-delta, vb, vc])
            print('PASS:', Path(path).name, 'headers, language, native game settings, Macro timer'+
                  (', DSpico sector routing and touch fingerprints' if read else ''))
    print('LIMIT: modeled calls/registers, no physical console or game compatibility test.')
