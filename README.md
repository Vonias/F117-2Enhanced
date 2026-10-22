# F-117A Enhanced — SDL3

An enhanced native port of MicroProse's *F-117A Nighthawk Stealth Fighter 2.0* (1991), built on the faithful
SDL3 port (`../F117-2`, v0.1.0), in the making. Not an emulator. The original game files are not included.
Goal: a sharper, smoother game (higher resolution, higher frame rate) with the original art unchanged.

Status and plan: [PLAN.md](PLAN.md) (as a page: `python tools/plan_site.py`, then open
`site/index.html`). Agent rules: [CLAUDE.md](CLAUDE.md). Engine rules: [ENGINE.md](ENGINE.md).

## Features

* `--classic`: the original picture and behaviour, as the faithful port.

## Building

```bash
cmake -S f117port -B f117port/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
cmake --build f117port/build
ctest --test-dir f117port/build
```

Needs CMake 3.24, a C11 compiler and SDL 3 (MSYS2 `mingw64`). The program is `f117port/build/f117-enhanced.exe`
(`--game-dir Game`; `Game/` is a junction to the faithful repo's game folder).

## Launcher

The build also makes `f117port/build/f117-enhanced-launcher.exe` (wxWidgets 3.2, MSYS2 `mingw-w64-x86_64-wxwidgets3.2-msw`;
`-DF117_LAUNCHER=OFF` skips it). It is the faithful port's launcher (game folder, window, speed, sound, key bindings)
plus an **Enhanced** box; its settings are kept in `%APPDATA%\F-117A Enhanced\settings.ini`.

```powershell
& "C:\Coding\F117-2Enhanced117portuild117-enhanced-launcher.exe"
```

* **Render scale**: Auto, or 1 to 8 times the original 320x200 (`--render-scale N`).
* **Wide view**: more of the world beside the 4:3 picture on a wide window (off: `--no-wide`).
* **Smooth distance shading** (`--smooth-shading`) and **True night** (`--true-night`): off by default.
* **Classic** (`--classic`): the original game; greys out the four options above.


## Layout

* `f117port/src/` — the faithful engine (`ENGINE.md`; changes marked `ENH:`).
* `f117port/src/enhanced/` — the enhancements.
* `port/notes/`, `port/spec/*_symbols.csv`, `port/symbols.csv` — the verification harnesses and symbol tables the
  build and tests use (copied from the faithful repo).
* `tools/` — symbol generators, packaging and the plan page.

## License

MIT ([LICENSE](LICENSE)); third-party licences in `licenses/`. *F-117A Nighthawk Stealth Fighter 2.0* is ©
MicroProse; its files are not included.
