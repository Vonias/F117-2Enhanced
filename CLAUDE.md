# F-117A Enhanced — agent rules (keep this file short: it is loaded every session)

Enhanced C/SDL3 port of MicroProse *F-117A Nighthawk Stealth Fighter 2.0* (1991), built on the faithful port
`../F117-2` at v0.1.0 (d05175f). State and subtasks: `PLAN.md`. Engine rules: `ENGINE.md`.
Goal: **higher resolution and frame rate without changing the original art** (pictures, fonts, sprites, palettes,
models stay the original's; only how they are rendered and paced changes).

Enhanced rules:
- Enhancements live in `f117port/src/enhanced/`. Every edit to engine code is marked `/* ENH: … */` and kept
  as small as a hook (`grep -rn "ENH:" f117port/src` lists them all).
- `--classic` turns every enhancement off and must behave exactly as the faithful release: the faithful ctests
  (golden frames, `sim_vgame`, `shell`, `debrief`, `sound`, chain tests) pass in `--classic`.
  Each enhancement is its own option; enhanced defaults are on.
- The simulation is unchanged unless the subtask says so: render, interpolate, add input or settings,
  do not change game rules or the original data.
- A bug in the engine itself: fix it in `../F117-2` first (its PLAN.md), then port the fix here.

Token budget rules:
- Work on ONE subtask per session (PLAN.md id, e.g. `S1.2`). Read only `PLAN.md` "Status" + that
  subtask. When done: status cell (`done YYYY-MM-DD`, `U2 pending`), `python tools/plan_site.py`,
  commit. Longer notes go in the "Notes:" list under the table, never between rows.
  When you need something from the user, add a row to `## User to-do` at the top of PLAN.md.
- Never read large files whole: `Grep` first, then `Read` with offset/limit.
- Specs, RE notes, disassembly and format docs stay in `../F117-2` (`port/spec/*.md`, `port/RE_GUIDE.md`,
  `FORMATS.md`, `tools/x86dis.py`): search them, never dump them, never re-derive what they say.
  `f117port/PORTING.md` (copied) is the engine's own guide.
- Do not view images or screenshots unless asked. Visual checks are the user's job (U-tasks): write
  the PNG / run the build, then ask the user what they see.
- Wide searches → the `scout` agent (Haiku).
- Windows: build with `C:\msys64\mingw64\bin` first on PATH. PowerShell 5.1 corrupts UTF-8 files:
  edit with the Edit/Write tools or Python only.
- Game data is never committed (`Game/` and `work/unpacked/` are junctions into `../F117-2`); never write into `Game/`.
- No test HTML pages; implement, then tell the user what to test.
