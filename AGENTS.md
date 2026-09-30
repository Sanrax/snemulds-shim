# Project instructions for Codex

## Project scope

This is the v0.14 source for the SNEmulDS launcher shim. It launches
cotodevel SNEmulDS 0.6d in NTR or TWL mode, and Archeide SNEmulDS 0.6a in NTR
mode. Preserve the mode, storage, argument, and configuration behavior
documented in `README.md` when making changes. Keep the NTR/TWL launch paths
separate where their hardware requirements differ.

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
- Do not touch `README.md` without the user's knowledge. Before any edit,
  tell the user what you plan to change and why. Keep user-visible controls,
  labels, and help text consistent with the behavior in `README.md`. Update it
  when behavior, supported paths, or validation changes, after giving that
  notice.
- Do not hand-edit generated artifacts. `patches/build.py` generates
  `include/twl_input_generated.h`, and `patches/ntr_codec_table.py` generates
  `include/ntr_codec_generated.h`. `make patches` runs both generators; `make`
  also builds the embedded loader at `data/load.bin`.
- Use `GPL-3.0-or-later` for project-authored files. Preserve the original
  notices and licenses of vendored code under `third_party/`.
- Follow the existing C style and keep the source's SPDX notices. The host
  tests compile with warnings treated as errors.

## Build and checks

The verified build used BlocksDS 1.24.0, Wonderful's ARM toolchain, and Python
3. With those installed, build `snemulds-shim.nds` with:

```sh
make
```

`make` builds the shim without running tests. The top-level Makefile uses
`/opt/blocksds/core` unless `BLOCKSDS` is set. The ARM toolchain defaults to
`/opt/wonderful`; set `WONDERFUL_TOOLCHAIN` for another location. `make clean`
only removes generated build files. Run `make` afterward for a clean rebuild.

For the portable checks, install a host C compiler and run:

```sh
make test
python3 tests/verify_build.py
```

`make test` compiles and runs the host tests, including the menu integration
check; it does not build the `.nds`. Run `make` first because
`tests/verify_build.py` inspects the built `.nds` and embedded loader. These
portable checks do not require Python `unicorn`.

For changes to handoff or mode transitions, also run the relevant modeled
instruction checks where the ARM toolchain and Python `unicorn` package are
available:

```sh
python3 tests/handoff_emulation.py
python3 tests/ntr_handoff_emulation.py
```

The TWL instruction tests need the user's stock SNEmulDS SRL; the DSpico IO
test also needs a DSpico DLDI driver:

```sh
python3 tests/twl_input_emulation.py /path/to/stock/SNEmulDS.srl
python3 tests/twl_io_emulation.py /path/to/stock/SNEmulDS.srl /path/to/DSpico.dldi
```

`tests/` comments describe the limits of each model. Host tests and instruction
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
contains the built shim, source, sample INI, tests, and notices. Review the
manifest and archive before distributing them. Build intermediates and local
caches are excluded; the generated binary and manifest are not tracked in Git.
