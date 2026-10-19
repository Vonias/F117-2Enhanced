# F-117A Enhanced — render and timing design (S1.1)

Binding inputs: PLAN.md Goal and Decisions 2026-10-19 (front cockpit: art in place, world extended around it;
left/right/rear views: whole picture scaled; full-screen 3D and outside views: extended), U3 defaults (render scale =
largest whole multiple, sim at the faithful 10 steps/s, nearest-neighbour art). Spec references are to
`../F117-2/port/spec/` (`render3d.md` = R3D, `simflow.md` = SF, `flight.md` = FL); port files are under `f117port/src/`.

## 0 Decisions in one table

| Topic | Decision |
|---|---|
| N× layer | A second, passive back end (`enhanced/hr_*.c`) fed at the points where render3d produces screen geometry. The faithful 1× page drawing keeps running unchanged; the hr back end rasterises the same primitives at N× into an 8-bit **3D layer** (DAC indices). |
| Precision | N enters inside the innermost rounding of each projection formula, so the result at N=1 is the original expression (rule R1, section 2.2). Screen geometry in the hr path is int32 (int64 intermediates). |
| Compositing | A 320x200 **shadow plane** records, per screen pixel, who wrote it last (3D / vector) and the value written. At present time a pixel whose page-0 value still equals its shadow value shows the N× layer block; any other pixel is original art, scaled N×N nearest. No hand-made masks. |
| Vectors at N× | HUD/MFD/map lines and 3D-scene dots/tracers are drawn thin into a **vector layer**; fonts, PIC/PAN art, lamps and sprites stay original pixels (sprites at N× positions, N×N blocks). |
| Wider view | A canvas of any aspect; the 320x200 frame at N× sits bottom centre; the projection keeps its centre and scale (256N px per unit), so the field of view grows with the canvas. Culling is widened only in display passes. |
| Frame rate | The faithful frame (render + flip + input + `39c4` step) runs unchanged at the step rate. Between steps, **sandboxed display passes** re-render a saved pre-render memory image with interpolated fields, compose, present, and restore the live state. The sim never sees a display pass. |
| Snapshot | Whole machine image (`mem[]`, VGA model, driver statics, hr layers + shadow) taken once per step; 11 scalar fields + 4 object tables are interpolated (section 4.3). |
| `--classic` | Every hook is behind an option test; `--classic` clears all options, so no hook body runs and the host keeps today's 320x200 present path. |

## 1 Facts the design rests on (measured from code and data)

### 1.1 One faithful frame (SF Part 3 section 0, `sim/sim_main.c frame_loop_body`)
`view_frame` (2F82, `sim/sim_view.c`) → `r3d_scene_draw` (03CE) into page 1: `r3d_begin_scene` (DAE8: `r3d_set_viewport` DB71,
camera DC64, wait `[262A]`, sky 1442), terrain 04DA, world objects 9ACA, `r3d_end_scene` DB3E (queue DF08, spinners), `[7D50]`,
overlay pass A17E (`obj_overlay_frame`, which also renders the TV window `mfd_view_render` bf71 as a nested 3D pass) →
cockpit pastes (SF 2.12) → `hud_overlay_frame` 7898, `hud_draw_far`, `hud_side_strips`, `cockpit_draw_adi` → `frame_floor` →
`flip(160*[2E34])` copies rows 0..h-1 of page 1 to A000 → `sim_input_flight` 1e9e → `sim_frame_step` 39c4.
The instrument panel rows live on A000 and are refreshed by explicit page 1 → page 0 copies (MFD windows, ADI).

### 1.2 3D windows and art per view (SF 1, 2.9–2.12; key colour 0 = `blit_key_body`, `video/mgraphic.c`)

