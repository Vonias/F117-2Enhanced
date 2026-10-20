# F-117A Enhanced — plan

Built on the faithful port (`../F117-2`, release v0.1.0, commit d05175f). The faithful engine runs unchanged
(`ENGINE.md`); enhancements live in `f117port/src/enhanced/` and hook into the engine at marked `ENH:` points.
`--classic` turns every enhancement off.

**Goal: higher resolution and higher frame rate, without changing the original art.** The original pictures,
fonts, sprites, palettes and 3D models are used as they are; what changes is how the 3D view and the vector
graphics are rasterised (at a multiple of 320x200) and how often frames are drawn (display rate instead of the
original's one frame per sim step), and the 3D view can be wider and taller than the original: the cockpit art stays
in place, unchanged, and the world is simply shown further to the left, right and above it (Decisions 2026-10-19).
A longer view distance would change what the original shows and is parked.

Token efficiency is a first-class goal: small subtasks, one session each, the cheapest model that can
do the job, and the user doing what is cheap for a human (looking at pictures, playing).
Tracker: `python tools/plan_site.py` → `site/index.html` (this plan plus token usage per subtask).

## User to-do

What the agents need from you now, most urgent first. Agents add a row when they hand you a task and
remove it when it is done.

| Task | What to do | For | Time |
|---|---|---|---|
| U1 | Fly with `& "C:\Coding\F117-2Enhanced\f117port\build\f117-enhanced.exe" --game-dir "C:\Coding\F117-2\Game" --render-scale 4` (PowerShell) and compare with `--classic`. Check that the HUD ladder rungs, the airspeed / altitude tape lines and ticks, the heading tape, the glass frame, the reticle bars and (F9, near the landing site) the ILS needles, the attitude indicator's horizon and the map lines (F8 = the navigation page draws the route legs on the left MFD map; F3 = the scope rings) are thin and sharp, while the numbers, labels, the reticle / flight-path symbols and the panel are still the original pixels. HUD colour modes: F4 = the other HUD colour set, `v` = HUD style 0/1/2, `c` = small view, Alt-N = night: the lines must keep following the colour. Contact sheet of the real frames (classic left, hr right): `work/e33/contact_hud.png`, single crops `work/e33/hud_<state>_<hud|mfd>_<classic|hr>.png`. | S3.3 | 10 min |

## Status

- **Done:** S0.1 (repo from the faithful port), S1.1 (`DESIGN.md`, the render and timing design), S2.1 (canvas, compositor, shadow plane; U1 OK 2026-10-19), S2.2 (render3d at N×; U1 OK 2026-10-20), S2.3 (tests, reference rasteriser, dumps; U1 OK 2026-10-20), S2.4 (sandboxed display pass, hash ctest), S3.1 (objects and views at N×; U1 OK 2026-10-20), S3.2 (TV camera and map MFD at N×; U1 OK 2026-10-20), S3.3 (HUD and MFD vector graphics at N×; U1 pending).
- **Next:** S3.4 (Opus): wider view, per-view canvas geometry, widened culling (`DESIGN.md` 3). Your tasks: **User to-do** above.
- Escalation rule: a subtask that fails twice on Sonnet → new Opus session with a 5-line note
  (symptom, file, what was tried). Never carry an old transcript over.

## Model guide

| Model (app model picker) | Effort | Use for | Relative cost |
|---|---|---|---|
| **Haiku** | low–medium | Packaging, launcher wiring of existing options, README/CHANGELOG/PLAN bookkeeping, regeneration | 1× |
| **Sonnet** | medium | Default: features on a finished design (back ends, panel/HUD, input, settings), bug fixes with a known cause | ~3× |
| **Opus** | high | Architecture (render back end, display lists, frame interpolation), projection/clipping/timing changes, cross-module bugs, escalations | ~5× |

## How to run a subtask (user, every time)

1. Code tab → **New session** in `C:\Coding\F117-2Enhanced` (never continue an old session for a new subtask).
2. Pick the model from the subtask table; set the effort.
3. Send exactly: `Do S1.1 from PLAN.md.` (with the subtask id).
4. When the agent says it is done: glance at the commit, do any U-check it asks for, close the session.

## Subtasks

Model: H = Haiku, S = Sonnet, O = Opus. Status goes in the row's last cell (`done YYYY-MM-DD`,
`U2 pending`); notes go in the "Notes:" list under each table, never between rows.

