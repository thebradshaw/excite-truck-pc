# Excite Truck PC

A native PC remaster of **Excite Truck** (Wii, 2006), built with static recompilation.
The game's PowerPC code is translated to C++ ahead of time and compiled into a normal PC program,
with no emulator in the loop.

Goals: 120 fps, HD and widescreen, longer draw distance, RetroAchievements, Xbox controller
layout (R/R2 drive, X brake, A turbo, left stick for steering and air control), and later
online two-player.

> No game code, assets or disc images are in this repository. You need your own dump of the
> US disc (`REXE01`, revision 0). The translation runs on your machine against that dump.

## Status

Step 1 of 4 (taking the game apart) is done. See [docs/step1-findings.md](docs/step1-findings.md).

## Layout

| Path | What it is |
| --- | --- |
| `third_party/wiicompiled` | The [WiiCompiled](https://github.com/patchzyy/Wiicompiled) recompiler and runtime (GPLv3), pinned |
| `projects/excite-truck/recomp.yml` | Translator manifest for the US disc |
| `projects/excite-truck/MAP.txt` | Function start addresses that seed the translator |
| `tools/` | Small Python helpers for reading the game's `main.dol` |
| `scripts/translate.sh` | Runs the translation |

## Translating the game

1. Extract your disc: `nodtool extract "Excite Truck (USA).wbfs" extracted` (`cargo install nodtool`).
2. Copy `extracted/sys/main.dol` to `Assets/main.dol`.
3. Install the .NET SDK (8 or newer, with a compiler at least as new as SDK 8.0.2xx).
4. Run `./scripts/translate.sh`. The output lands in `build/excite-truck/`.

## License

GPLv3, matching WiiCompiled.