| View `[C07A]` | 3D viewport (DB71, frame px) | Centre `[85D2],[85D4]` | Art over the 3D after the scene | Key-0 pixels in that art (measured, `tools/picdec.py`) |
|---|---|---|---|---|
| 0 front cockpit | x 0..319, y 0..90 (`[2DFC]`=0) or 0..106 (`[2DFC]`=1, initial) | 159, 52 (60 if `[294B]`) | h=107 only: rows 91..106 x 32..287 opaque strip (pit rows 91..106); x 0..31 and 288..319 keyed from pit rows 74..89 | strip 0/4096; each corner 272/512 |
| 0x41 rear | 0..319, 0..156 | 159, 100 | centre x 91..228 opaque; x 0..90, 229..319 keyed (256REAR) | centre 0; each side 9793/14287 |
| 0x42 / 0x43 left/right | 0..319, 0..160 | 159, 100 | full 320x161 keyed (256LEFT / 256RIGHT) | 37995/51520 each (mirror images) |
| 0x44 front clean | 0..319, 0..199 | 159, 100 | grey nose triangle (160,185)(230,199)(90,199), colour 5 | — |
| 0x84..0x8C outside | 0..319, 0..199 | 159, 100 | none | — |
| TV (bf71, in A17E) | x 169..245, y 110..169 on page `[4088]`, blitted to page 0 at the same place | window centre | labels (font) | — |
| map MFD (119C, `mfd_map_redraw` 8387) | left MFD x 74..150, y 110..169, ortho mode `[7D5E]` | — | map symbols, text | — |

Every path from a 3D pixel to the screen is a copy to the **same coordinates** (flip, the 77x60 MFD `blit_rect`s, keyed blits).
This is what lets the shadow plane (2.5) work per screen coordinate. There is no full-screen map view in VGAME's view list;
the map is an MFD (panel, not extended); the shell's map screens are 2D and pillarboxed (3.4).

### 1.3 The render is not pure (why display passes must be sandboxed)
The render path changes sim state: 9ACA runs the target designators (`[3D66]`, `[3D64]`, the `[B28E]` cooldown, R3D Part 2);
A17E moves and hit-tests cannon tracers, kills units, damages the player, starts explosions, posts messages (SF Part 9 S2);
the TV fire puffs C73C draw from the sim LCG `DS:926E` (R3D C73C); DB3E advances the spinners `[C04C]/[C04E]`; DAE8 sets the
collision flag `[C686]` → `[9F66]`; the graphics driver has its own state (`mgraphic.c` statics incl. `rng`, `shake`).
So: (1) the canonical frame must run exactly once per step with faithful viewport and culling; (2) anything rendered more
often, or with a widened view, runs on a saved image whose every change is thrown away.

## 2 (a) High-resolution rendering

### 2.1 Layers
* **Canvas** (layer pixels) `Lw x Lh` (section 3.1); frame origin `(fx, fy)`, scale N (integer).
* **3D layer**: `u8[Lw*Lh]`, DAC indices; written only by the hr back end (`enhanced/hr_raster.c`).
* **Vector layer**: `u8[Lw*Lh]` + coverage bit (0 = transparent); written by the vector emitter (`enhanced/hr_vec.c`).
* **Shadow plane**: per frame pixel `{src: none/3D/vec, val, under, under3d}` (4 B x 64000), see 2.5.
* Colours stay 8-bit indices end to end; the DAC is applied once, in the compositor, from the **live** VGA state, so
  palette fades, the DAC[13] flicker and day/night recolours (`explosion_fx_tick`, `cockpit_daynight_recolor`) work as today.

### 2.2 The hr back end: dual emission, and where N enters
The faithful functions run as now and write the 1× page. At each geometry point an `ENH:` hook passes the same primitive,
recomputed at N×, to the hr back end. `hr_clip.c`, `hr_raster.c`, `hr_edge2d.c` are **copies** of `r3d_clip.c`, `r3d_raster.c`,
`edge2d.c` with: coordinates int32, records/tables in host memory (`HrCtx`: window, centre, L/R tables of `Lh` rows,
edge records, target layer), and N plus the frame offset folded in. The faithful files are not edited beyond the hooks.

