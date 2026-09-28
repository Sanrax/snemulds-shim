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
                                       'source/versions.c')],
                '-o', str(EXE)], check=True)

def run(*, dsi=True, mode='auto', autoboot=False, keys='', select=False,
        device='fat:', rom=True, missing=False, bad=False):
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)/device
        apps, roms = base/'apps', base/'ROMs/SNES'
        apps.mkdir(parents=True); roms.mkdir(parents=True)
        for name in ['SNEmulDS.nds', 'SNEmulDS.srl', 'SNEmulDS_0.6a.nds']:
            if not missing: (apps/name).write_bytes(b'fake emulator; hardware preflight stubbed')
        (roms/'Super Mario World.sfc').write_bytes(b'fake ROM')
        ini = apps/'snemulds-shim.ini'
        ini.write_text(f'; preserve\n[snemulds]\nautoboot={str(autoboot).lower()}\ndefault={mode}\n')
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
    return [screen for screen in out.split('<CLEAR>') if 'BOOT OPTIONS' in screen]

for dsi in (False, True):
    for mode, filename in [('auto', 'SNEmulDS.srl' if dsi else 'SNEmulDS.nds'),
                           ('ntr', 'SNEmulDS.nds'), ('legacy', 'SNEmulDS_0.6a.nds')]:
        out, ini, cfg, old = run(dsi=dsi, mode=mode, autoboot=True)
        assert f'<BOOT {"TWL" if dsi and mode == "auto" else "NTR"} fat:/apps/{filename} ' in out
        assert 'BOOT OPTIONS' not in out  # no menu on successful autoboot
        if mode == 'legacy':
            assert cfg == old.replace(b'ROMPath=/old', b'ROMPath = /ROMs/SNES')
            assert '<ARG ' not in out and ' 0>' in out
        else:
            assert cfg == old
            assert '<ARG fat:/ROMs/SNES/Super Mario World.sfc>' in out

out, *_ = run(autoboot=True, select=True, keys='RRRLX')
assert '<BOOT ' not in out and 'Autoboot bypassed' in out
assert 'Overview' in out and 'Paths' in out and out.count('BOOT OPTIONS') == 2
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
out, ini, *_ = run(keys='UU<X')  # wrap to Autoboot, then Default left
assert 'autoboot = true' in ini  # left toggles Autoboot
out, _, cfg, old = run(autoboot=True, mode='legacy', device='sd:', keys='BX')
assert '<BOOT ' not in out and 'NTR needs flashcart storage' in out and cfg == old
out, _, cfg, old = run(autoboot=True, device='sd:')
assert '<BOOT TWL sd:/apps/SNEmulDS.srl ' in out and cfg == old
for flags in ({'missing': True}, {'bad': True}):
    out, _, cfg, old = run(autoboot=True, mode='legacy', keys='BX', **flags)
    assert '<BOOT ' not in out and cfg == old and 'BOOT OPTIONS' in out
out, _, cfg, old = run(autoboot=True, mode='legacy', rom=False)
assert '<BOOT NTR ' in out and cfg == old
out, _, cfg, old = run(autoboot=True, rom=False)
assert out.count('<ARG ') == 2 and cfg == old
for dsi in (False, True):
    for mode in ('auto', 'ntr', 'legacy'):
        out, *_ = run(dsi=dsi, mode=mode, keys='RRX')
        assert ('TWL touchscreen + audio fixes' in out) == (dsi and mode == 'auto')
        assert 'v0.10' not in out
out, *_ = run(keys='RRRRLX')
assert out.index('SNEmulDS Versions          1/4') < out.index('Overview                  2/4')
assert out.index('Overview                  2/4') < out.index('Paths                     3/4')
assert out.index('Paths                     3/4') < out.index('Arguments                 4/4')
assert out.count('BOOT OPTIONS') == 2
out, *_ = run(keys='RRRRRX')
assert out.count('SNEmulDS Versions          1/4') == 2  # R wraps back to the first page
out, *_ = run(keys='RRUX')
assert out.count('SNEmulDS Versions          1/4') == 2  # Up from Overview returns to Versions
out, *_ = run(keys='DDDX')
assert 'A/LEFT/RIGHT  Change' in out and 'Settings save automatically' not in out
out, *_ = run(autoboot=True, mode='ntr', device='sd:', keys='BX')
assert 'NTR needs flashcart storage' in out and 'Check paths on the info pages' not in out
out, *_ = run(keys='R' + 'D'*12 + 'U'*12 + 'RLX')
assert 'Lines 11-27 of 27' in out and 'Archeide. Only DS mode is' in out and 'well.' in out
assert 'version also has extended RAM' in out and 'is similar, but slightly worse' in out
assert out.count('Lines 1-17 of 27') == 2  # initial view and back at top
assert out.count('Overview') == 1 and out.count('BOOT OPTIONS') == 2
print('PASS: actual menu controls, mode filtering, autoboot/SELECT, saved settings, info pages, safe errors, and legacy-only ROMPath writes')
