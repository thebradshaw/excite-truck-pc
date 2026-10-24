# Step 1 findings: taking the game apart

Date: 2026-10-24

## The disc

| | |
| --- | --- |
| Title | Excite Truck (USA) |
| Game ID | `REXE01`, disc 1, revision 0 |
| Dump format | WBFS (scrubbed, so it won't match Redump, which is expected) |
| `main.dol` SHA-256 | `573b5dcf48ebe0e2fba11c5bbcd88cb149696924fea7ed0f98c78986671e76b6` |
| Game code | 3.0 MB of PowerPC code in one DOL, no REL modules |
| Files | 272 files, 558 MB |

The data on the disc is mostly tracks (`.trk`, 48), trucks (`.car`, 40), resource packs
(`.toc`/`.res`), music (`.sng`), three Bink videos (`.bik`) and the standard Home Menu files.

## The code

- Built with the Wii SDK from September/October 2006 (`RVL_SDK ... 0x4200_60422`):
  AI, AX, DSP, DVD, EXI, GX, KPAD, NAND, OS, PAD, SC, SI, VI and WPAD.
- The game engine is the developer's own C++ engine (Monster Games), not Nintendo's EGG/NW4R
  framework that Mario Kart Wii uses. Sound is custom, sitting on top of AX.
- Videos use Bink (`BinkMovie`).
- No symbol map or debug info is on the disc, so functions have no names yet.

| Memory layout | |
| --- | --- |
| Entry point | `0x80004170` |
| `.text` | `0x800049C0`, size `0x2E86A0` |
| `_SDA_BASE_` (r13) | `0x80560900` |
| `_SDA2_BASE_` (r2) | `0x80568100` |
| `.bss` | `0x8034EC40`, size `0x216600` |

## WiiCompiled is a good fit

[WiiCompiled](https://github.com/patchzyy/Wiicompiled) is a working native PC port of Mario Kart
Wii built with static recompilation. Its translator is game-agnostic, and its runtime already
does what this remaster needs: Direct3D 12/Vulkan rendering through aurora, high internal
resolution, any aspect ratio, interpolated frames above 60 fps, SDL controllers and remapping.

**Result:** the translator turns all of Excite Truck's code into C++ with no changes to the
translator itself.

- Following calls from the entry point alone reaches 7,748 functions. A lot of the engine is
  only reached through C++ virtual calls, so `tools/mkmap.py` adds function starts found from
  code pointers in the data sections and from stack-frame prologues.
- With that map: **14,820 functions translated** in about 40 seconds.
- Only 6 functions contain instructions it can't translate, all reads of low-level CPU registers
  (L2 cache, write-gather pipe, exception registers) inside SDK OS code. The runtime replaces
  those functions with native code anyway.

## What step 2 has to do

The runtime replaces about 580 Wii SDK functions with native PC code (GX graphics about 330,
VI, OS, cache, NAND, IOS, PAD, DVD and others), but each replacement is tied to that
function's **address in Mario Kart Wii**. Excite Truck has the same libraries at different
addresses and in an older version. So step 2 is:

1. **Find the SDK functions in Excite Truck.** Match each one the runtime replaces to its
   address in this DOL, using byte patterns, the SDK's own strings and call-graph position.
   Check for API differences between the 2006 and 2008 SDK versions.
2. **Make an Excite Truck version of the runtime.** Swap in the new addresses, remove the
   Mario Kart parts (EGG, race boot, online), and keep GX, VI, OS, DVD/storage, AX audio and
   input.
3. **Input.** Excite Truck steers by tilting the Wii Remote sideways (KPAD/WPAD motion data).
   Map the Xbox controller onto it: left stick to tilt for steering and air control, R/R2 to
   drive (button 2), X to brake (button 1), A for turbo (the D-pad).
   Exact button roles get confirmed against the game.
4. **Bink videos.** Either decode them on the PC (FFmpeg reads Bink) or skip them at first.
5. **Get to the first race.** Boot, menus, then one full race.

## Legal

Nothing from the disc goes into this repository: the DOL, assets and translated C++ are all
`.gitignore`d. Players use their own disc, the same way WiiCompiled works.
