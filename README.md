# SNEmulDS Launcher Shim

Coto's SNEmulDS 0.6d fork is compiled with a custom toolchain, ToolchainGenericDS. Unfortunately, TGDS handles argv differently from BlocksDS and DevkitPro. This shim acts as a translation layer to to convert regular argv calls to TGDS's argv standard, by chainloading SNEmulDS with TGDS-formatted argv after the shim itself was called with standard argv arguments.

This shim also does in-memory patching on the SRL (DSi-mode) release of SNEmulDS 0.6d. Unfortunately, the official SRL release of 0.6d contains a touchscreen bug that stops it from registering inputs, even when directly launched via Unlaunch's file menu. A patch for this issue is applied when running the SRL from DSpico or console SD.

The 0.6d DSi-mode release was also not intended to be used on anything other than console SD, so it crashes by default when run on a DSpico in DSi mode. A secondary set of patches are applied to the 0.6d SRL when running on a DSpico in addition to the touchscreen patch. The first one forces TGDS to initialize the DSpico's DLDI instead of the console SD path, then patches the SRL's ARM9 sector r/w routines to use that driver. Another patch also enables and unmutes DSi audio during the TWL handoff, restoring sound on DSpico.

Both stock 0.6d builds save game settings under the ROM's title in `snemul.cfg`, but fail to read that section when loading a game. The shim patches the loaded emulator in RAM to restore those saved settings before the game starts, including VBlank disabled, fast, or full. This applies whether a ROM is passed as an argument or selected in the emulator browser. Saving remains in the emulator GUI; the shim does not modify the emulator binary or `snemul.cfg` for 0.6d.

SNEmulDS 0.6a (the original release by Archeide) is also supported as a launch option. While 0.6a does not support any sort of argv, it does support setting a default rompath in the `snemul.cfg` file. When launching 0.6a via the shim, the shim will set the rom path to the directory containing your launched rom. This means that you can keep your `.sfc` roms in any folder, and they will still be browseable in 0.6a's file browser.

## LLM Disclosure

This shim was written with the help of Codex - GPT-6 Sol/Astra. It is not entirely hand-written and I will not pretend that it is. If you find an issue with the shim or would like to improve the codebase, I'm happy to accept PRs.

## Install

1. Put `snemulds-shim.nds` and `snemulds-shim.ini` on your card. If upgrading, replace the shim at its current location and keep your existing `snemulds-shim.ini`.
2. Put the SNEmulDS builds next to the shim `.nds` and `.ini` if you want to use the default configuration provided.
   - The default INI expects `SNEmulDS.nds`, `SNEmulDS.srl`, and `SNEmulDS_0.6a.nds` beside it.
3. Configure `snemulds-shim.ini` as your `.sfc` and `.smc` emulator association in your menu.

You can grab the latest stable release of SNEmulDS 0.6d TWL/NTR here: https://github.com/cotodevel/SnemulDS/archive/TGDS1.65.zip

SNEmulDS 0.6a can be found here: https://www.gamebrew.org/wiki/SNEmulDS

Remember to rename the `.nds` files or edit the `.ini` as needed so that the shim finds the target emulator binary.

### Supported SNEmulDS Hashes

| Binary | SHA-256 |
| --- | --- |
| `release/arm7dldi-ntr/SNEmulDS.nds` | `09bc43aad372da4984155d77c24a30a8c0a8a668288aa834eeb0695f1359094b` |
| `release/arm7dldi-twl/SNEmulDS.srl` | `f2817e29db1958af6af2957d0ac7ccfce01b5a57f78e522527d1d508c121b7a2` |
| `SNEmulDS_0.6a.nds` | `6ec30da30098d8b3632fc3107b1a9cbc594011fa69992fe04ac6a13cb6c4e599` |

## Launch modes

| Menu choice | DS mode | DSi mode |
| --- | --- | --- |
| **0.6d Auto** | Launches the NTR `.nds` | Launches the TWL `.srl` and applies patches as necessary depending on DSi mode environment |
| **0.6d NTR** | Hidden because it duplicates Auto | Switches to DS mode and launches the NTR version `.nds` |
| **0.6a Classic** | Launches the `.nds` defined as "legacy" | Switches to DS mode and launches the `.nds` defined as "legacy" |

The first choice is labeled 0.6d DS mode when the shim is in DS mode. Only the chosen emulator needs to be present. DSi launches require a launcher that leaves SCFG unlocked. (Won't work on iEVO, lol)

**Note on console SD mode:** NTR mode emulators are only supported on flashcarts. When running the shim from a menu running on the console SD, TWL mode is the only supported option.

## Configuration

The included `snemulds-shim.ini` contains the defaults:

```ini
[snemulds]
ntr_path = SNEmulDS.nds
twl_path = SNEmulDS.srl
legacy_path = SNEmulDS_0.6a.nds
autoboot = false
default = auto
```

- `default` accepts `auto`, `ntr`, or `legacy`.

- `autoboot` accepts `true`/`false`, `on`/`off`, or `1`/`0`.

## Build

1. Install [BlocksDS](https://blocksds.skylyrac.net/docs/setup/) and [Python 3.](https://www.python.org/downloads/)

1. `git clone` this repository

1. `cd` into the cloned files.

1. Run `make` to build. This should compile `snemulds-shim.nds`.

## Tests

The repo also contains tests for the shim. You can build and run tests with `make test`, and then running `python3 tests/verify_build.py`

These checks require Python 3 and a host C compiler.

The modeled instruction checks below also require Python's `unicorn` package:

```sh
python3 tests/handoff_emulation.py
python3 tests/ntr_handoff_emulation.py
```

The TWL instruction checks need a copy of 0.6d SRL and the DSpico DLDI:

```sh
python3 tests/twl_input_emulation.py /path/to/stock/SNEmulDS.srl
python3 tests/twl_io_emulation.py /path/to/stock/SNEmulDS.srl /path/to/DSpico.dldi
```

The game settings instruction check needs the stock 0.6d NTR and TWL binaries and Python's `unicorn` package:

```sh
python3 tests/game_settings_emulation.py /path/to/stock/SNEmulDS.nds /path/to/stock/SNEmulDS.srl
```

These checks model instruction behavior. Game settings persistence on a console still needs hardware testing.

## License

Project-authored code is licensed under GNU GPL version 3 or any later version (`GPL-3.0-or-later`); see [LICENSE](LICENSE). Vendored sources keep their original terms and notices, listed in [third_party/NOTICE.md](third_party/NOTICE.md).
