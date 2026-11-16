<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Automatic 0.6d version selection (v0.16)

The launch menu's **Auto ROM version** option controls `auto_version` in
`snemulds-shim.ini`. It applies to every 0.6d entry, including autoboot,
DS mode, DSi mode and forced NTR. It does not change 0.6a.

The supplied INI enables it. An older INI with no `auto_version` key, or no
INI at all, keeps stable-only behavior. Saving settings from the menu adds
the key with its current value and preserves custom paths and other content.

Extract the emulator binaries from the following upstream archives:

- [DSP-1 build](https://github.com/cotodevel/SnemulDS/archive/dsp.zip)
- [S-DD1 build](https://github.com/cotodevel/SnemulDS/archive/sdd1.zip)

The defaults expect these files beside the shim and INI:

| Build | NTR path/key | TWL path/key |
| --- | --- | --- |
| Stable | `ntr_path = SNEmulDS.nds` | `twl_path = SNEmulDS.srl` |
| DSP-1 | `dsp_ntr_path = SNEmulDS_dsp.nds` | `dsp_twl_path = SNEmulDS_dsp.srl` |
| S-DD1 | `sdd1_ntr_path = SNEmulDS_sdd1.nds` | `sdd1_twl_path = SNEmulDS_sdd1.srl` |

Relative paths are resolved from the INI/shim directory. The selected build
must be on the same card as the shim and ROM. Only the selected emulator
needs to be installed. A missing required alternate produces an error with
the path available on the Paths info page. Turn the option off to force stable.

## ROM identification

The selector seeks to at most three 64-byte cartridge headers (LoROM, HiROM,
ExHiROM), accounting for an optional 512-byte copier header. It validates
the header location, mapping, size fields, reset vector and stored checksum/
complement pair, then examines the cartridge chipset. It never hashes or
loads the whole game. The result is cached for the current shim invocation;
disabled switching and launches without a ROM argument do not inspect a ROM.

The DSP subtype rules follow [Snes9x's cartridge detection](https://github.com/snes9xgit/snes9x/blob/master/memmap.cpp).
They exclude DSP-2 (Dungeon Master), DSP-3 (SD Gundam GX), DSP-4 (Top Gear
3000) and unrelated chips such as SA-1, SuperFX, CX4, ST010/011 and SPC7110.
S-DD1 is identified by mapping `0x32` and chipset `0x43` or `0x45`.

Names, region suffixes and ROM filenames do not affect selection. Regional
revisions, translations and hacks retaining a recognizable header select
the same build. Invalid, ambiguous, unreadable, truncated or unrecognized
headers select stable. Interleaved images and decompressed conversions with
different cartridge metadata are outside this detector's scope.

## Games requiring the alternate coprocessors

These are cartridge hardware requirements, not a claim that each game plays
correctly in SNEmulDS. The reference lists are the
[MAME SNES cartridge metadata](https://github.com/mamedev/mame/blob/master/hash/snes.xml)
and [bsnes cartridge database](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/Database/Super%20Famicom.bml).
Regional names below refer to the same game where joined with a slash.

DSP-1 / DSP-1A / DSP-1B retail games:

- Ace o Nerae! (also romanized Ace wo Nerae!)
- Ballz 3D / 3-jigen Kakutou Ballz
- Bike Daisuki! Hashiriya Tamashii / Rider's Spirits
- Drift King Shutokou Battle '94: Tsuchiya Keiichi & Bandou Masaaki
- Drift King Shutokou Battle 2: Tsuchiya Keiichi & Bandou Masaaki
- Final Stretch
- Michael Andretti's IndyCar Challenge
- Pilotwings
- Soukou Kihei Votoms: The Battling Road
- Super Air Diver / Lock On
- Super Air Diver 2
- Super Bases Loaded II / Super 3D Baseball / Hanguk Pro Yagu
- Super F1 Circus Gaiden
- Super Mario Kart
- Suzuka 8 Hours

DSP-1 prototypes/demos can also be selected if they carry a valid header.
Nintendo PowerFest '94 uses additional event hardware; detecting DSP-1 does
not imply support for that board. Power Rangers Zeo: Battle Racers uses
SA-1 and is not a DSP-1 game.

S-DD1 retail games:

- Star Ocean
- Street Fighter Alpha 2 / Street Fighter Zero 2

## Alternate-build patch audit

The shipped binaries were inspected separately from stable. Offsets here are
relative to the corresponding ARM image, not the complete NDS/SRL file.

| Behavior | DSP-1 and S-DD1 findings | Shim action |
| --- | --- | --- |
| Per-game settings | `reloadROM()` already reads Global, finds the game section and reads its options. Modeled calls confirm this order. | Validate the native code; **do not patch it**. The info page says `(native)`. |
| Configured GUI language | Both display switches still call `GUI_setLanguage(getLanguage())`, overwriting the configured language. Instruction tests reproduce this before patching and retain the configured language afterward. | Skip those two later language calls in NTR and TWL. |
| TWL system language | Both ARM7 startups still select 4 MiB RAM (`SCFG_EXT7 = 0x90FF0B80`), while ARM9 `getLanguage()` reads `0x02FFF634` through the remapped alias. ARM9 startup also selects 4 MiB. | Capture firmware language in `getLanguage()` in TWL only; explicit config overrides still work. |
| TWL touchscreen | Both retain the same stopped codec conversions, delayed initialization, raw-key overwrite and 600-frame timeout. The original codec initialization preserves stop/disable bits; instruction tests confirm the replacement clears them and publishes calibrated input. | Apply the touch fix at each build's validated locations, linked against that build's codec and division routines. Preserve the alternates' newer lid/sleep handling. |
| TWL flashcart selection | Both ARM7 `ARM7InitDLDI()` routines still choose console SD on DSi when it appears available. | Force the existing DLDI branch for flashcart launches only. |
| DSpico TWL sector I/O | Both ARM9 read/write paths still send ARM7 IPC requests using bounce buffers; they do not call the ARM9 DSpico driver. | Route sectors through that driver only when the PICO DLDI is identified. Preserve driver results, caller buffers, interrupt state and card ownership. No new FS error-message patch is applied to alternates. |
| DSi audio | Both still use TGDS `SoundPowerON()`, which sets the NTR mixer/power controls and leaves `SNDEXTCNT` inherited. | Retain DSi audio preparation during TWL flashcart handoff. Console-SD launches retain their existing setup. |
| GBA Macro countdown | Both still compare the frame counter to 359 and execute the countdown. | Bypass only when `skip_macro_timer` is enabled. This is an optional preference, not a coprocessor bug fix. |

The alternate native settings path must remain intact: its GUI call also
sets the DSP-1/S-DD1 emulation flag before reset/memory mapping. The shim
does not reorder these calls or transplant stable's settings veneer.

Useful audit locations:

| ARM image routine | DSP-1 NTR / TWL | S-DD1 NTR / TWL |
| --- | --- | --- |
| ARM9 native settings GUI call | `0x141A0` / `0x141A0` | `0x1030C` / `0x1030C` |
| ARM9 Macro branch | `0x16120` / `0x16120` | `0x124D8` / `0x124D8` |
| ARM9 TWL getLanguage | — / `0x28380` | — / `0x240C8` |
| ARM9 TWL sector read / write | — / `0x5BBBC`, `0x5BD50` | — / `0x4D038`, `0x4D1CC` |
| ARM7 TWL touch read / task | — / `0x8A2C`, `0x918C` | — / `0x898C`, `0x90EC` |
| ARM7 TWL Vblank / Vcounter | — / `0xDBE8`, `0xDC70` | — / `0xDAAC`, `0xDB34` |

All binary edits happen in RAM and require matching instruction signatures
or routine CRCs. Normal launches never rewrite the emulator binaries.

Validated upstream alternate SHA-256 hashes:

| Binary | SHA-256 |
| --- | --- |
| `SNEmulDS_dsp.nds` | `292b70d62bb4092aa94e3723e3352bf3f309cba406bb02c665c9a7faac3a62c4` |
| `SNEmulDS_dsp.srl` | `36b84c717692ec4c5ae1388c545292de6ca43bff4b881df553d4bffb440c99c2` |
| `SNEmulDS_sdd1.nds` | `c4393f40ff41e51df95cd122819c3c9b0781c331bb0e73f08a91e3b235e3242a` |
| `SNEmulDS_sdd1.srl` | `34a4063c98b099c4dabdb99c7b60c5c55f002c493e8e65a44f9fc0cbfd685e50` |

## Verification

Build with `make`, then run `make test` and `python3 tests/verify_build.py`.
If BlocksDS is installed elsewhere, supply `BLOCKSDS=/path/to/core` to make.

With Python Unicorn installed, run:

```sh
python3 tests/alternate_builds_emulation.py /path/to/SNEmulDS_dsp.nds /path/to/SNEmulDS_dsp.srl /path/to/SNEmulDS_sdd1.nds /path/to/SNEmulDS_sdd1.srl
python3 tests/twl_input_emulation.py /path/to/SNEmulDS_dsp.srl
python3 tests/twl_input_emulation.py /path/to/SNEmulDS_sdd1.srl
python3 tests/handoff_emulation.py
python3 tests/ntr_handoff_emulation.py
```

These are host and modeled instruction checks. No new console, real audio,
touch timing or gameplay testing has been performed for v0.16.