**Rule R1.** Put N inside the innermost rounding: `trunc(a/b)` becomes `trunc(a*N/b)`, `v >> k` becomes `(v*N) >> k`,
`mul15r(a,b)` becomes `(a*b*N + 0x4000) >> 15`; screen offsets become `fx + N*centre`. Any 16-bit wrap that can happen at
1× is applied at the 1× magnitude first, then scaled. At N=1, fx=fy=0 every formula is the original one.

| Geometry point (spec) | Port function | hr version |
|---|---|---|
| 11EB:0906 perspective | `r3d_project` (`r3d_object.c`) | `q = idiv((v*N)>>8, zh)`; small-z branch `idiv((v>>1)*N, d)`, INT 0 fallback `idiv(((v>>1)*N)>>8, d)*256`; "behind" kept as a separate flag, not the `8000h` marker; hr vertex array beside `D688` |
| 11EB:0906 ortho (map, `[7D5E]`) | same | `(coord_hi*N) << c` / `(coord_hi*N) >> c` with the original round-half-up |
| 11EB:0B3F edges + near point | `r3d_make_edge` | record in `HrCtx`; centre `fx + N*[85D2]`, `fy + N*[85D4]`; near point `((7D42*N) >> 8)*2` written as `(([7D42]>>8)*2)` at N=1 |
| 12EF clipper (fast16, path32 bisection, walls) | `r3d_clip_edge`, `r3d_poly_add_edge`, `r3d_poly_finish` | same algorithm, window `[N*xmin + fx, N*(xmax+1)-1 + fx]` etc.; the fast16/path32 choice uses the same "fits s16" test on N× values; sums/products int64 |
| 1359 edge DDA, walls, `fill_M` (dither/AND/OR/stipple), `line_M` | `r3d_edge_dda`, `r3d_wall_fill`, `r3d_fill_M`, `r3d_line_M` | unchanged integer DDA on N× coordinates; colour from `[85D6]` after `r3d_setcolor_M` (shade ramp included); dither phase see 2.4 |
| 1442 sky/ground bands | `band_points`, `bands` (`r3d_sky.c`) | boundary points by R1 (`mul15r` with N), `w2,h2` → layer centre; quads to `hr_edge2d` poly fill |
| 1001 `hz_draw` (detail < 3), `poly_fill_far` | `edge2d.c` | same; half-length uses the hr viewport's larger half-width |
| B85D world→screen (dots, billboards, tracers) | `r3d_project_point` (`sim/sim_objovl.c`) | also leaves hr `sx, sy` (layer px, int32) in an enh global for the emitters of the same object |
| 2D sprites in the scene (DF08 sprite records, 1AD6/1B1B/1A85) | `sprite_scaled` (`mgraphic.c`) | sprite pixels as NxN blocks at the hr position of the record (original pixels, sub-pixel placement) |

The hr back end never writes `mem[]`, the VGA model or driver statics (it is invisible to the sim and to the 1× page).

### 2.3 Overflow risks (16-bit fixed point)
| Place | 1× range | hr handling |
|---|---|---|
| 0906 `n = v>>8`, out up to 2^23 (INT 0 path) | s32 | int64 for `n*N`; output int32 (≤ 2^23·N, N ≤ 64) |
| B85D `lsl32(X,8)` wraps for \|X\| ≥ 2^23 (quirk) | wrap mod 2^32 | wrap at 1× (`(s32)((u32)X<<8)`), then `*N` in int64 |
| 12EF fast16 intercept `(xb-bx)*dy` | s16·s16 | int64 |
| 12EF path32 midpoint `(p0+p1+1)>>1` (33rd bit lost) | s32 | int64 sum; N× coordinates stay < 2^30 |
| 1359 L/R tables, sentinels 7FFFh / 8001h, rows ≤ 480 | s16 | int32 tables of `Lh` rows, sentinels INT32_MAX / INT32_MIN+1 |
| 1001 tables (YMAX ≤ 219), INT 0 saturation for huge band quads | s16 | `Lh` rows; saturation kept (copy), int32 values |
| E8B5 cull adds wrap (class 7, \|Ch\| > 13016) | s16 | widening (3.3) scales the **wrapped** 1× tolerance, so a wrap-culled object stays culled |

