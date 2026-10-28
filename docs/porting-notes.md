# Porting notes: pointing WiiCompiled's runtime at Excite Truck

WiiCompiled's runtime replaces Wii SDK functions (graphics, video, OS, storage, audio) with
native PC code. It names every hook by its address in **Mario Kart Wii PAL** (`RMCP01`).
Excite Truck has the same SDK at different addresses, so the port adds an Excite Truck
column to the runtime's address table.

## How the address table is built

1. `tools/sigmatch.py` hashes every function in Excite Truck's `main.dol` the way Dolphin
   does and looks them up in Dolphin's `Data/Sys/totaldb.dsy`. That gives about 1,400
   named SDK functions, which serve as anchors.
2. `tools/align.py` lines up Excite Truck's functions against Mario Kart Wii's `MAP.txt`
   (in the WiiCompiled repo) between and past those anchors, by comparing function sizes.
   The SDK libraries keep their order, so this works well. Pairs where the sizes don't match
   are kept only when checked by hand.
3. `tools/manual_map.txt` holds hand-checked additions (CPU register helpers, DSP mailbox)
   and rejected guesses.
4. `tools/gen_et_versions.py` writes the `MKW_REGION_RANGE(ExciteTruck, ...)` rows:
   [`tools/sdk_map/et_versions.inc`](../tools/sdk_map/et_versions.inc).

Scratch inputs live in `$ET_WORK` (default `work/`): `sdk_names.txt` (from step 1),
`mk_overrides.txt` (the runtime's hook list) and a function-files translation of the DOL.

Result: 305 of the runtime's 582 hooks have an Excite Truck address, plus 1,870 SDK
functions in total. Most of the remaining hooks are Mario Kart code (EGG, races, online) or
SDK functions Excite Truck never links (the linker stripped them).

## Patches to WiiCompiled

[`patches/0001-excite-truck-runtime.patch`](../patches/0001-excite-truck-runtime.patch):

- Translator: a `GuestRegion.ExciteTruck` column, selected by game ID `REXE01`. Hooks with no
  Excite Truck address map to 0 and are dropped from the native index.
- Runtime: the same column in `guest_region.h` (with the game facts: ID, NTSC, arena start
  `0x80575240`), `Translate()` returns 0 instead of aborting for Excite Truck, and the
  function registry skips address 0.

`et_versions.inc` is appended to `runtime/include/guest_abi/guest_versions.inc`.

## Building in a locked-down container

GitHub `archive/*.tar.gz` downloads, `sqlite.org` and `files.twilitrealm.dev` may be blocked.
Clone the dependencies with git and pass `FETCHCONTENT_SOURCE_DIR_<NAME>` for abseil-cpp,
xxhash, fmt, zlib (zlib-ng), png, tracy, sdl, imgui, rmlui and freetype. For sqlite3, point at
any SQLite amalgamation folder (`sqlite3.c`/`sqlite3.h`).

## Boot fixes (step 2)

The game now boots to its first menu ("create a save file?") at 60 fps. What it took, in boot order:

- Entry point, `.ctors` range, IPC globals and a few hand-checked SDK mappings (`tools/manual_map.txt`, `tools/manual_data.txt`).
- `runtime/src/hle/excite_truck/et_hooks.cpp` (game-only hooks, addressed through the `0x8F000000` alias):
  - `WPADGetStatus`: services alarms while boot spins waiting for the Wii Remote library.
  - `FAAttach`: the SD-card drive attaches with no card (the Music-folder feature replaces this later).
  - `OSGetFontEncode` / `OSSetFontEncode`: default to ANSI instead of reading a VI register.
  - `__OSReadROM`: serves the IPL ROM fonts from `assets/fonts` (Dolphin's Droid Sans replacements, Apache 2.0).
  - `GXSetDrawDone`: finishes queued draws and calls the game's draw-done callback.
- VI globals the game's older SDK lays out differently (retrace queue, render heights); globals with no counterpart are skipped.
- GX: GP metrics and EFB pokes are no-ops; FIFO breakpoints are emulated by holding draw-sync tokens until `GXDisableBreakPt`, which is how the game keeps the GPU one frame behind and detects hangs.
- The Mario Kart startup pipeline list is no longer bundled (`-DMKW_INITIAL_PIPELINE_CACHE=NONE`); Excite Truck's own recorded list replaces it later. The startup screen ends on the first frame.

`scripts/cycle.sh` regenerates the address table, retranslates, builds and runs; `scripts/shot.sh` boots on a virtual display and takes screenshots; `scripts/hangdump.sh` dumps thread stacks of a hung run.

## Save prompt to menus (step 2)

- **Unmapped globals wrote to address 0.** `MKW_GADDR()` returns 0 for a Mario Kart address with no
  Excite Truck counterpart, and guest address 0 aliases MEM1 0x80000000. GXInit's HLE cleared
  0x600 bytes there, and the AX DSP-task setup wrote its 0x38-byte task struct there. That wiped the
  OS globals (bus clock at 0x800000F8, disc ID), so the game's millisecond timer divided by zero,
  its frame delta stayed 0 and menu input was discarded forever. `tools/manual_data.txt` now maps
  the GX data block, the display-list FIFO, the AX task/ucode globals and the power-callback flag.
  `scripts/lowtrap.sh` makes the low page read-only under gdb and reports the first writer.
- **GX register pointers.** A few GX helpers (GXGetGPStatus and friends) still run as guest code and
  read the CP/PE/PI/MEM registers through `__cpReg` etc., which the replaced GXInit never set. They
  now point at a quiet register block in the reserved MEM2 window (CP status = read and command
  idle), which keeps the game's "HANG AT HIGH WATERMARK" watchdog happy.
- **KPAD layout.** This 2006 KPAD stores `dev_type` as a u32 at 0x5C, with extension data from 0x60.
  Writing the later SDK's byte fields there made the game ask to unplug an extension.
- **Game debug log.** `WC_GAME_LOG=1` routes the game's stripped debug printf (0x802E30C8) to
  OSReport, which shows scene changes, memcard steps and resource misses.
- Translator note: calls into an ET alias hook only store the argument registers the translator
  thinks the callee reads, so a hook that needs r4..r7 may see stale values. Prefer fixing the
  state the guest code reads over hooking small leaf functions.