### Phase 0 — Repository
| Id | Task | Model | Size | Status |
|---|---|---|---|---|
| S0.1 | Repo from the faithful port at v0.1.0: engine, build, tests, `src/enhanced/`, `--classic`, PLAN/CLAUDE/ENGINE/README | O | M | done 2026-10-19 |

Notes:
- Copied from `../F117-2` at d05175f: `f117port/` (minus the launcher; `copy_dlls.cmake` moved to `f117port/cmake/`),
  `third_party/`, `port/notes/` (the Unicorn harnesses the ctests run), `port/spec/*_symbols.csv` + `port/symbols.csv`,
  the symbol generators, `make_release.py`, `LICENSE`, `licenses/`. Junctions: `Game` and `work/unpacked` → `../F117-2`.
- The executable is `f117-enhanced` (CMake target still `f117`, so the copied tests work unchanged).

### Phase 1 — Design
| Id | Task | Model | Size | Status |
|---|---|---|---|---|
| S1.1 | `DESIGN.md`: (a) high-resolution rendering: a 3D layer rasterised at N×320x200 (render3d's projection, clipper and spans at N× with sub-pixel precision; the 8-bit DAC colours kept), composited under the original 320x200 cockpit/HUD art scaled by N (nearest); which vector elements (HUD ladder, tapes, reticle lines, map lines, TV window) can also be drawn at N×; the cockpit window mask. (b) frame rate: decouple VGAME's sim step (`39c4` once per frame, scaled by `sim_rate`) from the display: the sim at a fixed step rate, the display at the monitor rate with interpolation of camera, objects, rotating parts, missiles, tracers and smoke between two sim states; what the HUD/MFDs show between steps. (c) the wider view: a viewport larger than 320x200 (left, right and top), the original 320x200 cockpit/HUD frame kept
at its place (bottom centre, scaled by N), the projection's field of view and culling widened to the new edges, what
fills the extended area in each view mode (front cockpit: extended around the art; left/right/rear views: the whole
picture scaled up to fill; full-screen 3D, outside and map: extended), per Decisions 2026-10-19; (d) the `ENH:` hook list, how `--classic` stays byte-exact, and the test plan (N=1 equals classic byte for byte; golden frames at N×). | O | M | done 2026-10-19 · U3 pending |

### Phase 2 — Foundations
| Id | Task | Model | Size | Status |
|---|---|---|---|---|
| S2.1 | Host: canvas separate from 320x200 (any aspect, `--render-scale N`, `--wide`), the compositor (3D layer + vector layer + shadow plane, live DAC/CRTC) and the shadow records in `mgraphic.c`; `--classic` = today's present path (DESIGN 2.1, 2.5, 3.1) | S | M  done 2026-10-19 · U1 OK 2026-10-19 |
| S2.2 | render3d at N×: the hr back end (`hr_clip/hr_raster/hr_edge2d` copies) fed by hooks in projection, edges, clipper, spans, sky and 1001; rule R1; N=1 byte-identical (DESIGN 2.2–2.4) | O | L | done 2026-10-19 · U1 OK 2026-10-20 |
| S2.3 | Tests: the faithful suite in `--classic`; golden scenes at N=1 through the hr path; reference-rasteriser comparison at N=2/4; headless PNG and mask dumps for the user (DESIGN 5.3) | S | M | done 2026-10-20 · U1 OK 2026-10-20 |
| S2.4 | Sandboxed display pass: snapshot/restore of mem, VGA model, driver statics and layers; sound/wait suppression; `enh_frame_begin`/`enh_idle` hooks; the sandbox hash ctest (DESIGN 4.2) | O | M | done 2026-10-20 |

Notes:
- S2.1 status: `src/enhanced/enh_canvas.[ch]` (geometry, layers, shadow plane, compositor, mouse mapping), `enh_canvas_test.c` (`--compose-test`, ctest
  `compose`). Options: `--render-scale N` (default 0 = the largest whole multiple that fits the window), `--wide` / `--no-wide` (default off for now: the
  extended area is black until S2.2; the default turns on with S3.4), `--classic` clears all. The canvas is on by default in mode 13h only (text mode and
  `--classic` keep the faithful path). View placement: `view_frame` calls `enh_set_view` (0 front, 0x41-43 fit, other full); a mode set resets to the menu
  placement; VGAME's own menus are not yet told apart (S3.x). The shadow is double-buffered with the page flip (work records move to shown in `flip()`, with the
  layers). Layers are u16 (bit 8 = written, low byte = DAC index); a record whose layer is still empty falls back to the page value, so the empty-layer canvas is
  the classic frame scaled N x N. Shadow hooks so far: `put_pixel`, `line`, `poly_fill`, `sprite_scaled`, `flip`, `page_load`, `clear_screen`, `clear_es`; NOT yet:
  `hud_tick_strip` (S3.3), the clear in `r3d_set_viewport` and setting `enh_route` (S2.2). The faithful ctests run with `F117_CLASSIC=1` (set per test in
  CMakeLists). In this repo `video_decoders`, `frontend`, `gen_symbols_current`, `gen_module_headers_current` fail for reasons unrelated to the code (no
  `port/spec/*.md`, no `picdec`/`panlib` tools here; they look in this repo instead of `../F117-2`); `sound` skips.
