# Engine rules — F-117A (from the faithful port v0.1.0)

The engine in `f117port/src/` is the faithful port's. This file condenses its rules so that Enhanced sessions do
not need the faithful repo's docs for everyday work. Detail: `f117port/PORTING.md` (copied, searched) and
`../F117-2/port/spec/*.md` (platform, video, sound, frontend, shell, simflow, render3d, flight, debrief, HANDOFFS;
searched, never dumped).

## Architecture
* One process runs the original chain (SETUP, MPS_LOGO, PLAYER, DSWAP, START, VGAME, END) in `src/prog.c`, which
  replaces F117.COM; programs talk through the `comm` block (`src/comm.h`, HANDOFFS.md).
* Modules follow the specs: `platform video sound frontend shell sim render3d flight debrief` (one folder each).
  Each ported function's comment cites its spec section and original address (`SSSS:OOOO`).

## Memory model
* Original DGROUP state is kept at its original offsets (`DSW(off)` / `DSB(off)` accessors, `mem.h`); several routines
  rely on adjacency (render3d.md section 9, `R3dWork`). Names come from the generated headers `src/gen/`
  (`symbols_<image>.h`, `module_<spec>.h`), made by `tools/merge_symbols.py`, `gen_symbols.py`, `gen_module_headers.py`
  from `port/spec/*_symbols.csv`; the `gen_*_current` ctests fail when they are stale.

## Timing
* `host_pump()` delivers one tick per VGA retrace (70.09 Hz, exact rational). Each program's ISR body (7th-tick skip,
  sound poll, VGAME's `[3660]` frames-per-second estimator) is installed with `timer_set_hook`.
* VGAME runs **one sim step per drawn frame** (`39c4`); motion is scaled by `sim_rate` / `[3660]` (flight.md 1.3), so
  the sim step rate and the display rate are the same thing in the original. `--frame-ticks N` (default 7, 10 fps)
  is the faithful port's floor on ticks per frame (`sim_main.c frame_floor`).

## Video
* VGA mode 13h model (`platform/vga.c`): 320x200 8-bit pages (page 0 = A000, off-screen pages by segment), 256-colour DAC.
  The host converts the presented page through the DAC to XRGB and scales it (4:3 or 1:1, nearest) in `host.c`.
* 2D: `video/mgraphic.c` (fonts, lines, fills, blits, sprites), `pic.c`/`pan.c` (pictures, animations).
* 3D: `render3d/` — `r3d_scene_draw` (`0000:03CE`) per frame: scene set-up, terrain, sorted queue, object renderer
  (11EB), clipper (12EF), raster (1359, mode M spans), sky (1442), fixed-point maths (1433). Screen coordinates are
  16-bit integers of the 320x200 page; the 3D window is drawn into the off-screen page, then the cockpit
  (256PIT/LEFT/RIGHT/REAR .PIC), HUD and MFDs are drawn over it and the page is flipped.

## Input / sound
* SDL keys → XT scan codes (`host.c` → `platform/kbd.c`); `--keys` remaps (`keymap.c`). Gamepad → synthetic port 201h.
* AdLib through Nuked-OPL3 (`sound/`), polled from the ISR bodies.

## Verification (must stay green in `--classic`)
* `ctest --test-dir f117port/build`: the original programs run under Unicorn (`port/notes/s5*/`, `work/unpacked/`) and
  are compared byte for byte with the port: 110 render3d golden scenes, 1497 sim/flight scenarios, shell, debrief,
  sound register streams, chain runs. Tests skip (77) without `Game/` or `work/unpacked/`.

## Where to hook (ENH:)
* Presentation: `host.c` present path (output size, layers, filtering, frame pacing).
* 3D: `r3d_scene_draw` and the raster/clip back end (a high-resolution path beside the 320x200 one).
* Frame loop: `sim_main.c` (frame floor, the per-frame `39c4` step, the flip) for decoupling sim and display.
* HUD/MFD vector drawing (`sim/`, `video/mgraphic.c` lines) for crisp lines at high resolution.
