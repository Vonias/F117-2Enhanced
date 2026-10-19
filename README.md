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

## Layout

* `f117port/src/` — the faithful engine (`ENGINE.md`; changes marked `ENH:`).
* `f117port/src/enhanced/` — the enhancements.
* `port/notes/`, `port/spec/*_symbols.csv`, `port/symbols.csv` — the verification harnesses and symbol tables the
  build and tests use (copied from the faithful repo).
* `tools/` — symbol generators, packaging and the plan page.

## License

MIT ([LICENSE](LICENSE)); third-party licences in `licenses/`. *F-117A Nighthawk Stealth Fighter 2.0* is ©
MicroProse; its files are not included.