- S2.2 implements both dither modes (`--dither grid|fine`, Decisions 2026-10-19); S2.3 shows them to the user (U1).
- S2.2 status: the hr back end `src/enhanced/hr_pass.c`, `hr_clip.c`, `hr_raster.c`, `hr_edge2d.c` (`hr.h`), fed by `ENH:` hooks in `r3d_set_viewport`,
  `r3d_end_scene`, `r3d_project`, `r3d_build_edges`, `r3d_poly_add_edge`, `r3d_poly_finish`, `r3d_line_M`, `fill_M` (shadow), `r3d_draw_sky_ground`, `r3d_project_point`;
  the choices are in `DESIGN.md` 2.7. Every 3D pass on page 0/1 is drawn at N× into the 3D layer (main view, TV camera, map MFD terrain); other 1× writes in a pass
  (sprites, dots) are stamped as original pixels (sub-pixel sprites: S3.1). Fixed in S2.1's shadow: page 1 is `video_page_seg(1)` (a DOS block), not A400.
  Options `--dither grid|fine`, `--hr-check` (and `F117_ENH_ARGS` for test drivers). ctests `r3d_hr_n1` (N=1: hr layer == page on the 110 golden scenes, golden
  frames unchanged with the enhancements on) and `r3d_hr_n234` (agreement 95.7/94.3/94.0 %, away from edges 99.98/99.96/99.94 %); the 24 faithful ctests pass in
  classic. With `ctest -j4` `shell` once failed on time (passes alone).
- S2.3 status: tests and tools in `port/notes/e23/`. The port option `--hr-dump DIR` (implies `--hr-check`; `F117_ENH_ARGS`) makes each `--render-test` scenario write `DIR/<id>.hrd`: the primitives the hr back end rasterised (clipped edges, walls, fill colour words, lines with brush, 1001 polygons; `hr.h`), the 3D layer of the pass windows, the page, DAC and 3D-write mask. `r3d_ref.py` (ctest `r3d_hr_ref`, N=2 and 4, grid and fine, ~27 s, cached golden references, skips without `Game/`) fills the same primitives with an exact float rule (an edge is the line of pixels within half a pixel of each row; strict = certainly covered, wide = possibly) and judges only pixels whose last painter covers them strictly: gap (not written), wrong (other colour), stray (written outside every wide coverage). Measured 2026-10-20 over 128 scenes: 9.5 k polygons, 9.6 k lines, 146 dithered fills, 8.7 M (N=2) / 35.9 M (N=4) judged layer pixels: gap 0, stray 0, wrong 0.0012 % (sky-band edges of 1001, which does not follow the Bresenham rule: worst scenario `high2` 0.14 %; the tests assert totals gap/stray <= 0.001 %, wrong <= 0.01 %, per scenario wrong <= 0.3 %, gap/stray <= 0.05 %, and >= 50 dithered fills). Stamped 1x pixels, AND/OR fills and the hz half plane are not judged. Dither: the shipped colour tables contain no two-colour (hi < FBh) or FCh word in any scene, so `r3d_golden.py` got 18 scenes (`dither_*`, `stipple_*`: targets, close ups, runway, low, a view_frame) that patch the active colour tables on both sides (`patched_table`: odd colour bytes get a second colour `lo ^ 8`, every third byte becomes a stipple); the port equals the original there too (golden now 128 scenes) and the hr layer agrees with the reference in both dither modes. `dump_scenes.py` (ctest `r3d_hr_dump` on two scenes) writes `work/e23/<scene>_classic|grid|fine|mask.png` and `contact_dither.png` / `contact_scenes.png` (U1). The faithful suite passes in `--classic` and the full ctest is 28/28 (`r3d_hr_ref`, `r3d_hr_dump` are new).
- S2.3 note (2026-10-20): in `work/e23` the own F-117 in `ext0` is light green in classic and hr alike: the golden scenes poke the
  view mode and keep the start-up palette. In real play the jet is black (user). Later reference dumps should load the mission's flight palette.