### 2.4 Colours, dither, line width
Indices are copied as is (shade ramp of `r3d_setcolor_M`, AND `FEh`/OR `FDh` modes read-modify-write the 3D layer).
The checkerboard dither (`fill_M`, `((x^r)&1)`) is evaluated on **original-pixel phase** `((X-fx)/N ^ (Y-fy)/N) & 1`,
so dithered faces keep the original look at any N (option `--dither fine` for a per-layer-pixel checkerboard; Q2).
Lines (`line_M`, HUD lines) are drawn `max(1, N/2)` layer pixels wide (Q3).

### 2.5 Compositing: the shadow plane
Emitters record into the shadow at screen coordinates `(off % 320, off / 320)` of every page-0/page-1 write they make
(`draw_off` / viewport offset included); page-2 (picture page) writes are ignored.
* 3D emitter (1359 spans/lines, 1001 polys and sprites inside a 3D pass): `src = 3D, val = byte written`.
* Vector emitter (`line()`/`put_pixel` outside a 3D pass): if the old record is stale (page value ≠ `val`) clear that
  block of the vector layer first; then `under = previous page value`, `under3d = (old src == 3D && fresh)`, `src = vec, val`.
* `r3d_set_viewport` clears the vector layer and shadow inside the new pass's window.

Compose (per displayed pixel, honouring CRTC start/pan as `vga.c compose` does, so the explosion shake moves the layers too):

| Shadow of the page-0 pixel | Shown in its NxN block |
|---|---|
| `3D` and page value == `val` | 3D layer block |
| `vec` and page value == `val` | vector-layer pixels over (`under3d` ? 3D layer block : `under` scaled) |
| anything else | the page-0 value, NxN (original art, fonts, panel, keyed frames) |
| canvas outside the frame | 3D layer (extended views) or black |

This reproduces section 1.2 exactly without tables: keyed frame pixels (key 0) are never written, so the 3D shows through;
opaque strips overwrite and win. Known artefact: an overlay pixel of the very same index as the 3D pixel under it is shown
as hi-res 3D (invisible in practice). Debug: `--enh-mask-dump DIR` writes the classification as a PNG (U1).

### 2.6 What is drawn at N×
| N× (thin / sub-pixel) | Original pixels (NxN nearest) |
|---|---|
| all 3D polygons, 3D lines, sky/ground, terrain, objects (main view, TV window, map MFD terrain) | fonts and all text (HUD digits, labels, messages) |
| HUD pitch ladder, horizon, heading/speed/altitude tape lines and ticks (`hud_tick_strip`), reticle, flight-path marker, target boxes, ILS bars (`line()` calls of `sim_hudasm.c`, `sim_hud.c hud_line_*`, `sim_overlay.c`) | PIC art (cockpit, panel, frames), PAN animations, lamps (`rect_recolor`) |
| ADI horizon lines (`cockpit_draw_adi`), map MFD lines and blips (`mfd_map_redraw`), TV window crosshair | sprites: explosions, smoke, billboards, ordnance MFD sprite (placed at N× positions) |
| far unit/projectile dots (9ACA 873F), cannon tracers (A17E S2) | `hud_fill_rect`, MFD fills, shell/debrief screens |

