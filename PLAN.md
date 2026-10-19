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
| U3 | Decisions (defaults apply if unanswered), see U3 below | S1.1 | 5 min |

## Status

- **Done:** S0.1 (repo from the faithful port).
- **Next:** S1.1 (Opus): the render and timing design. Your tasks: **User to-do** above.
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
fills the extended area in each view mode (cockpit, full-screen 3D, outside, map); (d) the `ENH:` hook list, how `--classic` stays byte-exact, and the test plan (N=1 equals classic byte for byte; golden frames at N×). | O | M | |

### Phase 2 — Foundations
| Id | Task | Model | Size | Status |
|---|---|---|---|---|
| S2.1 | Host: an output size separate from 320x200, a layered compositor (8-bit page through the DAC + a high-res 3D layer + the cockpit mask), `--render-scale N`; `--classic` = today's present path | S | M | |
| S2.2 | render3d at N×: projection, clipper (12EF incl. the 32-bit path), spans (1359), sky (1442), terrain and objects into the N× layer; N=1 byte-identical to the original (golden scenes) | O | L | |
| S2.3 | Tests: the faithful suite in `--classic`; the render3d golden scenes at N=1 through the new path; an N=2/4 smoke test (headless PNGs for the user) | H | S | |

### Phase 3 — Visible features (resolution)
| Id | Task | Model | Size | Status |
|---|---|---|---|---|
| S3.1 | The main 3D view at N× behind the cockpit art (all view modes: cockpit, full-screen, outside views) | O | M | |
| S3.2 | The TV/sensor camera window and the map MFD terrain at N× | S | M | |
| S3.3 | HUD and MFD vector graphics at N× (pitch ladder, tapes' lines, reticle, ILS, map lines and blips); fonts and bitmaps stay the original pixels, scaled | S | M | |
| S3.4 | Wider view: the 3D world extended left, right and above the original cockpit (which stays in place, unchanged); window and full-screen aspect options; `--classic` = 320x200 | O | M | |

### Phase 4 — Frame rate
| Id | Task | Model | Size | Status |
|---|---|---|---|---|
| S4.1 | Decouple sim and display: the sim at a fixed step rate (U3 decision 2), the display at the monitor rate; interpolate the camera and every drawn object between the last two sim states; `--fps N` / vsync | O | L | |
| S4.2 | Interpolate what moves on screen between steps: rotating parts (radar dishes), missiles, tracers, smoke, explosions, the HUD ladder and tapes | S | M | |
| S4.3 | Input at display rate: keyboard/joystick sampled every frame and fed to the next sim step unchanged (no change to the flight model) | S | S | |

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
- 2026-10-19, U3 item 4 (wider view): keep the original cockpit art in place and unchanged, and extend the 3D view
  to the left, right and top, so a wide or tall window shows more of the world around the cockpit (S3.4). The original
  320x200 picture stays available with `--classic`.

## TODO (parked)
- [ ] Longer view distance (changes what the original shows; terrain/object tables).
- [ ] Roland MT-32 (`RSOUND.117`; Munt, user ROMs) — faithful TODO, would be done in `../F117-2` first.
