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

[`patches/0002-aurora-channel-colors.patch`](../patches/0002-aurora-channel-colors.patch) applies
inside the `aurora-main` submodule (`git -C aurora-main apply ...`). See "Truck paint" below.

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

## First race on screen (step 2)

- ET's `GXSetVtxDescv` (0x8003FC98) and `GXSetVtxAttrFmtv` (0x80040254) are now mapped. ET has no
  `GXGetVtxDesc`/`GXGetVtxAttrFmt`, `GXLoadPosMtxIndx` or the LOD-bias texture helpers, so those
  MKW hooks stay unmapped on purpose.
- The engine switches vertex layouts with tiny recorded display lists: `GXBeginDisplayList`,
  `GXClearVtxDesc` + `GXSetVtxDescv`, `GXEndDisplayList` (0x802D62FC). In this SDK,
  `GXEndDisplayList` calls `GXGetCPUFifo`, which calls `GXFlush`, so the dirty VCD is written
  into the list as CP 0x50/0x60, followed by 32 zero bytes. The game stores the list size as
  `size >> 5`, so without those bytes every list was size 0 and the model lists were read with
  the HUD's direct layout, which produced garbage FIFO commands. The HLE now writes the VCD and the
  padding when a descriptor changes during recording, for non-MKW games. It also saves and
  restores the HLE vertex state around recording when GX's save-context flag is set.

## Custom music from a folder (step 2)

The game plays MP3s from an SD card in races (Options > Race Music > All Random). The port gives it
a read-only virtual SD card built from a folder on the PC:

- Folder: `Music` in the app data folder (`~/.local/share/WiiCompiled/Music` on Linux), or
  `WC_MUSIC_DIR`. Subfolders count. The game takes at most 100 songs.
- `runtime/src/hle/excite_truck/et_sdcard.cpp` answers `/dev/sdio/slot0` (the commands follow
  Dolphin's SDIO slot) and builds a FAT16 volume in memory: MBR, boot sector, FAT, a root with a
  `MUSIC` folder, and one entry per song with a long file name, so the track list shows the real
  names. Song bytes are read from the host files on demand. Writes fail as on a locked card.
- The card is reported uninitialised until the driver's own reset, and its size is rounded to one
  the CSD can state exactly: the FAT library rejects a partition that ends past the card size.
- The game's real FAT stack (`FAAttach`) now runs; the old "no card" hook is gone.
- **Background threads.** The game's MP3 thread scans the card from its own loop, sleeping 50 ms
  between passes. ET's main loop never blocks in the guest scheduler (frames are paced on the
  host inside `GXCopyDisp`), so that thread never woke up. `VIFlush`, which ET calls once a frame
  after the copy, now wakes due sleepers and, when a thread is ready, parks the main thread for a
  moment so it runs (`OS_HLE_YieldToReadyThreads`). Other ET background threads benefit too.
- `WC_TRACE_SD=1` logs every SDIO request.

## Developer options and the first Excite race (step 2)

The game parses a command line at boot (0x80004D64 → 0x800199B8), but retail builds pass it an
empty string. `WC_ET_ARGS` now supplies one, which unlocks the developer options in the table at
0x802FD528. The useful one for testing is `-direct_to_track:<event>`, which skips the menus and
starts a race, for example `WC_ET_ARGS=-direct_to_track:canada` (an Excite race in Canada with five
opponents). Others: `-no_music`, `-no_sound`, `-profile`, `-players:<n>`, `-locale:<x>`.

A full race runs this way: countdown, HUD, AI opponents, stars, Tree Runs, jumps and boosts.
Open: the container's software renderer only manages 7 to 10 fps in races, so scripted laps are
slow. (`-force_chassis:<Name>` picks the truck, for example `Trekker`.)

## Truck paint (step 2)

Every truck drew black, with a grey chrome pass on top. Cause: aurora's `GXSetChanMatColor` and
`GXSetChanAmbColor` overwrote the whole register for `GX_ALPHA0`. The game sets the colour with
`GX_COLOR0` (white) and then the alpha with `GX_ALPHA0` (`000000FF`), so the lit trucks ended up
with a black material. On real hardware `GX_COLORn` only replaces RGB and `GX_ALPHAn` only alpha.
Fixed in [`patches/0002-aurora-channel-colors.patch`](../patches/0002-aurora-channel-colors.patch),
which applies inside WiiCompiled's `aurora-main` submodule.

How the body is drawn, for reference: stage 0 multiplies the paint texture (CMPR) by the lit
colour, stage 1 blends toward a 256x256 environment map that the game renders and copies each
other frame (`0x12934280`, RGBA8); the reflection strength is the env map's alpha, written with
`GXSetDstAlpha(1, 0x90)` into an RGBA6 framebuffer.

**GX frame trace.** `WC_TRACE_GX_FRAME=<n>` logs every GX state call, texture and TLUT load,
display-list call and EFB copy for frame n (lines start with `[gxt]`). Good for finding which
draw is wrong without a GPU debugger.

