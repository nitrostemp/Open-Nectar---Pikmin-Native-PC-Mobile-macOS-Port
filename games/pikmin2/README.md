# Open Nectar 2 — Pikmin 2 Native Port (experimental)

<img width="2172" height="476" alt="opennectarlogo (1)" src="https://github.com/user-attachments/assets/71283101-1be5-4ca4-9b16-488320343cc8" />

Native, experimental port of *Pikmin 2* (GameCube, 2004) for **Linux and
macOS**, part of Open Nectar Fusion. Like the Pikmin port in `../pikmin1/`, it
compiles the game's own code for the host system and translates GX to OpenGL;
it does not use Dolphin or any emulator.

It builds on the decompilation by
[projectPiki/pikmin2](https://github.com/projectPiki/pikmin2), vendored in
`pikmin2-decomp/`, and on the Pikmin 1 port's native layer, adapted here in
`pc_port/`.

## Status

This is a work in progress and has no release packages yet.

**Works:**
- Boots, runs the memory card check and the opening movie, and plays the
  first day with the European disc
- OpenGL renderer shared with the Pikmin 1 port: TEV specialisation, shadows
  and the F1 settings menu
- The game's own JAudio engine for music and effects, through a software DSP
- Keyboard and controllers through SDL
- Started from the Fusion launcher next to Pikmin 1

**Known gaps:**
- **Only Pikmin 2 Europe** (`GPVP01`) works; the port is developed against it.
  The USA disc (`GPVE01`) stops at boot.
- **Audio is drier than on console**: the DSP effect lines that carry reverb
  and echo are not implemented yet (`JAInter::Fx` skips them), and fades and
  some effects are still being checked (see `PRUEBAS_PENDIENTES.md`).
- No installer yet: the launcher shows Pikmin 2 as "Coming soon" until its data
  is set up by hand, as below.

## Game data

The game reads the disc's files from `assets/` in this folder. Use your own
copy of Pikmin 2 Europe as an ISO or GCM; convert RVZ/WIA/GCZ images to ISO
with Dolphin first (right-click the game, **Convert File…**, format ISO).

From the repository root:

```sh
scripts/extract-gc-disc.py "/path/to/Pikmin 2 (Europe).iso" games/pikmin2/assets
touch games/pikmin2/assets/.pikmin2-assets
```

The extractor only reads the image. `.pikmin2-assets` tells the launcher the
data is in place. Everything in `assets/` except its README is ignored by Git
and must not be uploaded or redistributed.

## Building

Pikmin 2 is built together with Pikmin 1 by the Fusion scripts, from the
repository root:

```sh
scripts/build-fusion.sh
scripts/run-fusion.sh
```

The first builds both games into `build/` (Pikmin 2 as
`build/pikmin2/pikmin2_pc`); the second opens the launcher, where Pikmin 2 has
a **Play** button once its data is in place.

### Linux

```sh
# Debian/Ubuntu
sudo apt install build-essential cmake pkg-config libsdl2-dev libgl1-mesa-dev
```

On its own, from this folder:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)" --target pikmin2_pc
ctest --test-dir build --output-on-failure
```

### macOS (Apple Silicon)

```sh
xcode-select --install
brew install gcc cmake ninja sdl2
```

`scripts/build-fusion.sh` builds Pikmin 2 through
`scripts/build-pikmin2-macos.sh`; it can also be run on its own. Two things
differ from Linux, and the script handles both without changing this folder:

- **GCC, not Apple Clang.** The decompiled code relies on GCC's permissive mode
  (pointers held in 32-bit integers, templates using types defined later),
  which Clang rejects. The script uses the newest Homebrew `g++-N`.
- **A case-sensitive disk image.** Some headers differ only in capitalisation
  (`system.h` and `System.h`, the adapter's `Dolphin/Mtx.h` and the decomp's
  `Dolphin/mtx.h`), which macOS's file system cannot tell apart. The script
  mirrors this folder onto `build/macos-cs.sparseimage`, a sparse image that
  only takes the space it uses, and compiles there.

The tests run from that build:

```sh
ctest --test-dir build/macos-cs/build-RelWithDebInfo --output-on-failure
```

macOS also needs a few code differences, all marked in the sources: a Core 4.1
OpenGL context, the window framebuffer bound around each buffer swap, the
decomp's libc headers kept out as glibc's guards keep them out on Linux, and
the game's `operator new`/`delete` kept private to the executable, so Apple's
frameworks never allocate from the game's heap.

## Saves and settings

The memory card is `save/card0/Pikmin2_SaveData` beside `pikmin2_pc`. If that
folder cannot be written, it falls back to the user's data folder:
`~/.local/share/pikmin2-native/save` on Linux and macOS (`$XDG_DATA_HOME` if
set), `%LOCALAPPDATA%\Nectar2\save` on Windows. `NECTAR_SAVE_DIR` overrides
both. Pikmin 2 never shares a card folder with Pikmin 1.

Settings from the F1 menu are kept in `pikmin_settings.conf` in the folder the
game runs from.

## Project structure

```
.
├── pikmin2-decomp/          # Vendored projectPiki/pikmin2 decompilation
├── pikmin2-decomp-adapter/  # Forced-include header and shims that let it build on PC
├── pc_port/                 # Native layer: GX→OpenGL, audio, input, memory card
├── include/                 # Headers the native layer shares with the Pikmin 1 port
├── assets/                  # Your extracted game data (not in Git)
└── CMakeLists.txt
```

## Legal

You need your own legally obtained copy of Pikmin 2. No Nintendo game data is
included in this repository or its packages. See `LEGAL.md` and `LICENSE.MD`.
