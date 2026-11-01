# Excitebots: Trick Racing on PC

Excitebots (USA, `RX3E01`) is built the same way as Excite Truck: WiiCompiled's translator turns
`main.dol` into C++, and the runtime replaces the Wii SDK with native PC code. It lives in this repo
as a second game and is built on top of the Excite Truck patches (0001 to 0003).

## The disc

| | |
| --- | --- |
| Game ID | `RX3E01` |
| `main.dol` SHA-256 | `14ed09f70c8f9fcd5c027bb9e423ba2deade3c15bbd7d4376b2f63fe99c80c1e` |
| Code | 4.2 MB, `.text` at `0x800045E0`, `.ctors` at `0x80417720` (0x740 bytes), no RELs |
| Entry, r13, r2 | `0x80004170`, `0x805A7FE0`, `0x805ACAE0`; stack top / arena start `0x805BBD28` |
| SDK | `0x4199_60831`, the same build as Mario Kart Wii's (plus THP, NWC24/DWC, HBM; no PAD) |
| IOS | 55 (from the TMD; the SDK refuses to start under the IOS36 values Mario Kart uses) |

Same Monster Games engine as Excite Truck. Videos are THP (decoded by the game itself), there is
no SD-card music player, and the online parts use Nintendo WFC (DWC).

## Address table

Because the SDK is Mario Kart's own build, almost every SDK function has the same size, and the
size alignment that Excite Truck needed hand-holding for works nearly on its own:

1. Translate with a seed map (`tools/excitebots/mkmap.py`): 15,201 functions.
2. Name SDK functions with Dolphin's signatures (`tools/excitebots/sigmatch.py`): 1,612.
3. Align against Mario Kart's `MAP.txt` (`tools/excitebots/align.py`), plus hand-checked entries in
   `tools/excitebots/eb_manual_map.txt`. `tools/excitebots/dolfind.py` finds Excite Truck's
   hand-checked functions in Excitebots by their instructions with relocations masked.
4. Data globals: `tools/excitebots/datamap.py` plus `eb_manual_data.txt`. The automatic data map
   guesses `lis`-based struct addresses wrongly at times (it picks the high half nearest the Mario
   Kart value), so every struct global the runtime touches was checked by disassembly.
5. `tools/excitebots/gen_eb_versions.py` writes `tools/sdk_map/eb_versions.inc`.

Result: 306 of the runtime's 552 hooks map (Excite Truck: about 305). Manual entries whose size
differs from Mario Kart's map only their entry point (`align.py` checks the sizes).

## Runtime changes (`patches/excitebots/0004-excitebots-runtime.patch`)

- `GuestRegion::Id::Excitebots = 4`, game ID `RX3E01`, `IsExciteTruck()` / `IsExcitebots()`.
  Excitebots-only hooks use the `0x8E000000` alias (Excite Truck's are at `0x8F000000`).
- Display lists: the game records small lists that hold only a vertex descriptor change and
  calls them before its model lists, so the dirty vertex descriptor is written into a list at
  `GXEndDisplayList` (as for Excite Truck) and at `GXBegin` while recording (2008 SDK
  behaviour). Without it the model lists were walked with the wrong vertex size and the race
  drew nothing. `WC_DEBUG_DL=1` logs nested list calls with impossible addresses.
- Excite Truck's frame pipelining stays Excite Truck only; GX register pointers and the
  draw-sync token callback have Excitebots addresses.
- Boot: IOS55 version values in low memory (`0x80003140`, `0x80003188`), `.ctors` range, entry.
- `runtime/src/hle/excitebots/eb_hooks.cpp`: `GXSetDrawDone`, the `WPADGetStatus` boot spin,
  `OSGetFontEncode` (no VI DTV register), `__OSReadROM` fonts, `NANDCreateAsync` and
  `NANDCreateDirAsync` (save file), `AIStopDMA` (Mario Kart lacks it;
  the aligner had put `AIStartDMA` one function off), the debug printf (`WC_GAME_LOG=1`) and the
  developer command line (`WC_EB_ARGS`).
- `runtime/src/hle/excitebots/eb_input.cpp`: a virtual sideways Wii Remote at the KPAD level, as
  for Excite Truck. The game reads one `KPADStatus` per channel per frame (`0x803F1C54`) and learns
  of a remote through its KPAD connect callback (`0x803F10E0`).

## Controls and swings

- Race state: `0x805A2CE4` (r13-0x52FC) is the race time step (zero in the menus, kept while
  paused) and `0x805A2AC4` (r13-0x551C) the race clock (stops while paused). A race counts as
  running while the step is set and the clock moved in the last 1.5 s, which picks the driving or
  the menu layout, as for Excite Truck. Found with `WC_EB_SCAN=<dir>`, which dumps the small-data
  area every 30 controller reads.
- The game's input layer (`rjoy`, 0x803F1450) turns each KPADStatus into a joystick record:
  steering at +4, pitch at +8 and a gravity vector at +0x58..+0x60. A motion detector
  (0x801E3D50) keeps a slow average of that vector and reports a flick when the acceleration points
  against it, plus a shake strength from how fast it changes. Crash recovery ("SHAKE!!!") reads
  these, and so do the bars (`controller_swing` in a bar's `hookbar_params`).
- A (pad), C or Space (keyboard) plays one swing per press: three reads with the acceleration
  thrown against gravity at twice its strength. On the crash prompt the meter fills and the bot
  boosts out. `WC_EB_AUTOSWING=N` swings every N reads for testing.
- The crash prompt's shaking remote (overlays.res `shakecont.art`) is replaced by the A button
  (`tools/prompts/make_eb_pack.py`, installed to `prompts/excitebots`; `[excitebots] xbox_prompts`
  or the F10 Controllers checkbox turns it off).

## Developer options

The command-line table at `0x804A0EE8` matches Excite Truck's: `-direct_to_track`,
`-force_chassis`, `-widescreen`, `-standardscreen`, `-players`, `-no_music`, `-kiosk` and more.
Set them with `WC_EB_ARGS`.

## Building

- Windows: `scripts\excitebots\build-windows.ps1 <disc>` (work folder `C:\EBBuild`, kept apart
  from Excite Truck's `C:\ETBuild` because the WiiCompiled tree is patched differently). Without a
  checkout it runs in setup mode: a `payload` folder beside it holding `overlay\` (the WiiCompiled
  files changed by patches 0001 to 0004, `guest_versions.inc` with both games' tables appended, and
  `projects\excitebots\`), `stamp.txt` and `prompts\`. The Excite PC launcher runs it from
  `setup\bots\` with `-Disc` and `-Out`.
- Linux: `scripts/excitebots/build-linux.sh <disc>` (`OUT=`, `WORK=`, `JOBS=`).
- Both print `==> step` lines, `##progress P` / `##progress A B` for the launcher's bar, and
  `Error: ...` on failure. The game is `Excitebots(.exe)` with `portable.txt` and
  `UserData\Config.toml` (`[paths] dvd_root`).

## Status

- Boots to "Press 2 to Start" at about 59 fps (container, software renderer), 16:9, creates its
  save file and reaches the profile screen.
- Races load and draw (`WC_EB_ARGS=-direct_to_track:China`), with the HUD in 16:9.
- Xbox layout in races and menus; A swings (checked on crash recovery).
- Next: check a bar swing in play, the rest of the Wii prompts, shader list, achievements.
