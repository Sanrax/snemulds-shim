# Project instructions for Codex

## Project scope

This is the v0.16 source for the SNEmulDS launcher shim. It launches
cotodevel SNEmulDS 0.6d in NTR or TWL mode, and Archeide SNEmulDS 0.6a in NTR
mode. Preserve the mode, storage, argument, and configuration behavior
documented in `README.md` when making changes. Keep the NTR/TWL launch paths
separate where their hardware requirements differ.

Optional `auto_version` selects the DSP-1/S-DD1 0.6d builds from ROM headers.
The sample INI enables it; older INIs omitting the key keep stable-only behavior.
See `docs/automatic-versions.md` for game requirements and the alternate patch
audit. Those builds already load game settings natively; do not apply stable's
settings veneer to them.

The emulator binaries and ROMs are not included. The TWL SRL is patched in
memory during launch; do not rewrite a user's emulator binary as part of the
normal shim workflow. The 0.6a path updates its `snemul.cfg` ROM folder only
when 0.6a is selected with a ROM argument.

TWL launches apply the touchscreen fix and prepare DSi audio. DSpico DSi-mode
launches also select DLDI storage instead of console SD and route the SRL's
ARM9 sector reads and writes through the DSpico driver.

## Editing guidance

- Prefer focused changes and preserve existing INI compatibility. The
  `snemulds-shim.ini` sits beside the shim; relative paths are resolved from
  that directory.
- Do not touch `README.md`. If it needs to be updated, notify the user, but do
  not make any changes to the file unless explicitly asked to.
- Do not hand-edit generated artifacts. `patches/build.py` generates
  `include/twl_input_generated.h`, and `patches/ntr_codec_table.py` generates
  `include/ntr_codec_generated.h`. `make patches` runs both generators; `make`
  also builds the embedded loader at `data/load.bin`.
- Use `GPL-3.0-or-later` for project-authored files. Preserve the original
  notices and licenses of vendored code under `third_party/`.
- Follow the existing C style and keep the source's SPDX notices.

## Branch layout

The `master` branch contains the shim's build sources and release tooling.
All host tests, mocks, build verification and modeled instruction checks live
on the separate `tests` branch alongside their matching shim sources. That
branch retains `make test` and the detailed check commands in its `AGENTS.md`.
Keep test code and test-only Makefile targets off `master`.

## Build

The verified build used BlocksDS 1.24.0, Wonderful's ARM toolchain, and Python
3. With those installed, build `snemulds-shim.nds` with:

```sh
make
```

The top-level Makefile uses `/opt/blocksds/core` unless `BLOCKSDS` is set.
The ARM toolchain defaults to
`/opt/wonderful`; set `WONDERFUL_TOOLCHAIN` for another location. `make clean`
only removes generated build files. Run `make` afterward for a clean rebuild.

The `tests` branch describes the limits of each model. Host tests and instruction
emulation do not prove behavior on a console or measure real audio, touch, or
frame timing. Report hardware testing only when it was actually performed;
the user has confirmed v0.11-rc1 launch behavior on DSpico/DSi and DS Lite,
and has confirmed that the language patch allows English to be forced on a
Japanese DSi. This language confirmation does not cover other v0.14 UI or
behavior changes, which have software checks but no new physical hardware
check in this workspace.

## Packaging

For a release, build the shim, update the documentation, then run:

```sh
python3 tools/package_release.py
```

This creates `SHA256SUMS` and a zip next to the project directory. The zip
contains the built shim, source, sample INI, documentation, and notices. Review
the manifest and archive before distributing them. Build intermediates and local
caches are excluded; the generated binary and manifest are not tracked in Git.