- S2.4 status: `src/enhanced/sandbox.[ch]` (choices in `DESIGN.md` 4.7). Snapshot set: `mem[]` whole, `cur_dgroup`, `dg_sp`, `timer_ticks`, the VGA model
  (vram, DAC, registers, port state), the `mgraphic.c` / `pic.c` / `r3d_object.c` statics and the driver image, the hr back end's fixed statics, each listed by
  an `ENH:` `*_sandbox_regions` function: 1 418 421 B; plus the canvas (shadow plane + four layers, 5 120 000 B at the headless 960 x 600), copied into a
  scratch set and swapped in by pointer. Hooks: `enh_frame_begin` (frame_loop_body, A_k), `enh_idle` (frame_floor, first and in its wait), no-ops / refusals in
  sandbox (`sim_snd_*`, `frame_delay`, `[262A]` wait, vga and host waits, `host_pump`, `host_opl_write`, DOS/stdio file calls), `enh_env_args` in `main`.
  Options: `--sandbox-test N` (N invisible passes per frame with a test view: other heading/position), `--sandbox-hash FILE`; `F117_VCLOCK=1` (virtual host clock,
  test aid). Measured: A_k 0.4-0.8 ms per frame, per pass 0.55-0.95 ms snapshot + 0.10-0.17 ms restore (+ ~1.9 ms render). ctest `enh_sandbox_hash` (~15 s):
  the `debrief_chain_sea` chain plain / with 3 passes per frame / `--classic`: 212 VGAME frames, per-frame hashes (DGROUP, comm, LCG, sound driver, OPL
  stream, `mem[]`, VGA, canvas) identical, final comm block and roster identical, 0 forbidden calls; no field excluded, no leak found (a deliberately broken
  restore of `mem[]` or of the canvas fails it). Full ctest 29/29. Nothing is shown, so no U1.

