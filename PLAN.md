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
| U3 | `DESIGN.md` Q2 (dither: original grid or fine) and Q3 (HUD line width at N×: N/2 or 1): compare `work/examples/q2_q3_examples.png` and reply A or B for each; defaults (A) apply if unanswered | S2.1 | 2 min |

## Status

- **Done:** S0.1 (repo from the faithful port), S1.1 (`DESIGN.md`, the render and timing design).
- **Next:** S2.1 (Sonnet): host canvas, layered compositor and shadow plane (`DESIGN.md` 2.1, 2.5, 3.1). Your tasks: **User to-do** above.
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
| S2.1 | Host: canvas separate from 320x200 (any aspect, `--render-scale N`, `--wide`), the compositor (3D layer + vector layer + shadow plane, live DAC/CRTC) and the shadow records in `mgraphic.c`; `--classic` = today's present path (DESIGN 2.1, 2.5, 3.1) | S | M | |
| S2.2 | render3d at N×: the hr back end (`hr_clip/hr_raster/hr_edge2d` copies) fed by hooks in projection, edges, clipper, spans, sky and 1001; rule R1; N=1 byte-identical (DESIGN 2.2–2.4) | O | L | |
| S2.3 | Tests: the faithful suite in `--classic`; golden scenes at N=1 through the hr path; reference-rasteriser comparison at N=2/4; headless PNG and mask dumps for the user (DESIGN 5.3) | S | M | |
| S2.4 | Sandboxed display pass: snapshot/restore of mem, VGA model, driver statics and layers; sound/wait suppression; `enh_frame_begin`/`enh_idle` hooks; the sandbox hash ctest (DESIGN 4.2) | O | M | |

### Phase 3 — Visible features (resolution)
| Id | Task | Model | Size | Status |
|---|---|---|---|---|
| S3.1 | The main 3D view at N× in all view modes: routing 3D vs HUD (9ACA, A17E), sprites, dots and tracers at N× positions, keyed frames through the compositor (DESIGN 2.5, 2.6) | S | M | |
| S3.2 | The TV/sensor camera window and the map MFD terrain at N× (verify the viewport hook covers bf71 and 119C, ortho mode) | S | S | |
| S3.3 | HUD and MFD vector graphics at N× (pitch ladder, tapes' lines, reticle, ILS, ADI, map lines and blips) in the vector layer; fonts and bitmaps stay the original pixels | S | M | |
| S3.4 | Wider view: per-view canvas geometry, widened culling, B85D range and terrain cells in display passes (needs S2.4); left/right/rear as a whole picture; `--classic` = 320x200 (DESIGN 3) | O | M | |

### Phase 4 — Frame rate
| Id | Task | Model | Size | Status |
|---|---|---|---|---|
| S4.1 | Decouple sim and display: display passes in the `frame_floor` wait at the monitor rate (vsync / `--fps N`), alpha, lerp of the own aircraft and camera; the `sim_interp_equal` ctest (DESIGN 4.1, 4.3, 4.6) | O | M | |
| S4.2 | Interpolate the objects: units, projectiles, tracers, smoke, wreck, spinners, the delayed view's pose ring; jump detection (DESIGN 4.3, 4.4) | S | M | |
| S4.3 | Input at display rate: audit that keyboard/gamepad events keep reaching the sim unchanged between steps (no averaging, no flight-model change); latency check | S | S | |

### Phase 5 — Settings and launcher
| Id | Task | Model | Size | Status |
|---|---|---|---|---|
| S5.1 | Launcher from `../F117-2/f117port/launcher/` (the faithful one: folder, options, key bindings, Game speed) plus Render scale, Frame rate and Classic; built by default, `-DF117_LAUNCHER=OFF` to skip | S | M | |
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
