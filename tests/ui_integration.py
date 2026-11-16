#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the production menu, real INI writes, and launch argument decisions."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'build/ui_harness'
subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT/'tests/mock'), '-I' + str(ROOT/'include'),
                *[str(ROOT/p) for p in ('tests/ui_harness.c', 'source/arguments.c',
                                       'source/config.c', 'source/preferences.c', 'source/ui.c',
                                       'source/versions.c', 'source/rom.c')],
                '-o', str(EXE)], check=True)

def run(*, dsi=True, mode='auto', autoboot=False, skip_macro_timer=False,
        keys='', select=False,
        device='fat:', rom=True, missing=False, bad=False, auto_version=None,
        variant='stable', missing_alt=False, extra_ini=''):
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)/device
        apps, roms = base/'apps', base/'ROMs/SNES'
        apps.mkdir(parents=True); roms.mkdir(parents=True)
        for name in ['SNEmulDS.nds', 'SNEmulDS.srl', 'SNEmulDS_0.6a.nds']:
            if not missing: (apps/name).write_bytes(b'fake emulator; hardware preflight stubbed')
        for name in ['SNEmulDS_dsp.nds', 'SNEmulDS_dsp.srl',
                     'SNEmulDS_sdd1.nds', 'SNEmulDS_sdd1.srl']:
            if not missing_alt: (apps/name).write_bytes(b'fake alternate emulator')
        data = bytearray(0x100000)
        data[0x7FC0:0x7FD5] = b'RENAMED TEST GAME    '.ljust(21, b' ')
        data[0x7FD5:0x7FDB] = {
            'stable': bytes([0x20, 0x02, 10, 0, 1, 1]),
            'dsp': bytes([0x20, 0x03, 10, 0, 1, 1]),
            'sdd1': bytes([0x32, 0x43, 10, 0, 1, 1]),
            'dsp2': bytes([0x20, 0x05, 10, 0, 1, 1]),
            'dsp3': bytes([0x30, 0x05, 10, 0, 1, 0xB2]),
            'dsp4': bytes([0x30, 0x03, 10, 0, 1, 1]),
        }[variant]
        data[0x7FDC:0x7FE0] = bytes([0xCB, 0xED, 0x34, 0x12])
        data[0x7FFD] = 0x80
        (roms/'Super Mario World.sfc').write_bytes(data)
        ini = apps/'snemulds-shim.ini'
        ini.write_text(f'; preserve\n[snemulds]\nautoboot={str(autoboot).lower()}\n'
                       f'default={mode}\nskip_macro_timer={str(skip_macro_timer).lower()}\n'
                       + (f'auto_version={str(auto_version).lower()}\n' if auto_version is not None else '')
                       + extra_ini)
        original = b'# keep\nROMPath=/old\nSound=1\n[Global]\nROMPath=/other\n'
        cfg = base/'snemul.cfg'; cfg.write_bytes(original)
        env = dict(os.environ, TEST_DSI=str(int(dsi)), TEST_DEVICE=device, TEST_KEYS=keys)
        if select: env['TEST_SELECT'] = '1'
        if bad: env['TEST_BAD_HEADER'] = '1'
        args = [str(EXE), f'{device}/apps/shim.nds']
        if rom: args.append(f'{device}/ROMs/SNES/Super Mario World.sfc')
        p = subprocess.run(args, env=env, cwd=tmp, capture_output=True, text=True, timeout=5)
        assert p.returncode == 0, (p.returncode, p.stderr, p.stdout)
        return p.stdout, ini.read_text(), cfg.read_bytes(), original

def menu_screens(out):
    return [screen for screen in out.split('<CLEAR>') if 'LAUNCH OPTIONS' in screen]

def patch_screen(out):
    return next(screen for screen in out.split('<CLEAR>') if 'Patches                   3/5' in screen)

for dsi in (False, True):
    for mode, filename in [('auto', 'SNEmulDS.srl' if dsi else 'SNEmulDS.nds'),
                           ('ntr', 'SNEmulDS.nds'), ('legacy', 'SNEmulDS_0.6a.nds')]:
        out, ini, cfg, old = run(dsi=dsi, mode=mode, autoboot=True)
        assert f'<BOOT {"TWL" if dsi and mode == "auto" else "NTR"} fat:/apps/{filename} ' in out
        assert 'LAUNCH OPTIONS' not in out  # no menu on successful autoboot
        if mode == 'legacy':
            assert cfg == old.replace(b'ROMPath=/old', b'ROMPath = /ROMs/SNES')
            assert '<ARG ' not in out and ' 0>' in out
        else:
            assert cfg == old
            assert '<ARG fat:/ROMs/SNES/Super Mario World.sfc>' in out