### Phase 3 — Visible features (resolution)
| Id | Task | Model | Size | Status |
|---|---|---|---|---|
| S3.1 | The main 3D view at N× in all view modes: routing 3D vs HUD (9ACA, A17E), sprites, dots and tracers at N× positions, keyed frames through the compositor (DESIGN 2.5, 2.6) | S | M done 2026-10-20 · U1 OK 2026-10-20 |
| S3.2 | The TV/sensor camera window and the map MFD terrain at N× (verify the viewport hook covers bf71 and 119C, ortho mode) | S | S | done 2026-10-20 · U1 OK 2026-10-20 |
| S3.3 | HUD and MFD vector graphics at N× (pitch ladder, tapes' lines, reticle, ILS, ADI, map lines and blips) in the vector layer; fonts and bitmaps stay the original pixels | S | M | done 2026-10-20 · U1 pending |
| S3.4 | Wider view: per-view canvas geometry, widened culling, B85D range and terrain cells in display passes (needs S2.4); left/right/rear as a whole picture; `--classic` = 320x200 (DESIGN 3) | O | M | |
| S3.5 | Smooth distance shading (option, launcher switch, default off): the colour bands that show distance (the sky/ground bands of 1442 and the depth shade of faces and terrain, render3d "depth shade") blended into a gradual gradient instead of steps; the hr layer keeps the band index plus the distance fraction, the compositor mixes the two neighbouring band colours from the live DAC (so fades and flashes still apply); off and `--classic` = the original bands | O | M | |
| S3.6 | True night (option, launcher switch, default off): in night missions the sky and the ground are much darker and the lit distance is shorter (terrain and objects fade into the dark sooner); done in the compositor as a transform of the night palette's sky/ground/distance-shade colours by distance, using the band index and fraction of S3.5; the HUD, MFDs, cockpit art and lamps unchanged; rendering only (detection and the sim untouched); off and `--classic` = the original night | O | M | |

Notes:
- S3.1 status: `src/enhanced/hr_obj.c` (`DESIGN.md` 2.8). Routing: the 3D polygons, sky, terrain and models were already hr (S2.2); now also into the **3D layer**: the smoke billboards (sprites 1AD6 via the queue: the original bitmap scaled to the N× size with the `sprite_scaled` DDA, centred on B85D's hr point), the explosion 1A85 (A17E, after the pass, into the last pass's layer) and the far unit / projectile dots (N×N block at the hr point); into the **vector layer** (thin, `max(1, N/2)`): the cannon tracers, the target boxes / hexagons (B3E6) and the diamond (B339), clipped by a port of the 1001 clip core. Fonts, PIC art, panels and the TV sprites stay original pixels. Mechanism: `enh_stamp_hold` / `enh_vec_hold` (the 1× footprint is recorded but not shown; a new `SRC_VECHR` shadow record shows what was under a line where no hr pixel lands), `enh_vec_put`, `HrCtx.have` (the last pass's window stays valid for A17E). Hooks (`grep -rn "ENH: S3.1" f117port/src`): `sprite`, `r3d_queue_billboard`, `r3d_draw_queued`, 9ACA (B, D, E), A17E (S2, S3), `hud_target_box`, `hud_draw_diamond`, `r3d_project_point` (invalidates the hr point on early return). Every view mode goes through the same pass; the keyed frames need nothing extra (the shadow marks the art writes). Tests: 15 new golden scenes (`view_cockpit, front, rear, left, right, ring, side, top, behind, chase, weapon, own, map` = view modes 0, 44, 41, 42, 43, 84..8C, plus `view_boxes0/1` with boxes, the diamond and the hexagon; each with 8 smoke puffs, 4+4 tracers, a near and a far projectile, an explosion; objects rotated to where each camera looks; golden total 143, original-vs-port byte for byte). `--hr-check` composes the whole frame: N=1 identity: 0 of 9 152 000 frame pixels differ in all 143 scenes (81 sprites = 111 952 px, 15 dots, 109 vector lines all exact); N=2/3/4: interior blocks agree 99.98 / 99.94 / 99.93 %, every one of 713 vector footprint pixels has an hr line pixel within one original pixel (N=4). `r3d_hr_ref` judges the sprites, dots and vector lines with its independent rule (trace types 7-9, vector layer in the .hrd): gap 0, wrong 0, stray 0 on the objects; the polygon figures are unchanged (wrong 0.0009 %). User PNGs: `python port/notes/e23/dump_scenes.py <exe> <Game> <golden work> work/e31 --views` (runs the scenes with `call pal_flight_load`, so the jet etc. use the flight palette) → `work/e31/contact_views.png`, `view_*_classic.png` / `_hr.png`. Full ctest 29/29, 0 warnings. Open: the TV camera sprites and the sensor window (S3.2), HUD vector lines (S3.3), the wide view (S3.4); the chase / own / map scenes show no smoke (those cameras sit away from the puffs), only dots and tracers; the S4a ground-target box path of A17E is exercised through `hud_target_box` calls, not through its own state.
- S3.2 status: `DESIGN.md` 2.9. The viewport hook needed no change for the small windows (judged by the reference rasteriser: 35 TV/map scenes, gap 0, wrong 0, stray 0 at N=2 and 4). Fixed/routed: the TV close view's second pass keeps the first pass's layer (`enh_pass_continue`, trace type 0 `keep`); the 1x sensor backdrop is a stamped 3D write of the first pass (`enh_tv_backdrop`); TV fire/smoke sprites at N× (`enh_tv_sprite_pending`, `hr_edge_point`); the FLIR ramp (poly_fill mode 3) recolours the layer blocks (`enh_rec_remap`, `enh_tint_note`); the map's backup page 2 round trip keeps records and layers (`enh_window_save/restore`). Hooks: `grep -rn "ENH: S3.2" f117port/src`. Tests: 21 new golden scenes (tvclose/tvfire/tvflir/tvsmoke/tvsimple_flir x3, mapfp3/6/9, mapbg3/6/9; `pal_flight_load`, `[4088]`=1; golden total 164, original-vs-port byte for byte), `r3d_hr.py` (N=1: 0 differing frame pixels in all 164; N=2/3/4 interior 99.9 %; window still 3D-shown >= 97 %, FLIR ramp, sprites), `r3d_ref.py` (S3.2 line), new ctest `r3d_hr_tvmap` (PNG dump `dump_scenes.py --tvmap` -> `work/e32/`). Full ctest 30/30, no warnings. Open: the map's route lines, symbols and the marker are still 1x vector/art pixels (S3.3); the golden `tv0..5` scenes keep the draw page != pass page state and are only partly 3D-shown; the sprite size is N x the 1x size (not recomputed from the distance).
- S3.2 fix (first U1: the TV window and the map were 1x pixels in play): root cause = the game copies the MFD windows between the pages with `blit_rect` every frame and the flip does not cover those rows, so the shown shadow plane never got the records (the poked scenes had no such blit). Fix: `enh_blit` hook in `blit_rect` (records and layers follow between pages 0/1, art pages void). Real-path test `port/notes/e32/real_path.py` (ctest `r3d_hr_realpath`: the scripted Libya chain, `/` for the TV page, `F117_VGAME_SELECT`/`F117_VGAME_SITE=0` for the home airbase, `--mfd-probe`): TV window shown from the 3D layer 0 % -> >= 95 %, map 0 % -> >= 99 %. The `tv0..5` scenes now use `[4088]` = 1. `DESIGN.md` 2.9. Full ctest 31/31.
- S3.3 status: `DESIGN.md` 2.10. Every 1x vector line of the cockpit goes through MGRAPHIC `line()` (the asm HUD, `7898`, the ILS, the map MFD, the scope, the TV crosshair), so one hook pair there routes them all: `enh_hud_begin` / `enh_hud_end_line` (`hr_obj.c`) hold the 1x footprint as `SRC_VECHR` records and draw the same line thin (`max(1, N/2)` layer pixels, Q3) in the vector layer with the 1x Bresenham on layer coordinates (so N = 1 is the original, pixel for pixel); axis-aligned lines keep their full length. `hud_tick_strip` (tape scale lines and ticks) writes the page directly: its pixels are now vector records and `enh_hud_tick` draws the ticks as thin lines and the dotted scale line as dashes. The ADI's horizon polygon (`cockpit_draw_adi`) is filled again at N x (`enh_adi_begin/end`, `enh_rec_vecfill`; N >= 2). A vector write on page 0 (the map's second copy, the route legs the step draws after the flip) now belongs to the shown plane and layer (`plane_shown`). Stay original pixels: all text, the `blit_key` bitmaps (reticle, flight-path marker, aircraft marker, lamps, panel), filled bars and boxes (threat / fuel bars, MFD backgrounds), colour 0 lines (they erase; the throttle / sink bars by day) and single points (the map's 2x2 blips and dots: an N x N block, as the S3.1 dots). Hooks: `grep -rn "ENH: S3.3" f117port/src` (`line`, `hud_tick_strip`, `poly_fill`, `cockpit_draw_adi`; test aids: `enh_hud_hr_off` for `--compose-test`). Tests: 12 new golden scenes (`hud_l0 l1 l2 l0s lvl`, `hud_adi0..2`, `hud_ils`, `hud_scope`, `hud_map3/6`; golden total 176, original-vs-port byte for byte); `--hr-check` N = 1: 0 differing frame pixels in all 176; N = 2/3/4 interior 99.98 / 99.94 / 99.92 %, footprints 4631/4631 within one pixel, `vthin` (lines thin) and `vstray` 0 (r3d_hr.py); the reference rasteriser judges 255 lines and polygons of the HUD scenes (trace types 9, 11 ADI polygon, 12 / 13 map window restore / save): gap 0, wrong 0, stray 0 (r3d_ref.py). Real game path (`port/notes/e32/real_path.py`, ctest `r3d_hr_realpath`, scripted F4, v, c, F8, F3, Alt-N; `--mfd-probe` now prints `hudv mapv allv tapel taper framel framer centre`): the HUD's vector records shown from the vector layer 99.4-99.8 % (airspeed / altitude tape, frame and the ladder / reticle centre 100 %) in both HUD colour sets, styles 0/1/2, the small view and night; map window with the route legs 82 % (the rest are blips), scope 100 %. PNGs from real frames: `work/e33/contact_hud.png`, `hud_<state>_<hud|mfd>_<classic|hr>.png`. Full ctest 31/31, no warnings. Open: the ILS is exercised by the golden scene only (it needs a flight near the landing site; its `line()` path is the one measured in the HUD); the pitch ladder's rung endpoints are 1x integers, so the rungs move in 1x steps (sub-pixel endpoints would need the ladder maths at N x); the ADI polygon is 1x vertices drawn straight; colour 0 lines and points are 1x pixels.

### Phase 4 — Frame rate
| Id | Task | Model | Size | Status |
|---|---|---|---|---|
| S4.1 | Decouple sim and display: display passes in the `frame_floor` wait at the monitor rate (vsync / `--fps N`), alpha, lerp of the own aircraft and camera; the `sim_interp_equal` ctest (DESIGN 4.1, 4.3, 4.6) | O | M | |
| S4.2 | Interpolate the objects: units, projectiles, tracers, smoke, wreck, spinners, the delayed view's pose ring; jump detection (DESIGN 4.3, 4.4) | S | M | |
| S4.3 | Input at display rate: audit that keyboard/gamepad events keep reaching the sim unchanged between steps (no averaging, no flight-model change); latency check | S | S | |

### Phase 5 — Settings and launcher
| Id | Task | Model | Size | Status |
|---|---|---|---|---|
| S5.1 | Launcher from `../F117-2/f117port/launcher/` (the faithful one: folder, options, key bindings, Game speed) plus Render scale, Frame rate, Smooth distance shading (S3.5), True night (S3.6) and Classic; built by default, `-DF117_LAUNCHER=OFF` to skip | S | M | |
| S5.2 | Settings: every Enhanced option on the command line and in the launcher; defaults from U3 | H | S | |

### Phase 6 — Release
| Id | Task | Model | Size | Status |
|---|---|---|---|---|
| S6.1 | README features, CHANGELOG, `tools/make_release.py` adapted (`f117-enhanced`, launcher), v0.1.0 zip | H | S | |

Notes:
- S1.1–S5.2 come from the faithful plan's S6.2 ("native-resolution / widescreen / 60 fps 3D view, longer view
  distance"): resolution, frame rate and the wider view are taken; view distance is parked (TODO below).
- Bugs in the engine: fix in `../F117-2` first (patch release when it matters), then port here.
- 2026-10-19 (S1.1, `DESIGN.md` section 6): the render path changes sim state (designators, tracers, RNG, spinners), so
  every wide or interpolated frame is a sandboxed display pass; that sandbox is now its own foundation S2.4 (S3.4 and
  S4.1 need it). S4.1 shrinks from L to M; S2.3 gains the reference-rasteriser test (Sonnet, M); S3.1 moves to Sonnet
  (the back end is S2.2's); S3.2 becomes S (the viewport hook already covers the TV and map passes).

## User tasks

### U1 — Visual checks (when an agent asks)
Run the build or open the PNG it names next to `--classic`, and reply in one line: `OK`, or what is
wrong and where. Agents do not look at images themselves.

### U2 — Playtests and bug reports
```
Build: <commit hash>   Options: <command line or launcher settings>
Steps: <what you did, from launch>
Classic does: <expected>
Enhanced does: <what happened; crash message if any>
Files: work\bugs\<n>.png
```
Several bugs in one message; one session then fixes them in a batch.

### U3 — Decisions (answer when convenient; defaults apply otherwise)
1. **Render scale**: default = the largest whole multiple of 320x200 that fits the window or screen (4× = 1280x800
   at 1280x960 4:3); a fixed 2×/3×/4× can be chosen.
2. **Sim step rate in Enhanced**: default = keep the faithful pacing for the sim (Game speed 7 = 10 steps a second,
   the flight model unchanged) and interpolate the display to the monitor rate. Alternative: 15 steps a second (the top of
   the original's estimator range).
3. **2D art scaling**: default = nearest-neighbour (the original pixels, sharp). A smoothing filter is not planned
   (it would change the art's look).
4. **Wider view**: settled 2026-10-19 (see Decisions). **Longer view distance**: default = parked.
5. **GitHub**: create a private `kylofon/F117-2Enhanced` when you want it; default: local only until the first release.

## Decisions
(Settled answers from U3 go here, dated.)
- 2026-10-20, DESIGN.md Q2: dither stays **grid** (the default). S2.3 found that no original colour table uses a two-colour or
  stipple fill in the golden scenes, so the choice has no visible effect in normal play; `--dither fine` stays as an option.
- 2026-10-20, user request: a "True night" launcher toggle (S3.6) that significantly darkens the night sky and ground
  and shortens the lit distance. Off by default; `--classic` never applies it; a look change only, not a sim change.
- 2026-10-20, user request: a launcher switch that blends the distance colour bands into a gradual gradient
  (S3.5). It is an option, off by default, so the original banded look stays the default; `--classic` never blends.
- 2026-10-19, DESIGN.md Q3: HUD and vector lines at N× are `max(1, N/2)` layer pixels wide (the default).
- 2026-10-19, DESIGN.md Q2 (dither) is decided after seeing it in the game: S2.2 implements both (`--dither grid`, the
  default, and `--dither fine`), and S2.3 writes the same real scenes with both as PNGs and adds a U1 row asking the user.
- 2026-10-19, DESIGN.md Q1: the left, right and rear views are scaled to **fit the window's height** (the whole
  picture, top to bottom; black bars at the sides of a wide window).
- 2026-10-19, DESIGN.md Q4: in the front cockpit view the world is also shown beside the instrument panel (default kept).
- 2026-10-19, DESIGN.md Q5 / U3 item 2: the sim runs at 10 steps a second (Game speed 7, the faithful pacing); the
  display is interpolated to the monitor rate.
- 2026-10-19, U3 item 4 (wider view): keep the original cockpit art in place and unchanged, and extend the 3D view
  to the left, right and top, so a wide or tall window shows more of the world around the cockpit (S3.4). The original
  320x200 picture stays available with `--classic`.
- 2026-10-19, wider view per view mode: the **front cockpit view** keeps the rule above (cockpit art in place, the
  world extended around it). The **left, right and rear looking-out views** (256LEFT/RIGHT/REAR with the aircraft art)
  are not extended: the whole picture, art included, is scaled up to fill the wider window.

## TODO (parked)
- [ ] Longer view distance (changes what the original shows; terrain/object tables).
- [ ] Roland MT-32 (`RSOUND.117`; Munt, user ROMs) — faithful TODO, would be done in `../F117-2` first.

## Future mod (not this repo's goal)

Ideas that change the game's content or simulation, so they belong to a later mod built on this engine, not to the
Enhanced port (whose rule is: original art and sim unchanged). Recorded 2026-10-20.

- **Weather and turbulence**: the original draws no clouds. A mod would add cloud coverage per mission and turbulence
  in and near clouds, through the flight model's existing buffet term (`1e9e`, simflow.md Part 2 section 2.4) with its
  own random stream, so the original `rand_n` sequence is not disturbed.
- **1:1 altitude without the altitude bands**: the 3D world uses a compressed altitude `alt_s = [2DF4]` (simflow.md
  Part 2 section 2.9, flight.md section 1): feet 1:1 below 8192 ft, then half slope to 16384 ft, then a quarter. Its
  vertical unit is also exaggerated against the horizontal one (32 alt_s units = 1 level unit = 15.6 m, so 1 ft draws
  as 0.49 m, x1.6). A mod would make altitude true scale (no bands, 1 ft = 0.3048 m against the horizontal unit). It
  touches every user of `alt_s`: the 3D camera and objects, unit/projectile/tracer z, the radar altitude cone, AI
  altitude targets, collision and terrain probes, landing, so it is a sim change, not a rendering option.