## No shader stutter (step 2)

WiiCompiled's renderer (aurora) keeps every shader recipe it has built in
`Cache/pipeline_cache.db` and compiles all of them on the startup screen ("Compiling shaders,
please hold on") before the game runs. It also imports `initial_pipeline_cache.db` from next to
the executable on every launch. This repo ships that file for Excite Truck:
[`assets/shaders/initial_pipeline_cache.db`](../assets/shaders/initial_pipeline_cache.db),
795 recipes recorded by driving the menus, the tutorial and a race on each of the seven tracks
(the last track added only 10 new ones, so the set is close to complete). Pass it to the build with
`-DMKW_INITIAL_PIPELINE_CACHE=...` (the example configure script does).

So a first launch compiles everything up front and races never wait on a shader. If the game ever
meets a recipe that is not in the set, the main view skips that one draw for a frame or two instead
of freezing (`skip_unready_pipelines`, on by default), and the recipe is saved so it is built at
startup from then on.

To refresh the set after playing more of the game: `scripts/export-shaders.sh` merges your
`pipeline_cache.db` into the shipped file. The file holds only GPU state descriptions (TEV stages,
blend modes, vertex layouts), no game graphics.

## Controls, tutorial and copies (step 2, Chris's first Windows run)

- **Menu or race.** The float at r13-0x1DEC (0x8055EB14) is the race simulation's time step: non-zero
  while a race runs, zero in menus and while paused. `et_input.cpp` uses it to switch between the
  driving layout (stick = tilt, A = turbo + 2) and the menu layout (A = 2, stick = D-pad, remote
  level, since the menus ignore the D-pad while the remote is tilted). Found by diffing MEM1 dumps
  taken in menus, mid-race, paused and after a crash.
- **Mandatory tutorial.** `contest tutorial 0 mandatory yes` in common.res/chal_eves.val. 0x80161D50
  sums the challenges of mandatory tutorial contests; the front end (0x80247C18) forces the tutorial
  while a profile has passed fewer. The override returns 0. The res entries are CRC-checked, so the
  data can't simply be patched.
- **Texture copies.** `GXCopyTex` used to sync the GX worker (3 to 4 times a frame in a race). It now
  records the copy for the worker with a snapshot of the copy state; `WC_SYNC_COPYTEX=1` restores
  the old path. `WC_ET_PERF=1` prints frame time and the time spent waiting on the worker.
- **Icon.** build-windows.ps1 decodes the 48x48 RGB5A3 save icon (WIBN banner in files/mcbanner.res,
  icon at +0x60A0) into `runtime/resources/WiiCompiled.ico` before compiling, and the shortcut uses it.

## Frame rate, icon and 1080p (step 2, Chris's second Windows run)

Measured on the container with Dawn's null backend (`WC_NULL_GPU=1`, CPU cost only) and
`WC_ET_PERF=1` at the start of Canada: 43 ms per frame before, 25-31 ms after.

- **Draw batching.** Aurora's FIFO processor used to be woken after every draw
  (`kDrawBatchSize = 1` in `lib/gx/fifo.cpp`); it is now woken every 64 draws (patch 0002).
- **Pipelined frames.** `BeginAuroraFrame`/`EndAuroraFrame` no longer wait for the GX worker
  (`PipelineGxFrames()` in `aurora_events.h`), so the game thread runs the next frame's logic while
  the worker is still submitting the last one. This is safe for Excite Truck because the game waits
  for its own draw-done (0x802CFDE0) before it touches render data again, as on hardware.
  `WC_GX_FRAME_SYNC=1` restores the old full sync.
- **Texture copies.** `RunCopyTex` flushes instead of draining: aurora encodes the copy into the same
  FIFO, so order alone keeps it after the draws it reads. `WC_COPYTEX_DRAIN=1` restores the drain.
- **Tried and dropped:** collecting GXBegin vertices into one packet (direct or indexed). Direct
  packets gave no measurable gain; indexed packets overflowed aurora's 8 MB vertex buffer.
- What is left is mostly the game's own translated code (flat profile, no single hot function) and
  the worker replaying display lists.
- **Icon.** The runtime sets the window and taskbar icon from the disc's save icon
  (`files/mcbanner.res`). The build script also forces the exe's icon resource to rebuild, since the
  resource compiler does not notice a changed `.ico`.
- **1080p.** On first launch of this version the config is switched once to borderless full screen
  with "Auto (window size)" render resolution (`[excite_truck] defaults_version = 1`). F10 can
  change either afterwards.
- Discord presence names Excite Truck instead of Mario Kart Wii.

## Crash taps, audio drift, perf log (step 2, Chris's third run)

- **Crash recovery taps.** The recovery counts presses of 2. With R (gas = 2) held, A added no
  new press, so each A tap now releases 2 for one KPAD read and presses it on the next.
- **Audio crackle.** The game makes audio on the CPU clock and the device plays on its own. The
  output queue sat at its limit and dropped blocks all the time (each drop is a click). The backend
  now nudges SDL's stream frequency ratio (up to 2%) to hold about 70 ms queued, the cap went from
  120 to 250 ms, and an emptied queue gets a 40 ms silence cushion. Container test: 0 drops.
  `WC_TRACE_AUDIOQ=1` prints queue stats once a second.
- **Motion blur** is not extra work: the same four EFB copies run each frame at any speed.
- **ExciteTruck-perf-log.bat** in the install folder runs the game with `WC_ET_PERF=1` and writes
  `perf-log.txt`, so frame time on a player's PC can be read back.

## Menus after a race, tap counting, Chris's perf log (step 2, fourth run)

- **Race detection.** The time step at r13-0x1DEC is not cleared when a race ends, so the menus
  after a race got the driving layout (stick steering, no cursor). `RaceRunning()` now also needs
  the race clock (r13-0x1DE8, 0x8055EB18, seconds) to have moved in the last 0.5 s.
- **A taps** are counted from SDL events (an event watch), and each becomes one game frame with 2
  released and one with 2 pressed (frames counted by `g_gxFrameCount`), so fast taps are not lost.
- **Chris's FX-8350 + RX 570 log (D3D12).** Menus: 16.7 ms (paced). Races: 23-36 ms. Of that, the
  game thread waits 8-13 ms at its draw-done; the GX worker spends 7-12 ms of each frame waiting
  in the CopyDisp drain for aurora's FIFO processor. The game thread's own work is ~20 ms
  (6 ms logic, 14 ms render recording). Not draining at CopyDisp would overlap more, but aurora
  reads vertex arrays from guest memory when it processes a draw (`push_storage` on
  `array.data`), so the game could overwrite them first; that needs array snapshots first.

## Vulkan default, menu video, taps everywhere (step 2, fifth run)

- Chris's Vulkan log: race frames 19-31 ms vs 23-36 ms on D3D12, so "auto" now means Vulkan for
  Excite Truck on Windows (aurora falls back if Vulkan is missing).
- Frame pipelining is now on only while a race runs. In the menus the game decodes the background
  video into a texture during its logic phase, which overlapped the worker's upload of the previous
  frame and showed lines in the video.
- A-tap release/press pulses now run in every state; the crash sequence can freeze the race clock,
  which made `RaceRunning()` false and skipped them. The clock freeze window is now 1.5 s.
- After the audio output runs dry, the next block fades in over 256 samples.
- The game peeks the EFB through 0xC8xxxxxx at frame end (func_802CECF4, likely sun/lens-flare
  visibility); those reads get zero-filled memory.

## L brake, XInput tap counter, hitch log (step 2, sixth run)

- Chris's memory log: working set ~750 MB of 12 GB, so not paging. Soft page faults (up to 24k
  per 120 frames) rise with the slow frames, which also have the longest worker drain: the
  graphics side is catching up then.
