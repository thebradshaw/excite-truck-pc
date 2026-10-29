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
Step 2 (running on PC) is under way: menus, the attract demo, the tutorial and full Excite races
play, with the trucks in their proper colours. Every shader is compiled on the startup screen, so
races don't stutter. See
[docs/porting-notes.md](docs/porting-notes.md).

## Controls

Xbox-style gamepad, in a race: RT or RB drive, X, LT or LB brake, A turbo, left stick steers and tilts the
truck in the air. Air control is inverted by default (push up to dip the nose); turn that off with
"Invert air control" in the F10 settings bar under Controllers. A also presses the "2" button, so
it answers the "press 2" boost after a crash, even while R is held. Y is the Wii A button.

In menus and the pause menu: A selects, B goes back, and the left stick or D-pad moves.

New profiles skip the tutorial that the game normally forces; it is still in the main menu.

The game opens borderless full screen at your monitor's resolution (1080p on a 1080p screen).
Alt+Enter switches to a window; F10 has the display and resolution settings.

## Your own music

Drop MP3s into `ExciteTruck/UserData/Music` (the build scripts create it). Subfolders are fine, up to 100 songs. In the game, set Options > Race Music to
All Random and your songs play during races, as with an SD card on the Wii.

## Layout

| Path | What it is |
| --- | --- |
| `third_party/wiicompiled` | The [WiiCompiled](https://github.com/patchzyy/Wiicompiled) recompiler and runtime (GPLv3), pinned |
| `projects/excite-truck/recomp.yml` | Translator manifest for the US disc |
| `projects/excite-truck/MAP.txt` | Function start addresses that seed the translator |
| `tools/` | Small Python helpers for reading the game's `main.dol` |
| `scripts/translate.sh` | Runs the translation |

## Building and playing

You need your own copy of the US disc (REXE01) as a .wbfs, .iso or .rvz image (a .zip holding
one works on Windows). The scripts check the disc, fetch the pinned WiiCompiled sources, apply this
repo's patches, translate the game's code to C++, compile it and put the game in `ExciteTruck/`.
The first build takes a while (mostly compiling); nothing from the disc ends up in this repo.

**Windows 10/11, easiest:** drag your disc image onto `ExciteTruckSetup.exe`. It needs nothing
installed, builds the game into `%LOCALAPPDATA%\Programs\ExciteTruck` and puts "Excite Truck" and
"Excite Truck Music" shortcuts on the desktop. Windows may warn that the exe is unrecognised (it
isn't signed): click More info, then Run anyway. `scripts/make-setup.sh` builds the exe.

**Windows 10/11, from a checkout** (needs [Git for Windows](https://git-scm.com/download/win), for example
`winget install Git.Git`; the compiler, CMake, Ninja, .NET and libraries are downloaded with
pinned checksums):

```powershell
git clone https://github.com/thebradshaw/excite-truck-pc.git
cd excite-truck-pc
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 "D:\Games\Excite Truck (USA).wbfs"
```

Then run `ExciteTruck\ExciteTruck.exe`. Build files go to `C:\ETBuild` (about 20 GB, safe to
delete afterwards; `-Work` picks another folder).

**Linux** (Debian/Ubuntu: `sudo apt install git cmake ninja-build clang python3 curl dotnet-sdk-8.0`):

```sh
scripts/build-linux.sh "/path/to/Excite Truck (USA).wbfs"
./ExciteTruck/ExciteTruck
```

The game folder is portable: settings, saves and caches live in `ExciteTruck/UserData`, and your
songs go in `ExciteTruck/UserData/Music`. Running the script again after `git pull` rebuilds only
what changed.

## License

GPLv3, matching WiiCompiled.
