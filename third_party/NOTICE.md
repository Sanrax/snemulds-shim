# Third-party source and notices

`nds-bootloader/` is based on [devkitPro/nds-bootloader](https://github.com/devkitPro/nds-bootloader)
at commit `69cea3c5b7f3278f4b63672d345d0009b7f7d62d` (pre-Calico).
Per-file authorship and GPL-2.0-or-later notices are retained. This release
uses the GPLv3-or-later option those notices permit; their original terms
remain available for the upstream files. The GPLv2 text is in
`GPL-2.0.txt`, and the project's GPLv3 text is in the top-level
[LICENSE](../LICENSE). Principal contributors include Michael Chisholm and
Dave Murphy; original reset routines also credit Darkain.

The BlocksDS port uses `bootloader/arm9_stages.s` for the ARM9 handoff,
freestanding helpers in `bootloader/support.c`, and an offset-based DLDI
relocator in `source/`. It also adds internal-SD support, checked in-memory
TGDS storage and TWL input patches, DSpico sector wrappers, and separate
NTR/TWL handoff code. The DSpico DLDI driver is supplied by the launching
menu and is not bundled.

`nds-bootloader/source/legacy_sdmmc.h` is the unmodified libnds v1.8.0
[header](https://github.com/devkitPro/libnds/blob/v1.8.0/include/nds/arm7/sdmmc.h).
It avoids a collision with the newer BlocksDS SDMMC API. Its license is in
`libnds-legacy-LICENSE`.

The forced-NTR codec sequence is derived from LNH-team/Pico-loader
`arm7/source/loader/DSMode.cpp` at
`67a453fff05c7f63f6e32a5ef10390bb87ec7178` (Copyright 2025 LNH team,
Zlib). Register definitions come from Gericom/libtwl
`libtwl7/include/libtwl/spi/spiCodec.h` at
`5cc15f26a225b6ef770fed486294c0e4848b7c73` (Zlib).
The source references and licenses are in `pico-reference/` and are used by
`patches/ntr_codec_table.py`; the C++ reference is not compiled. Original
projects: [Pico-loader](https://github.com/LNH-team/pico-loader) and
[libtwl](https://github.com/Gericom/libtwl).

BlocksDS 1.24.0, its libnds/default ARM7 core, and Wonderful runtime
libraries are linked under their respective licenses. SDK notices are in
`blocksds-licenses/`. Emulator binaries, game ROMs, and third-party
flashcart DLDI drivers are not redistributed here.