out, *_ = run(autoboot=True, select=True, keys='RRRRLX')
assert '<BOOT ' not in out and 'Autoboot bypassed' in out
assert 'Overview' in out and 'Paths' in out and out.count('LAUNCH OPTIONS') == 2
assert all('Hold SELECT to show this menu' in screen for screen in menu_screens(out))
out, *_ = run(dsi=False, keys='X')
assert 'NTR/TWL' not in out and 'NTR Forced' not in out and '0.6a   Classic' in out
out, *_ = run(keys='X')
assert '0.6d   NTR' in out and '  DS mode (NTR)' in out
assert 'NTR Forced' not in out and 'Always DS mode (NTR)' not in out
assert 'Hold SELECT to show this menu' not in out
out, *_ = run(keys='DDDX')  # Autoboot selected while off
assert all('Hold SELECT to show this menu' not in screen for screen in menu_screens(out))
out, *_ = run(keys='DDDADX')  # enable Autoboot, then move to Default
screens = menu_screens(out)
assert all('Hold SELECT to show this menu' not in screen for screen in screens[:-2])
assert all('Hold SELECT to show this menu' in screen for screen in screens[-2:])
out, *_ = run(keys='DDDAAX')  # enable Autoboot, then turn it off again
screens = menu_screens(out)
assert 'Hold SELECT to show this menu' in screens[-2]
assert 'Hold SELECT to show this menu' not in screens[-1]
out, *_ = run(keys='DA')
assert '<BOOT NTR fat:/apps/SNEmulDS.nds ' in out
out, _, cfg, old = run(keys='DDA')
assert '<BOOT NTR fat:/apps/SNEmulDS_0.6a.nds 0>' in out and cfg != old
out, ini, *_ = run(keys='DDDAD>A')  # enable autoboot, advance default twice
assert 'autoboot = true' in ini and 'default = legacy' in ini
out, ini, *_ = run(keys='UUUU<X')  # wrap through all four settings to Autoboot
assert 'autoboot = true' in ini  # left toggles Autoboot
out, ini, *_ = run(keys='UUAX')  # wrap past Auto ROM version to Macro timer
assert 'skip_macro_timer = true' in ini and 'Skip GBA Macro timer On' in out
out, *_ = run(skip_macro_timer=True, keys='A')
assert '<MACRO 1>' in out
out, *_ = run(skip_macro_timer=True, mode='legacy', autoboot=True)
assert '<MACRO 0>' in out  # legacy never receives the patch
out, *_ = run(skip_macro_timer=True, keys='RRRX')
screen = patch_screen(out)
for patch in ('Touchscreen fix', 'DSpico DLDI selection', 'DSpico ARM9 sector I/O',
              'Game settings load', 'Language selection', 'DSi audio setup',
              'Skip GBA Macro timer'):
    assert patch in screen, patch
out, *_ = run(device='sd:', keys='RRRX')
screen = patch_screen(out)
for patch in ('Touchscreen fix', 'Game settings load', 'Language selection'):
    assert patch in screen, patch
assert 'DLDI selection' not in screen and 'DSpico ARM9' not in screen
assert 'DSi audio setup' not in screen
assert 'Skip GBA Macro timer' not in screen
out, *_ = run(mode='ntr', skip_macro_timer=True, keys='RRRX')
screen = patch_screen(out)
assert 'Game settings load' in screen and 'Language selection' in screen
assert 'Skip GBA Macro timer' in screen and 'Touchscreen fix' not in screen
out, *_ = run(mode='legacy', skip_macro_timer=True, keys='RRRX')
assert 'No patches required for 0.6a' in patch_screen(out)
out, _, cfg, old = run(autoboot=True, mode='legacy', device='sd:', keys='BX')
assert '<BOOT ' not in out and 'NTR needs flashcart storage' in out and cfg == old
out, _, cfg, old = run(autoboot=True, device='sd:')
assert '<BOOT TWL sd:/apps/SNEmulDS.srl ' in out and cfg == old
for flags in ({'missing': True}, {'bad': True}):
    out, _, cfg, old = run(autoboot=True, mode='legacy', keys='BX', **flags)
    assert '<BOOT ' not in out and cfg == old and 'LAUNCH OPTIONS' in out
out, _, cfg, old = run(autoboot=True, mode='legacy', rom=False)
assert '<BOOT NTR ' in out and cfg == old
out, _, cfg, old = run(autoboot=True, rom=False)
assert out.count('<ARG ') == 2 and cfg == old
for dsi in (False, True):
    for mode in ('auto', 'ntr', 'legacy'):
        out, *_ = run(dsi=dsi, mode=mode, keys='RRRX')
        assert ('Touchscreen fix' in out) == (dsi and mode == 'auto')
        assert ('Game settings load' in out) == (mode != 'legacy')
        assert ('Language selection' in out) == (mode != 'legacy')
        assert 'v0.10' not in out
