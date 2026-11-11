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
  against it, plus a shake strength from how fast it changes. It only looks at the y and z of the
  vector. Results land in the player controller (pointer at 0x805785D0): +0x28 flick, +0x29 a
  flick up and to the side, +0x2A a hard drop along gravity, +0x30 shake. Crash recovery
  ("SHAKE!!!") and letting go of a bar ("Push") use the flick. Grabbing a bar is automatic.
- Pad layout in a race: RT/RB accelerate (Wii 2), X and LT/LB brake (Wii 1), A turbo, B the Wii
  A button (weapons), Y also Wii A, left stick steers and tilts. Menus: A OK, B or X back.
- Motion prompts are spotted by their textures (`EbScreenTextures` in gx_texture.cpp) and played
  with the left stick. Bars are played with A (round 12, Chris: "tapping on the A button
  repeatedly to gather speed, or a timed A button to release"; round 11's press-to-charge window
  did not feel like gathering momentum). On a red (horizontal) bar A is mashed as in
  crash recovery (round 13, Chris: "increase speed the faster the A button is pressed"): the
  remote thrusts back and forth 1.5 times per press per second (presses counted over the last
  0.75 s of race time, 2 to 8 thrusts a second), each thrust also rocking its pitch the other way.
  On School_Mexico 1.7 presses a second took 9.4 s on the bar for "Red Bar" (5 stars), 8 a
  second took 6.7 s for "Super Red Bar" (10 stars). Its hint is crash recovery's A: up, then
  pressed, ten changes a second. On a yellow
  (vertical) bar one press of A is a forward thrust, which lets go about 0.7 s later; the game's
  "Push" label lights up as the cue. The hint is an A button that stays still and shrinks and
  grows: 0x802C27B0 draws 2D sprites from (x, y, z, scale x, scale y, angle) and first calls cos
  (0x80032990) on the angle, so cos is a native override (`EB_cos_80032990`, the library's own
  kernels) that, when called from there for the prompt's sprite, pins its position (already on
  the stack at r1+68), scale and angle to the first frame's. The pulse is in the texture: four
  marked copies of the Push texture and of its I4 mask (hashes in tools/prompts/make_eb_pack.py),
  drawn 4/3 wide because the sprite is 3/4 as wide as its texture. Five pulses a second on a red
  bar, two on a yellow one. Without the prompt pack the Wii Remote picture is left alone.
  `WC_EB_TEST_TAP=S` taps for tests; `WC_EB_TRACE_HINT=1` logs the sprite. What a tap does is what
  rounds 9 and 10 did on their own (`WC_EB_AUTO_BARS=1` still does): while "Push" is up the
  remote's pitch rocks a full tilt forward and back every 0.25 s of race time
  (`WC_EB_PUMP_HALF`), and the bar spin's shake rocks it side to side at full speed, whatever
  the stick does. What spins the bot round a bar is that pitch rocking: on the bar challenge's
  bar it cut the hang from 3.8 s (no input, or flicks) to 1.1 s (0.5 s rocking: 1.8 s; 0.12 s
  aliases at the container's 4 fps). A flick does nothing there, and a push (acceleration along
  the remote's x, which the motion detector reports as controller +0x34, flag 0x10 in the input
  record built at 0x801EC250) lets go of the bar. Red (horizontal) bars are the exception
  (round 10): their meter fills only from those x thrusts, forward then back, so while the red
  bar's own texture (256x256 CMPR 78583d305e906e63, never drawn on a yellow bar) is up the remote
  also thrusts each way every 0.5 s of race time (`WC_EB_REDBAR_THRUST`). On School_Mexico that
  filled the meter to the top in one swing and launched a record jump; pitch rocking alone had
  reached two stars. With A held the same run also filled it to the top. `WC_EB_MANUAL_BARS=1` restores the manual
  controls below. Manual: on "Push" (swinging on a bar) every stroke of the stick plays a swing:
  each time it reaches a new direction past 50% (forward, back, left or right), so a fast
  forward-back flip is two swings even when it skips the centre between reads (round 8: waiting
  for the centre missed most strokes). Held in one direction, or with A held, it swings again
  every 12 reads. A swing asked for while one plays waits for it and two reads at rest, so the
  game sees each as its own flick (checked here: one game flick per swing). A swing is three
  reads with the acceleration thrown against gravity at twice its
  strength. On the crash meter (256x256 CMPR fcf8a1191fd23f9c) each change of side while shaking
  the stick left and right plays a swing. On the shaking remote without the crash meter (bar
  spin, dentures) the stick pushed in any direction (past 40%) rocks the remote at full speed, so
  holding or wiggling it charges as fast as the hardest real shake (round 8: steering the stick only
  rocked as fast as the player wiggled, and forward and back did nothing).
  `WC_EB_STICK_STEER_SHAKE=1` goes back to the old steer rock; `WC_EB_TEST_STICK=1` fakes the held
  stick and `WC_EB_TRACE_SHAKE=1` logs the detector's shake values. A (pad), C or Space (keyboard)
  still work: a swing per press, or held on a bar spin, a rocking remote. A is the main way out of a
  crash, so the crash prompt shows A (the stick shake is the backup).
