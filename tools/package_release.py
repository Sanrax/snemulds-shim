#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package the built shim with the public source and license files."""
from pathlib import Path
import hashlib
import zipfile

root = Path(__file__).resolve().parents[1]
binary = root / 'snemulds-shim.nds'
if not binary.is_file():
    raise SystemExit('Build snemulds-shim.nds before packaging.')

top_level = (
    '.gitignore', 'AGENTS.md', 'LICENSE', 'Makefile', 'Makefile.arm9',
    'README.md', 'snemulds-shim.ini', 'snemulds-shim.nds',
)
source_dirs = ('bootloader', 'include', 'patches', 'source',
               'tests', 'third_party', 'tools')
files = [root / name for name in top_level]
for directory in source_dirs:
    files.extend(p for p in (root / directory).rglob('*') if p.is_file()
                 and not any(part.startswith('.') or part == '__pycache__'
                             for part in p.relative_to(root).parts)
                 and p.suffix != '.pyc')
files = sorted(files)
if not all(p.is_file() for p in files):
    raise SystemExit('A required release file is missing.')
manifest = root/'SHA256SUMS'
manifest.write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(root).as_posix()}\n'
                            for p in files))
output = root.parent/(root.name + '.zip')
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for p in sorted(files + [manifest]):
        z.write(p, root.name + '/' + p.relative_to(root).as_posix())
with zipfile.ZipFile(output) as z:
    assert z.testzip() is None
    for line in manifest.read_text().splitlines():
        digest, name = line.split('  ', 1)
        assert hashlib.sha256(z.read(root.name + '/' + name)).hexdigest() == digest
print(f'{output.name}: {len(files) + 1} files, {output.stat().st_size} bytes; manifest verified')
print('SHA-256:', hashlib.sha256(output.read_bytes()).hexdigest())
