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
- Excite Truck workarounds for its 2006 SDK (display-list dirty-state flush, frame pipelining)
  now apply to Excite Truck only; GX register pointers and the draw-sync token callback have
  Excitebots addresses.
- Boot: IOS55 version values in low memory (`0x80003140`, `0x80003188`), `.ctors` range, entry.
- `runtime/src/hle/excitebots/eb_hooks.cpp`: `GXSetDrawDone`, the `WPADGetStatus` boot spin,
  `OSGetFontEncode` (no VI DTV register), `__OSReadROM` fonts, `AIStopDMA` (Mario Kart lacks it;
  the aligner had put `AIStartDMA` one function off), the debug printf (`WC_GAME_LOG=1`) and the
  developer command line (`WC_EB_ARGS`).
- `runtime/src/hle/excitebots/eb_input.cpp`: a virtual sideways Wii Remote at the KPAD level, as
  for Excite Truck. The game reads one `KPADStatus` per channel per frame (`0x803F1C54`) and learns
  of a remote through its KPAD connect callback (`0x803F10E0`).

## Developer options

The command-line table at `0x804A0EE8` matches Excite Truck's: `-direct_to_track`,
`-force_chassis`, `-widescreen`, `-standardscreen`, `-players`, `-no_music`, `-kiosk` and more.
Set them with `WC_EB_ARGS`.

## Status

- Boots to "Press 2 to Start" and the save prompt at about 59 fps (container, software renderer),
  16:9.
- Next: save file, menus and the first race; race-state detection for the controls; bar swings on
  A (`barswingComponent`, Red/Yellow bar minigames) with an on-screen prompt; shader list;
  achievements; build scripts (`scripts/excitebots/`).