- Any menu's Back / OK icons on screen (pause, Training cards, results) switch the pad to the
  menu layout, even while the race clock runs.
- The "SHAKE!!!" text over the crash meter is cut to an empty string in guest memory as each race
  starts (it comes with the race's message table).
- The Guide button opens the runtime's "Quit the game?" prompt (A exits, B cancels) instead of
  the Wii HOME menu. On the crash prompt the meter fills and the bot
  boosts out; on a bar it lets go. `WC_EB_AUTOSWING=N` swings every N reads for testing.
- The remote pictures on the shake prompt (overlays.res `shakecont.art`: crash, bar spin,
  dentures) and the bar prompt ("Push") are replaced by the left stick, pushed left and right or
  flicked up. While the crash meter is up, `GX__LoadTexObj` hands aurora a copy of the shake
  texture with its first 8 bytes (hidden by the mask) marked `EBcrashA`, so it hashes as
  e15dafb8e52aaa7e and the pack draws the A button there instead, up and pressed
  (`tools/prompts/make_eb_pack.py`, installed to
  `prompts/excitebots`; `[excitebots] xbox_prompts` or the F10 Controllers checkbox turns it off).
- The round button icons shared with Excite Truck (common.res: Wii 1, 2, A, +, -) are redrawn as
  B, A, B, Menu and View.
- The "how to" cards after a race's intro screen (red bar, yellow bar, leg run, glide: Wii Remote
  instructions) are not queued: 0x800113C0 picks them by comparing the event's `howto_guide`
  against names at 0x80477511, 0x80477567, 0x804775CC and 0x80477623, which `EbFrameTick`
  breaks once at boot (`DisableHowToCards`). Minigame how-to cards are left alone.
- Audio at 60 FPS: the race leaves the guest thread little idle time, so the 3 ms audio blocks
  arrived in bursts and the output queue sat near empty (about 5 ms), crackling. A tick now
  catches up the whole backlog (up to 40 blocks) once 4 are due, the output controller corrects
  by up to 1.5% (was 0.5%), and a dry queue is refilled with 120 ms of silence (was 40). The
  queue is kept around 120 ms (was 90) so a slow frame does not empty it, and a gap between audio
  services of up to 250 ms (was 100) is caught up instead of lost. The made audio itself is clean
  at 30 and 60 FPS (checked with the dump). `WC_TRACE_AUDIOQ=1` prints the queue each second
  ("low": pushes that found under 2 blocks queued, "in": audio made so far over real time) and
  each gap over 50 ms; `Excitebots-audio-log.bat` in the install folder runs the game with it into
  `audio-log.txt`. `WC_AUDIO_DUMP=<file>` writes the raw output.