HUD line endpoints are integers computed at 1× (centre of the 1× pixel → layer `fx + N*x + N/2`), so they are thin but move in
1× steps; giving ladder/tapes sub-pixel endpoints is an optional later step inside S3.3 (only where the port's C computes them).

## 3 (b) The wider view

### 3.1 Canvas geometry (window of any aspect)
Output `Wo x Ho`, pixel aspect `a` (1.2 for the 4:3 option, 1 for square pixels; the host stretches the canvas by `a`).
`Lw = Wo`, `Lh = floor(Ho / a)`; `N = floor(min(Lw/320, Lh/200))` (U3-1; or the fixed `--render-scale`), at least 1.
Frame: `fx = (Lw - 320N) / 2`, `fy = Lh - 200N` (bottom centre). Frame coordinate u (continuous) → layer `fx + N*u`.
Extensions in original pixels: `El = fx/N`, `Er = (Lw - fx - 320N)/N`, `Et = fy/N`. With `--wide off` the canvas is the frame
(`Lw = 320N`, `Lh = 200N`, host letterbox as today).

### 3.2 Per view
| View | hr 3D viewport (layer px) | Projection centre | Extended area shows |
|---|---|---|---|
| 0 front cockpit | x 0..Lw-1, y 0..Lh-1 (frame panel rows covered by art via compose) | `fx+159N, fy+52N` (60 with `[294B]`) | the world left, right, above and beside the panel (Q4) |
| 0x44, 0x84..0x8C (full-screen 3D, outside) | whole canvas | `fx+159N, fy+100N` | the world |
| 0x41..0x43 rear/left/right | frame only | frame-mapped | black: the whole 320x200 picture (3D at N×, art NxN) as large as fits, centred (Q1) |
| TV, map MFD | their frame rect at N× | frame-mapped | not extended (panel) |
| SETUP/shell/START/END/debrief, VGAME menus | — | — | 320x200 (or 640x400 text) scaled by the largest whole N, centred, black bars |

### 3.3 Field of view, culling and clip edges (display passes only; canonical pass stays faithful)
The scale stays 256N px per unit tangent in x and 192N in y (128N/96N with `[294B]`), so angular size is unchanged and the
half-angles become `atan((159+El)/256)` left, `atan((160+Er)/256)` right, `atan((52+Et)/192)` up (front cockpit).
Let `kx = (160 + max(El,Er)) / 160`, `ky = (up0 + max(Et, down extension)) / up0` (Q8, ≥ 1), up0 from `[0D20]`.
| Test | Widening |
|---|---|
| E8B5 `r3d_cull_core` x/y radius (tables 48D6..4946) | `sx *= kx`, `by *= ky` (int32, on the wrapped 1× values) |
| 11EB:02B6 `r3d_frustum_test` (45° pyramid) | per axis, `t *= max(1, T)` with T = tangent of the extended half-angle (x: `0.625·kx`, y: `(up0·ky)/192`) |
| B85D `r3d_project_point` (90° x-field, rows 0..0x60/0xC7, columns 0..319) | x-field `|X| <= -Z·max(1, 0.625·kx)`, accept `sx` in `[-El, 319+Er]`, `sy` in `[-Et, lim]`; the `0xFFFF` sentinel: an extended `sx == -1` becomes -2; applies only while the route is "3D" (9ACA blocks D/E/F/B, A17E S2/S3), not to HUD symbology |
| 04DA terrain cells (octant tables `DS:034C`, 9 or 15 cells) | when `kx > 1` or `ky > 1`: an Enhanced table of the forward half of the 5x5 block per octant, far → near, camera cell last (generated, `enhanced/wide_cells.h`); the down-look table `04CC/04DE` becomes the full 5x5 |
| far limit `[48B6]` | unchanged (view distance parked) |
The hr clip window is the hr viewport; the 1× page still clips to the original window, so the frame content equals the
original's except where newly drawn side cells/objects overlap into it (more correct terrain at the frame edge).

### 3.4 HUD anchor
The HUD, MFDs, panel and messages stay in frame coordinates (1× drawing), so they sit with the cockpit at bottom centre.
The HUD symbology lines up with the world because the projection centre and scale inside the frame are the original ones.

## 4 (c) Frame rate

### 4.1 Timeline
```
frame k (faithful, unchanged order):
  enh_frame_begin()            snapshot A_k = pre-render image; fields F_k = read from A_k (F_{k-1} kept)
  view_frame ... cockpit_draw_adi        canonical render of S_k (1x page + passive hr emission)
  frame_floor()                waits until frame_ticks since the last flip; its wait loop calls
                               enh_display_frame(alpha) whenever a refresh is due (vsync / --fps)
  flip; [262A]=1; sim_input_flight; sim_frame_step  -> S_{k+1}
```
`alpha = clamp((now - t_last_flip) / (frame_ticks / 70.09 Hz), 0, 1)`; a display pass at alpha shows
`lerp(F_{k-1}, F_k)` on image A_k, so alpha = 1 equals the canonical frame k, which is flipped next. The displayed camera
is never behind the faithful display (which still shows S_{k-1} during that wait). `--interp off`: one display pass with
alpha = 1 per step (needed only for the wider view); N× without wide or interp needs no display pass at all.

### 4.2 The display pass (`enhanced/sandbox.c`)
1. Save live: `mem[]` (1.06 MB), VGA model (vram 256 KB, DAC, CRTC, attr, sequencer), `mgraphic.c` statics
   (`draw_seg`, `draw_off`, `colour`, `rng`, `shake`, `flip_last`, page table), `r3d_object.c bsp_out`, `pic.c rle_out`,
   hr layers + shadow (only the rects that are not fully redrawn: MFDs, TV).
2. Load A_k; write the interpolated fields (4.3); rebuild derived state the step would have rebuilt (`2D94` from the
   lerped angles via `sim_matrix_from_angles`).
3. `enh.sandbox = 1`: `sim_snd_cmd`/`sim_snd_noise` do nothing, `frame_delay` and the `[262A]` wait return at once,
   `host_pump` is never called, `mission_end` and DOS writes assert in debug builds.
4. Run `view_frame`, `hud_overlay_frame`, (view 0) `hud_draw_far`, `hud_side_strips`, `cockpit_draw_adi`, `flip`,
   with the wide culling (3.3) on; compose (2.5) with the **live** DAC/CRTC into the host frame.
5. Restore live; `enh.sandbox = 0`; host presents. A debug ctest hashes everything in step 1 before and after (must match).
Cost: about 2.6 MB of copies per display frame (well under 1 ms) plus one render.

### 4.3 Interpolated fields (DGROUP offsets; SF 2.1, R3D Part 2 "Structures seen", FL 1.1)
| Field | Offsets | Lerp |
|---|---|---|
| own x, y (world, s32) | BA10:BA12, C02A:C02C | linear |
| own level units | C0A4, C0B2 | linear |
| altitude | 2DF4 | linear |
| heading, pitch, roll | 2DEE, 2DF0, 2DF2 | shortest 16-bit arc |
| eject camera altitude (view 0x8C) | DECA | linear |
| spinners (radar dishes, var 15/16) | C04C, C04E | modular (+0x1B00 / +0x1800 per step) |
| falling wreck | C0A6, C0B8, C602 | linear while C602 > 0 in both |
| units, `[DED0]` x 36 B at C140 | +0 x, +2 y, +4 z, +6 x32, +A y32, +E hdg, +10 pitch, +12 roll | if active (+16 bit 1) in both and type (+14) equal |
| projectiles, 12 x 28 B at 3C0C | +0 x, +2 y, +4 z, +8/+A/+C angles | if active (+E) in both, type (+10) equal |
| cannon tracers, (`[B16C]`+4) x 12 B at E44E | +0 x, +2 y, +4 z | if x ≠ 0 in both |
| smoke puffs, 16 x 8 B at 39DA | +4 z | if x (+0) equal and ≠ 0 |
| delayed view 0x84 | pose ring B056 | hook: lerp between the slot read and the next newer slot |
No lerp across a jump (position delta > 2 cells, angle delta > 0x4000, a view-mode change, a slot change): use F_k.
Camera variables built by `view_frame` (E44C, C07C, B770, B288, B752, B75C, B156, DE98…) are recomputed from these.
Everything else (sim state, explosion frame `[9D0C]`, fire puffs, messages, damage) is the discrete A_k value.

### 4.4 HUD/MFDs, effects between steps
The HUD is re-drawn in every display pass from the lerped attitude/position: ladder, horizon, heading tape, ADI, target
boxes and the velocity vector move smoothly (in 1× steps, 2.6); digits show values computed from the lerped fields or A_k.
MFD pages redraw only when the sim changes them, so they update at the step rate, as today. Rotating parts, missiles,
tracers, smoke and the wreck follow 4.3; explosion and puff animation frames stay per step (they are art frames).

### 4.5 Input and sound
SDL events are taken in `host_pump` (already called in the `frame_floor` wait) and queued into the keyboard / port 201h
models as today; the sim reads them in `sim_input_flight` once per step, so the flight model is unchanged (S4.3 audits the
gamepad path and adds no averaging). Ticks and the ISR body (`vgame_tick`: sound poll, 7th-tick skip, `[3660]` estimator
inputs) run only from `host_pump` in real time, never in a display pass: sound timing is unchanged.

### 4.6 Must not change (sim bit-identical at the same step rate)
* The canonical frame's code path and order (view_frame … step), including `frame_delay([43B8])`, the `[262A]` wait,
  faithful viewport/culling/terrain cells, and its sound commands.
* Tick delivery: one `vgame_tick` per retrace from `host_pump`; `frame_floor`'s pacing (`last`, `frame_ticks`).
* No display pass leaves any byte changed in `mem[]`, VGA model or engine statics; the hr back end never writes them.
* Input bytes reach the keyboard model in the same order; the RNG `926E` is touched only by canonical code.

## 5 (d) Hooks, `--classic`, tests

### 5.1 `ENH:` hooks (each a guarded call of 1–3 lines; bodies live in `src/enhanced/`)
| Function | File | Hook | Subtask |
|---|---|---|---|
| `main` option loop | `main.c` | `enh_parse_arg` (exists) | S0.1 |
| `host_set_frame_source`, `present`, `HOST_FRAME_MAX_*` | `host.c`, `host.h` | canvas size, `a` stretch, Enhanced frame source | S2.1 |
| `compose` | `platform/vga.c` | mode 13h in VGAME → `enh_compose` (layers + shadow + live DAC/CRTC) | S2.1 |
| `line`, `put_pixel`, `sprite_scaled`, `poly_fill` | `video/mgraphic.c` | shadow record + vector/sprite emit (route aware) | S2.1 (shadow), S3.1/S3.3 (emit) |
| `r3d_set_viewport` | `render3d/r3d_scene.c` | hr viewport/centre for this pass; clear rect | S2.2 |
| `r3d_begin_scene`, `r3d_end_scene` | `render3d/r3d_scene.c` | route push/pop; skip `[262A]` wait in sandbox | S2.2, S2.4 |
| `r3d_project`, `r3d_make_edge` | `render3d/r3d_object.c` | hr vertex, hr edge record + clip | S2.2 |
| `r3d_poly_add_edge`, `r3d_poly_finish`, `r3d_line_emit` | `render3d/r3d_clip.c` | mirror into the hr back end | S2.2 |
| `band_points` / `bands` | `render3d/r3d_sky.c` | hr band quads | S2.2 |
| `hz_draw`, `poly_fill_far` | `render3d/edge2d.c` | hr polygon | S2.2 |
| `r3d_project_point` | `sim/sim_objovl.c` | hr sx/sy; extended range when wide and route 3D | S3.1, S3.4 |
| `r3d_draw_world_objects`, `obj_overlay_frame` | `render3d/r3d_world.c`, `sim/sim_objovl.c` | route markers (3D vs HUD blocks) | S3.1 |
| `mfd_view_render`, `map_render_terrain` | `sim/sim_mfdview.c`, `render3d/r3d_terrain.c` | none expected (DB71 hook covers them); verify | S3.2 |
| `r3d_cull_core`, `r3d_frustum_test` | `r3d_scene.c`, `r3d_object.c` | widen by kx/ky in wide display passes | S3.4 |
| `r3d_draw_terrain` | `render3d/r3d_terrain.c` | wide cell table in wide display passes | S3.4 |
| `frame_loop_body` | `sim/sim_main.c` | `enh_frame_begin()` before `view_frame` | S2.4 |
| `frame_floor` wait loop, `frame_delay` | `sim/sim_main.c` | `enh_idle()` → display pass; return at once in sandbox | S2.4, S4.1 |
| `sim_snd_cmd`, `sim_snd_noise` | `sim/sim_stubs.c` | no-op in sandbox | S2.4 |
| `view_frame` case 0x84 | `sim/sim_view.c` | lerp the ring pose in sandbox | S4.2 |

### 5.2 `--classic`
`enh_parse_arg("--classic")` clears every option; each hook is `if (enh.<option>) …` (or `enh_active()`), so with classic
no hook body runs, `vga.c compose` keeps the 320x200 path and `host.c` keeps its sizes. The ctest commands copied from the
faithful repo run with `--classic` added (CMake list `F117_CLASSIC_TESTS`); 110 golden scenes, `sim_vgame` (1497 scenarios),
shell, debrief, sound, chain must stay green. Hook bodies are unreachable in classic, so the faithful suite also checks that
no hook leaks into classic.

### 5.3 Test plan
| Test | What | Subtask |
|---|---|---|
| `r3d_hr_n1` | every render3d golden scene through the hr back end at N=1, fx=fy=0: 3D layer == the 1× page inside the viewport, byte for byte | S2.2/S2.3 |
| `r3d_hr_ref` | N=2, 4: capture the hr polygons (`r3d_trace`), fill them with an independent pixel-centre reference filler; differences only within 1 layer px of an edge, summary count per scene | S2.3 |
| `enh_sandbox_hash` | 200 `sim_vgame` scenarios: hash of mem/VGA/statics before and after k display passes per step = equal | S2.4 |
| `sim_interp_equal` | the same scenarios with N=4, wide, interp (3 display passes per step, alpha .25/.5/.75) vs `--classic`: DGROUP after every step and the comm block at the end identical | S4.1 |
| headless dumps | `--headless --enh-dump DIR` + `F117_KEYS` scripts: front cockpit (h 91/107), rear, left, right, 0x44, chase, TV, map; N=2/4; 4:3, 16:9, 21:9; plus mask PNGs → U1 | S2.3, S3.x |

## 6 (e) Risks, open questions, plan changes

Risks: (1) sandbox completeness: an engine static or host-side state missed by 4.2 would make display passes leak; the
hash test and a debug "no host_pump/no DOS write in sandbox" assert catch it. (2) Re-rendering per display frame costs a full
1× + N× render; a view change makes each display pass of that one step reload the PIC from disk (acceptable; cache later).
(3) Wide passes may draw side scenery overlapping the frame that the original never drew (accepted, 3.3). (4) The B85D range
change must be limited to the 3D route; some HUD consumer might use `[49E0]` as an index (S3.4 lists all readers: 8C10 903F
90B9 99A7 9ACA A17E B339 B77A). (5) Interpolation adds no lag over the faithful display, but input-to-picture latency stays one
step (100 ms at 10 steps/s); 15 steps/s (U3-2 alternative) halves the step error.

Questions (defaults apply if unanswered):
* Q1 (decided 2026-10-19: fit the window's height) left/right/rear views: default **fit** (the whole picture, black side bars); alternative **cover** (fill the window, crop top/bottom).
* Q2 dither: default original-pixel checkerboard; alternative fine (per layer pixel, smoother tone).
* Q3 line width at N×: default `max(1, N/2)` layer pixels; alternative always 1.
* Q4 (decided 2026-10-19: the world beside the panel) front cockpit: default the world also beside the instrument panel; alternative black side strips below the window line.
* Q5 (decided 2026-10-19: 10/s) (U3-2) sim step rate: default 10/s (Game speed 7); alternative 15/s.

Plan changes (applied to PLAN.md, ids kept): the wider view needs the sandboxed display pass before Phase 4, so the sandbox
becomes its own foundation **S2.4 (O, M)**; S4.1 shrinks to the loop, alpha and the own-aircraft/camera lerp (**O, M**, was L);
S2.3 gains the reference-rasteriser test (**S, M**, was H, S); S3.1 is routing + sprites/dots/tracers + all view modes on a
finished back end (**S, M**, was O); S3.2 is mostly verification (**S, S**); S3.4 depends on S2.4.