out, *_ = run(keys='RRRRRLX')
assert out.index('SNEmulDS Versions          1/5') < out.index('Overview                  2/5')
assert out.index('Overview                  2/5') < out.index('Patches                   3/5')
assert out.index('Patches                   3/5') < out.index('Paths                     4/5')
assert out.index('Paths                     4/5') < out.index('Arguments                 5/5')
assert out.count('LAUNCH OPTIONS') == 2
out, *_ = run(keys='RRRRRRX')
assert out.count('SNEmulDS Versions          1/5') == 2  # R wraps back to the first page
out, *_ = run(keys='RRUX')
assert out.count('SNEmulDS Versions          1/5') == 2  # Up from Overview returns to Versions
out, *_ = run(keys='DDDX')
assert 'A/LEFT/RIGHT  Change' in out and 'Settings save automatically' not in out
out, *_ = run(autoboot=True, mode='ntr', device='sd:', keys='BX')
assert 'NTR needs flashcart storage' in out and 'Check paths on the info pages' not in out
out, *_ = run(keys='R' + 'D'*12 + 'U'*12 + 'RLX')
assert 'Lines 11-27 of 27' in out and 'Archeide. Only DS mode is' in out and 'well.' in out
assert 'version also has extended RAM' in out and 'is similar, but slightly worse' in out
assert out.count('Lines 1-17 of 27') == 2  # initial view and back at top
assert out.count('Overview') == 1 and out.count('LAUNCH OPTIONS') == 2

# Old INIs omit auto_version. Every 0.6d entry, including autoboot and forced
# NTR, selects the matching alternate only when explicitly enabled.
for enabled in (None, False, True):
    for dsi in (False, True):
        for mode in ('auto', 'ntr', 'legacy'):
            for variant in ('stable', 'dsp', 'sdd1'):
                out, ini, cfg, old = run(dsi=dsi, mode=mode, autoboot=True,
                                        auto_version=enabled, variant=variant)
                suffix = '_' + variant if enabled and mode != 'legacy' and variant != 'stable' else ''
                filename = ('SNEmulDS_0.6a.nds' if mode == 'legacy' else
                            f'SNEmulDS{suffix}.{"srl" if dsi and mode == "auto" else "nds"}')
                assert f'fat:/apps/{filename} ' in out, out
                if mode != 'legacy':
                    assert cfg == old and f'<ARG fat:/apps/{filename}>' in out
                assert out.count('<ROM PROBE>') == int(bool(enabled) and mode != 'legacy')
                if enabled is None: assert 'auto_version' not in ini  # no migration until save
for variant in ('dsp2', 'dsp3', 'dsp4'):
    out, *_ = run(autoboot=True, auto_version=True, variant=variant)
    assert '<BOOT TWL fat:/apps/SNEmulDS.srl ' in out
out, ini, *_ = run(keys='UA' + 'U'*6 + 'A', variant='dsp')
assert 'auto_version = true' in ini and '<BOOT TWL fat:/apps/SNEmulDS_dsp.srl ' in out
assert 'Auto ROM version On' in out and 'DSi mode (TWL) / DSP-1' in out
out, ini, *_ = run(keys='UA' + 'U'*6 + 'A', variant='dsp', auto_version=True)
assert 'auto_version = false' in ini and '<BOOT TWL fat:/apps/SNEmulDS.srl ' in out
out, *_ = run(autoboot=True, auto_version=True, variant='dsp', rom=False)
assert '<BOOT TWL fat:/apps/SNEmulDS.srl ' in out and out.count('<ARG ') == 2
out, *_ = run(autoboot=True, select=True, auto_version=True, variant='sdd1', keys='RRRRRX')
assert 'Autoboot bypassed' in out and '<ARG ' not in out
assert 'fat:/apps/SNEmulDS_sdd1.srl' in out  # real selected path on info pages
out, _, cfg, old = run(autoboot=True, auto_version=True, variant='dsp', missing_alt=True, keys='BX')
assert '<BOOT ' not in out and 'Required alternate build' in out and cfg == old, out
out, *_ = run(autoboot=True, auto_version=True, variant='sdd1', device='sd:')
assert '<BOOT TWL sd:/apps/SNEmulDS_sdd1.srl ' in out
out, _, cfg, old = run(autoboot=True, auto_version=True, variant='dsp', device='sd:', mode='ntr', keys='BX')
assert '<BOOT ' not in out and 'NTR needs flashcart storage' in out and cfg == old
out, *_ = run(autoboot=True, auto_version=True, variant='dsp',
             extra_ini='dsp_twl_path=../apps/SNEmulDS_dsp.srl\n')
assert '<BOOT TWL fat:/apps/SNEmulDS_dsp.srl ' in out
out, *_ = run(autoboot=True, auto_version=True, variant='dsp',
             extra_ini='dsp_twl_path=sd:/apps/SNEmulDS_dsp.srl\n', keys='BX')
assert '<BOOT ' not in out and 'Emulator must be on the same' in out
out, *_ = run(auto_version=True, variant='dsp', keys='RRRX')
assert 'Game settings load (native)' in patch_screen(out)
out, *_ = run(auto_version=True, variant='sdd1', keys='RRRRRLUUUUUUAX')
assert out.count('<ROM PROBE>') == 1  # menu redraws/info never reread the ROM
print('PASS: actual menu controls, mode filtering, autoboot/SELECT, saved settings, info pages, safe errors, and legacy-only ROMPath writes')
