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

[`patches/0001-excite-truck-address-column.patch`](../patches/0001-excite-truck-address-column.patch):

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