- Menu changes (`eb_menus.cpp`, all off with `WC_EB_STOCK_MENUS=1`): layouts come through
  0x801688C0 (name, magic), which is overridden to edit them as they are handed over. `.lyt`
  ('LAYT') layouts are lists of 0x134-byte records (type, x +8, y +0xC, name +0x20, text +0x40)
  from +0x68, little endian on the first load (the loader swaps them after). Main menu
  (main_2.lyt): Nintendo WFC (`multi`) and its mail icon are deleted from the list and the
  buttons under it move up a row; the cursor follows the list. Post-race menu (postrace.lyt):
  "Send..." (`challenge`) is deleted and the other three move down half a row. Pause Options
  (vollyt.val, a "BAZ!" value file): the Controller row's elements are moved off screen by their
  y (key 0x790). The post-race tips screen (0x8030CE50, posttips.lyt) is not run.
  `WC_EB_TRACE_RES=1` logs layouts (2: every resource, 3: dumps each layout's records).
- On a bar (Push or the spin's shake prompt) and while shaking something off, the gas (2) is
  dropped, since holding it kept swings from registering (`WC_EB_GAS_ON_BARS=1` keeps it).
- Audio after a stall: the game side only makes as much audio as fits in about 200 ms of output
  queue (the rest of a stall is skipped); catching a long stall up at once overfilled the queue,
  which dropped blocks (clicks), mostly in the stage intro. The queue limit is 400 ms.
- Audio clock (round 7): the audio poll's wall-clock cursor was set to `now` each time while only
  whole microseconds were handed out. A race polls about 100,000 times a second, so the dropped
  fractions lost about 5% of the time; the game made too little sound and the output sat nearly
  empty (scratchy, worst in the stage intro). The cursor now advances by what it hands out.
  `WC_TRACE_AUDIOTIME=1` prints wall time, polled time and blocks per second. The fix itself is in
  patch 0001, so Excite Truck has it too; 0004 only adds the trace.
- The three screens before the title (strap, ESRB, hold) are skipped: the boot step at
  `0x805A47F0` is set to 11 (done) each frame until the game reaches it (`EbFrameTick`).
- `-direct_to_track:@mitch5_bar_challenge` (the yellow bar minigame) reaches a bar in about
  40 seconds. Other contests: `@Silver_Finland`, `@Platinum_Fiji`, `@School_Fiji` (yellow bars),
  `@School_Mexico` (red bars). The names come from the `*_eves.val` tables in the res packs.

## Shaders and achievements

- `assets/shaders/excitebots/initial_pipeline_cache.db` holds the 591 shaders seen in menus and
  races (Fiji, Finland). The build compiles them on the startup screen. Export more with
  `scripts/export-shaders.sh <UserData/Cache/pipeline_cache.db>` and copy the result there.
- Local achievements (23, `runtime/src/hle/excitebots/eb_achievement_*.inc`) share Excite Truck's
  module and files (`UserData/achievements`, F10 Achievements, pop-ups). They read the game's own
  stunt tallies, the ones behind its in-game Achievements screen: `*(0x805A1A0C)` + 0x1C + 4c
  (saved) and + 0x68 + 4c (this session), 19 categories in the order of the table at 0x804A0BC8.
  The tallies only move in normal races, not in `-direct_to_track` ones, so they are checked
  against the code but not yet in play.

## Rendering and saves

- Excite Truck's fix for the menu videos copies textures to fresh memory at each load outside
  races. Excitebots reads its own race state for that, and render-to-texture targets (GXCopyTex
  destinations) are never copied, which caused flickering scenery and white bots.
- An Excitebots save left by builds before this fix (inbox files only, no banner.bin or
  roam.dat) is cleared at launch: the game calls it corrupted on a pointer-driven dialog a pad
  cannot answer.
- WiiConnect24: the KD "try suspend scheduler" probe answers success for Excitebots (Mario Kart's
  two failing boot probes made Excitebots show error 109042 when loading a profile).
- 60 FPS races: the frame pacer (`0x803D0C00`) waits for the number of retraces requested at
  `0x805A1768` (`-0x6878(r13)`). Race start (`0x800096C0`) asks for 2 (30 FPS);
  `EbFrameTick` sets it back to 1 while a race runs, and the engine's per-frame step
  (`0x805A2CE4`) follows, so the race clock stays in real time. `WC_EB_30FPS=1` keeps 30.
- Sky streaks: some effect meshes (arrays in a per-frame MEM2 arena, `0x900Fxxxx`/`0x901Fxxxx`)
  are still being written after their draw is queued; the Wii GPU fetches them later. Aurora
  copied them at draw time, so stale or unwritten vertices (`0xE3E3E3E3` fill) made long thin
  triangles. `patches/excitebots/aurora/0001-late-vertex-arrays.patch` adds
  `aurora_set_late_array_window`: indexed arrays from guest RAM are copied again at frame end
  (unless the same source was staged twice that frame). Excitebots turns it on in
  `GxGuestWrite::InstallAuroraHooks`; `WC_EB_EARLY_ARRAYS=1` turns it off for comparison.
- The Wii Remote / Wii Wheel screen (`0x80326930` new profile, `0x80326A80` Options) is replaced
  by native overrides that answer "chosen" at once, so the default controls are used.
- Async NAND calls return OK and report the result through the callback, as on the Wii (for
  Excitebots only). The save code accepts EXISTS from the callback but treated a direct EXISTS as
  "An error occurred while accessing Wii system memory" on the second save of a new profile.

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
- Xbox layout in races and menus; the left stick plays the motion prompts.
- Shader list recorded; local achievements in place.
- Next: Training's lesson cards (Wii Remote pictures and text), play through a normal race to
  see an achievement unlock.