- L / LT (and LB) brake.
- On Windows a thread polls XInput every 4 ms and counts A presses into the tap queue, because SDL
  only samples the pad once per game frame and quick taps fell between samples. SDL A events are
  ignored while XInput sees a pad, so a press is never counted twice.
- `[perf] hitch:` lines report any single frame over 100 ms (Chris hears the sound cut out for half
  a second; audio is produced on the game thread, so a long stall empties the output).
- Menu video lines appeared together with the switch to Vulkan; not visible in the container.
- Chris's D3D12 run (0.1.8): menu video clean, race frames 17-32 ms, no hitches during the race
  (only at loads). Vulkan default reverted: the video lines are Vulkan-only and D3D12 felt better.
- Audio speed matching now hands SDL a new ratio only in 0.1% steps (each change restarts SDL's
  resampler). `WC_AUDIO_NO_RATE=1` turns it off.

## Draw-done without waiting for aurora (step 2, seventh run)

- In races the ET draw-done hook (`ET_GXSetDrawDone_80040c1c`) no longer syncs the worker and
  drains aurora's FIFO. It waits only until the worker has replayed the previous frame up to its
  CopyDisp (`GxWorker::WaitUntilDone(g_etCopyDispPos)`). The worker's CopyDisp drain and aurora's
  end of frame then overlap the game's next frame.
- What made that safe: aurora reads indexed vertex arrays and indexed XF (matrix) arrays from the
  pointer it is given when it processes a draw. For race frames the worker copies each array span
  into a per-frame arena (`EtArraySnapshots` in gx_dl.cpp, one copy per array per frame) and hands
  aurora the copy. The arena resets at the next GxWorkerBeginFrame, after aurora's end of frame
  drained the processor. Textures are still read from guest memory by pointer.
- Null-GPU container test at the start of Canada: ~18.5 ms per frame vs 22-29 ms with
  `WC_ET_DRAWDONE_FULL=1` (the old behaviour). Screenshots while driving and crashing look right.
