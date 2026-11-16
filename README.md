# SNEmulDS Launcher Shim

Coto's SNEmulDS 0.6d fork is compiled with a custom toolchain, ToolchainGenericDS. Unfortunately, TGDS handles argv differently from BlocksDS and DevkitPro. This shim acts as a translation layer to convert regular argv calls to TGDS's argv standard, by chainloading SNEmulDS with TGDS-formatted argv after the shim itself was called with standard argv arguments.

Starting with v0.16, the shim can also automatically select the stable, DSP-1, or S-DD1 build of 0.6d based on the ROM being launched.

This shim also does in-memory patching on the SRL (DSi-mode) release of SNEmulDS 0.6d. Unfortunately, the official SRL release of 0.6d contains a touchscreen bug that stops it from registering inputs, even when directly launched via Unlaunch's file menu. A patch for this issue is applied when running the SRL from DSpico or console SD.

The 0.6d DSi-mode release was also not intended to be used on anything other than console SD, so it crashes by default when run on a DSpico in DSi mode. A secondary set of patches are applied to the 0.6d SRL when running on a DSpico in addition to the touchscreen patch. The first one forces TGDS to initialize the DSpico's DLDI instead of the console SD path, then patches the SRL's ARM9 sector r/w routines to use that driver. Another patch also enables and unmutes DSi audio during the TWL handoff, restoring sound on DSpico.

The stable 0.6d build saves game settings to the ROM's title section in `snemul.cfg`, but fails to actually read them when loading a game. The shim adds a patch to load these settings before running the game, so that changing configurations and pressing the save button in the emu's GUI makes them persistent, as was intended. The DSP-1 and S-DD1 builds already load game settings correctly, so this patch is not applied to them.

A language patch fixes system language detection in DSi mode and stops 0.6d from resetting the language selected in `snemul.cfg`.

The optional GBA Macro timer patch skips the five-second prompt after a ROM loads in either 0.6d mode. You can enable it in the menu or the ini.

SNEmulDS 0.6a (the original release by Archeide) is also supported as a launch option. While 0.6a does not support any sort of argv, it does support setting a default rompath in the `snemul.cfg` file. When launching 0.6a with a ROM argument, the shim will set the rom path to the directory containing that ROM. This means that you can keep your `.sfc` roms in any folder, and they will still be browseable in 0.6a's file browser.

## LLM Disclosure

This shim was written with the help of Codex - GPT-6.1 Sol/Astra. It is not entirely hand-written and I will not pretend that it is. If you find an issue with the shim or would like to improve the codebase, I'm happy to accept PRs.

## Install

1. Put `snemulds-shim.nds` and `snemulds-shim.ini` on your card. If upgrading, replace the shim at its current location and keep your existing `snemulds-shim.ini`.
2. Put the SNEmulDS builds you want to use next to the shim `.nds` and `.ini`. The default filenames are:
   - Stable: `SNEmulDS.nds` and `SNEmulDS.srl`
   - DSP-1: `SNEmulDS_dsp.nds` and `SNEmulDS_dsp.srl`
   - S-DD1: `SNEmulDS_sdd1.nds` and `SNEmulDS_sdd1.srl`
   - 0.6a Classic: `SNEmulDS_0.6a.nds`
3. Configure `snemulds-shim.nds` as your `.sfc` and `.smc` emulator association in your menu.

SNEmulDS 0.6d downloads:

- [Supported stable TWL/NTR build](https://github.com/cotodevel/SnemulDS/archive/TGDS1.65.zip)
- [DSP-1 build](https://github.com/cotodevel/SnemulDS/archive/dsp.zip)
- [S-DD1 build](https://github.com/cotodevel/SnemulDS/archive/sdd1.zip)

The 0.6d zips contain emulator binaries in `release/arm7dldi-ntr` and `release/arm7dldi-twl`.

SNEmulDS 0.6a can be found here: https://www.gamebrew.org/wiki/SNEmulDS

Remember to rename the `.nds` and `.srl` binaries or edit the `.ini` as needed so that the shim finds the target emulator binary.

### Supported SNEmulDS Hashes

| Binary | SHA-256 |
| --- | --- |
| `release/arm7dldi-ntr/SNEmulDS.nds` | `77f82df3437eff94d35951c673523f2acd964d1a0400555bae58a55b4362a472` |
| `release/arm7dldi-twl/SNEmulDS.srl` | `83e3e4cd3e76a88ca089248eb375580d75cdff567aa490397aaf8dd07fc6812c` |
| `release/arm7dldi-ntr/SNEmulDS_dsp.nds` | `292b70d62bb4092aa94e3723e3352bf3f309cba406bb02c665c9a7faac3a62c4` |
| `release/arm7dldi-twl/SNEmulDS_dsp.srl` | `36b84c717692ec4c5ae1388c545292de6ca43bff4b881df553d4bffb440c99c2` |
| `release/arm7dldi-ntr/SNEmulDS_sdd1.nds` | `c4393f40ff41e51df95cd122819c3c9b0781c331bb0e73f08a91e3b235e3242a` |
| `release/arm7dldi-twl/SNEmulDS_sdd1.srl` | `34a4063c98b099c4dabdb99c7b60c5c55f002c493e8e65a44f9fc0cbfd685e50` |
| `SNEmulDS_0.6a.nds` | `6ec30da30098d8b3632fc3107b1a9cbc594011fa69992fe04ac6a13cb6c4e599` |

## Launch modes

| Menu choice | DS mode | DSi mode |
| --- | --- | --- |
| **0.6d Auto** | Launches the NTR `.nds` | Launches the TWL `.srl` and applies patches as necessary depending on DSi mode environment |
| **0.6d NTR** | Hidden because it duplicates Auto | Switches to DS mode and launches the NTR version `.nds` |
| **0.6a Classic** | Launches the `.nds` defined as "legacy" | Switches to DS mode and launches the `.nds` defined as "legacy" |

The first choice is labeled 0.6d DS mode when the shim is in DS mode. Only the chosen emulator needs to be present. DSi launches require a launcher that leaves SCFG unlocked. (Won't work on iEVO, lol)

**Note on console SD mode:** NTR mode emulators are only supported on flashcarts. When running the shim from a menu running on the console SD, TWL mode is the only supported option.

## Automatic S-DD1 and DSP-1 Selection

The **Auto ROM version** option in the launch menu enables automatic switching between the stable, DSP-1, and S-DD1 builds. You can also enable it with `auto_version = true` in the INI. It applies to all 0.6d launches.

The shim reads the ROM's cartridge header to choose the build. This method should work with all ROMs that contain a valid header.

- DSP-1 games, such as Super Mario Kart, Pilotwings, and Super Air Diver, use the DSP-1 build.
- Star Ocean and Street Fighter Alpha 2 / Street Fighter Zero 2 use the S-DD1 build.
- Other games and ROMs with unknown headers use stable.

Switching happens when a ROM is passed to the shim. If you choose a different game inside SNEmulDS after it has been launched, the currently running build will be used.

If a required alternate build is missing, the shim will throw an error. Check the selected emulator path on the `R` info page, or turn **Auto ROM version** off to use stable.

**Upgrading:** The supplied INI enables automatic switching. An older INI without `auto_version` keeps the previous stable-only behavior. Add the line or turn the option on in the menu to enable it.

## Configuration

The included `snemulds-shim.ini` contains these settings:

```ini
[snemulds]
ntr_path = SNEmulDS.nds
twl_path = SNEmulDS.srl
legacy_path = SNEmulDS_0.6a.nds
dsp_ntr_path = SNEmulDS_dsp.nds
dsp_twl_path = SNEmulDS_dsp.srl
sdd1_ntr_path = SNEmulDS_sdd1.nds
sdd1_twl_path = SNEmulDS_sdd1.srl
autoboot = false
default = auto
skip_macro_timer = true
auto_version = true
```

- `default` accepts `auto`, `ntr`, or `legacy`.

- `autoboot`, `skip_macro_timer`, and `auto_version` accepts `true`/`false`, `on`/`off`, or `1`/`0`.

- `auto_version` enables automatic 0.6d version switching. If it's missing, switching is off.

## Build

1. Install [BlocksDS](https://blocksds.skylyrac.net/docs/setup/) and [Python 3.](https://www.python.org/downloads/)

1. `git clone` this repository

1. `cd` into the cloned files.

1. Run `make` to build. This should compile `snemulds-shim.nds`.

The Makefile uses `/opt/blocksds/core` for BlocksDS and `/opt/wonderful` for the toolchain. If yours are installed elsewhere, set `BLOCKSDS` and `WONDERFUL_TOOLCHAIN` to their locations.

`make` builds the embedded loader and generates the patch headers too. Run `make clean` followed by `make` for a clean rebuild.

## Tests

Test code is kept on the separate `tests` branch. The `master` branch contains only the source for the shim itself.
Switch to `tests` before running the checks:

```sh
git switch tests
make
make test
python3 tests/verify_build.py
```

The host checks require Python 3 and a host C compiler. The build verification also needs the built shim and loader. Additional ARM instruction checks require Python's `unicorn` package, and some need the supported emulator binaries or a DSpico DLDI driver. See `AGENTS.md` on the `tests` branch for the commands, and `docs/automatic-versions.md` there for the alternate-build checks.

## License

Project-authored code is licensed under GNU GPL version 3 or any later version (`GPL-3.0-or-later`); see [LICENSE](LICENSE). Vendored sources keep their original terms and notices, listed in [third_party/NOTICE.md](third_party/NOTICE.md).
