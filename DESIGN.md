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

### 2.7 As built (S2.2): choices where 2.2–2.5 were silent
* Files: `enhanced/hr.h`, `hr_pass.c` (pass context, B85D point, `--hr-check`), `hr_clip.c` (12EF + 0906 + 0B3F), `hr_raster.c` (1359),
  `hr_edge2d.c` (1001 + 1442). Hooks (`grep -rn "ENH:" src/render3d src/sim/sim_objovl.c`): `r3d_set_viewport` / `r3d_end_scene` (pass, route),
  `r3d_project`, `r3d_build_edges` (after each `r3d_make_edge`, whose near-plane point 7D42 the hr edge reads), `r3d_poly_add_edge`,
  `r3d_poly_finish`, `r3d_line_M` (+ shadow records in `line_M` / `fill_M`), `r3d_draw_sky_ground` (both exits), `r3d_project_point`.
* R1 at the 16-bit edges: 0906's INT 0 test is made on the 1× quotient (its fallback from the N× numerator); the fast16 / path32 choice and
  every "fits s16" / overflow test of 12EF and 1001 use the 1× magnitude (`|v| <= 32767 N`), so the hr clip takes the 1× path; the 1001
  intercept saturates to `N * 7F00h`. A band / horizon point is computed three ways (the original s16 code, exact at 1×, R1 at N×): if
  the 1× value wrapped, the point is `N *` the wrapped value, else the N× value. `half_q` and the other path quirks are kept literally.
* State: the 1359 row tables (gap rows) and the 12EF assembler state persist between polygons in the original, so each pass starts from
  the faithful ones, scaled (row r → rows `fy + N r ..`, L → `fx + N L`, R → `fx + N R + N - 1`); at N = 1 a copy.
* Ortho (map): `((x N) >> 16) >> c` with the round bit, which equals R1 one-stage; slot 37's skip of a one-pixel span at 13Fh uses
  `N 320 - 1`. The hz horizon line itself is drawn 1 px (the fills cover it); 3D lines use the `max(1, N/2)` brush at offset `(N - w) / 2`.
* Shadow: page 1 is MGRAPHIC's `video_page_seg(1)` (a DOS block in the game, not A400; S2.1 assumed A400). 1359 writes record `3D` without
  touching the layer (the hr polygon follows); any other 1× write inside a 3D pass (sprites, dots, the 1× backdrop) **stamps** its N×N block
  with the 1× value (bit 9), so it shows as the original pixel until an hr write replaces it. A write to the other page during a pass (the
  TV camera's backdrop lands on MGRAPHIC's current page) does not stamp; the hr backdrop is drawn only when that page is the pass page.
* Not yet (later subtasks): sprites at sub-pixel positions (stamped at their 1× place; S3.1 uses `enh_hr_pt` of B85D), the wide view (S3.4). (The TV camera's
  second `begin_scene` in the same window cleared the first pass's layer: fixed in S3.2, 2.9.)
* Test `--hr-check` (`port/notes/e22/r3d_hr.py`, ctests `r3d_hr_n1`, `r3d_hr_n234`): the 110 golden scenes with the enhancements on stay
  byte for byte; N = 1: layer == page for all 2.51 M checked 3D pixels (99.1 % drawn by the hr back end, the rest stamped); N = 2 / 3 / 4:
  95.7 / 94.3 / 94.0 % of layer pixels agree with their 1× pixel, 99.98 / 99.96 / 99.94 % away from edges (tolerance 90 % and 99.5 %).

### 2.8 As built (S3.1): the objects on top of the scene, and the views
* File `enhanced/hr_obj.c`. Every object keeps its faithful 1x drawing (page, shadow records); a hook before it asks for a hold and a hook after it draws
  the same object from B85D's hr point `enh_hr_pt` (layer px, sub-pixel; `size` = the sprite size recomputed at N x, rule R1):
  | Object (original) | Hook | N x look |
  |---|---|---|
  | smoke billboards (9ACA B, queue DCC0, 1AD6 via DF08) | `enh_bill_size`, `enh_bill_note` (before `r3d_queue_insert`), `enh_bill_pending` (before `r3d_sprite_main`), `sprite()` | the original sprite bitmap scaled to `|0x100 N / d|`-based size with the column / row DDA of `sprite_scaled` (source k = floor(i sw / dw)), centred on the hr point, into the **3D layer** |
  | explosion 1A85 (A17E S3, after the pass) | `enh_expl_pending`, `sprite()` | the same; the 1x writes are recorded as 3D writes (route 3D during the call) and the pixels go into the layer of the last pass |
  | far unit / projectile dots (9ACA D, E: `hud_line_view` of one point) | `enh_dot_begin/end` | an N x N block centred on the hr point (the apparent size of the 1x dot), 3D layer |
  | cannon tracers (A17E S2) | `enh_tracer_p0`, `enh_tracer_begin/end` | thin line (`max(1, N/2)`, Q3) between the hr points, **vector layer** |
  | target boxes / hexagons B3E6, the diamond B339 (A17E S4, `hud_ping_label`) | `enh_box_begin/end`, `enh_diamond_end` | the outline at hr centre +- N x the 1x half sizes, thin, vector layer, clipped to the `hud_line_win` window |
  Vector lines are clipped with a 64-bit port of the 1001 clip core (`clip_core`, same Cohen-Sutherland and rounding block) and drawn with the 1x Bresenham of
  MGRAPHIC slot 31 on layer coordinates, so at N = 1 they are the original pixels.
* Decision: the dots are in the 3D layer, not the vector layer (DESIGN 2.5 said "thin into a vector layer"): they are drawn inside the pass, so later polygons must
  cover them, and a shrunk dot (N/2 px) would make a gameplay marker smaller than the classic one. Tracers and boxes are thin as 2.6 says.
* Hold mechanism (`enh_canvas.c`): `enh_stamp_hold` (the 1x pixels of a sprite / dot are recorded as 3D writes but not stamped into the layer) and `enh_vec_hold`
  (1x line pixels become `SRC_VECHR` records: where no hr pixel lands the compositor shows what was under the line, not the 1x line). `enh_vec_put` is the
  emitters' pixel: it marks the page pixel of its block `SRC_VECHR` too, so an hr line shows where the 1x line did not reach. Compose rule of 2.5 unchanged otherwise.
* `HrCtx.have` stays true after `r3d_end_scene` (the window and layer of the last pass on a tracked page), for A17E, which runs after the pass.
* Views: every mode goes through `r3d_scene_draw` (so one pass, 2.7) and `view_frame`'s keyed blits / strips over the window are page writes that the shadow marks as art
  (rows 91..106 strip, the corners, 256REAR / LEFT / RIGHT, the nose triangle), so the 3D layer shows through exactly the key-0 pixels. Checked on all of them at N = 1
  (the composed frame equals the page for the 13 view scenes) and at N = 2..4 (art blocks unchanged, interior blocks agree).
* The HUD lines were done in S3.3 (2.10). The TV camera sprites were done in S3.2 (2.9).
* Tests: `--hr-check` now also composes the frame (`fpx fdiffer finner finner_differ`, vector footprints `vtot vhit`, object counts) and `--hr-dump` writes `<id>.cmp` (the
  composed canvas) and the vector layer; trace types 7 (sprite + source bitmap), 8 (dot), 9 (clipped vector line) let `r3d_ref.py` judge them with its independent rule.

### 2.9 As built (S3.2): the TV camera window and the map MFD window
* The viewport hook (`r3d_set_viewport`, 2.7) already opens the right N x rectangle for these windows (TV x 169..245, y 110..169 of the page `[4088]`; map x 74..150,
  y 110..169): window = `DS:85CA..85D0`, centre `85D2/85D4`, layer origin `fx + N x0`; the DB71 pixel offset is the frame-pixel window, so nothing to add. Ortho (`[7D5E]`)
  was R1 from S2.2. Measured: the TV and map scenes (35 of the 164 golden scenes) are judged by the reference rasteriser with gap 0, wrong 0, stray 0 at N = 2 and 4.
* Two passes in one window (`mfd_view_render`, close view of an air target: terrain, then the object): `enh_pass_continue` (called before the second `begin_scene`) makes the
  next `enh_r3d_viewport` keep the shadow and layers if the window and page are the same; trace type 0 carries the flag (`keep`) for the reference.
* The 1x backdrop of the sensor window (fill, sky / ground bands: axis-aligned rows, so their N x look is the 1x one) is drawn before the first pass opens: `enh_tv_backdrop`
  (called at the top of the backdrop in `mfd_view_render`) clears the window, sets route 3D and arms the keep flag, so those writes are stamped 3D pixels and the pass'
  polygons are not cut to the 1x footprint (the shadow's "3D" rule shows layer pixels only in blocks whose 1x pixel is a 3D write).
* TV sprites (fire puffs 24h..27h, the smoke column 25h; `r3d_draw_queued` -> `r3d_sprite_tv`): `enh_tv_sprite_pending(e, size)` takes the first end of the last edge's hr
  record (`hr_edge_point`) as the sprite centre and N x the 1x `size`; the S3.1 sprite path draws it into the 3D layer (`enh_sprite_begin` accepts the TV context).
* FLIR tint (`mfd_view_flir_tint`, poly_fill mode 3: `((c & 0Eh) >> 1) | 98h` on the page): the poly_fill hook `enh_rec_remap` runs the same function over the N x N layer block
  of every pixel whose record is a fresh 3D write and keeps the record 3D; other pixels are recorded as art as before. The reference applies the ramp to its expected colours
  (trace type 10 = the tinted rectangle).
* Map MFD: the finished window is saved to the backup page 2 (`mfd_map_save_bg`) and copied back every frame (`mfd_map_restore_bg`) to erase the aircraft marker. The blit is a
  plain page write, so `enh_window_save` / `enh_window_restore` (one slot, not in display passes) keep the window's records and layers beside the backup. The map's route lines,
  symbols and text are art or 1x vector writes (S3.3, 2.10, turns the lines into vector-layer lines).
* Tests: golden scenes `tvclose / tvfire / tvflir / tvsmoke / tvsimple_flir` x 3 sites (two passes, fire puffs with live puff state, smoke column, FLIR, one-pass simple path),
  `mapfp3/6/9` (flight palette), `mapbg3/6/9` (save, a line over the window, restore); all with `pal_flight_load` and `[4088] = 1` (the TV pass page is MGRAPHIC's draw page, as in the game).
  `--hr-check` prints `wpx w3d tpx tramp` (the window still shown from the 3D layer; the tinted window in the green ramp); `r3d_hr.py` asserts them.

* **Real-game fix (S3.2, after the first U1):** in play the TV and map windows were 1x pixels because of how the game moves them: the MFDs are drawn into the draw page (page 1)
  and `view_frame` copies the two 77x60 windows to the other page with `blit_rect` every frame, while the flip (`enh_flip`) copies the shadow only for the rows of the 3D window.
  The shown plane's records for the MFD rows were therefore never refreshed. `enh_blit` (hook in `blit_rect`) now follows a blit between the tracked pages: records and the
  3D / vector layers go along (page 1 work -> page 0 shown and back); a blit from any other page (picture page 2) is art and voids the destination records. Measured in the
  real path (`port/notes/e32/real_path.py`, `--mfd-probe`): TV window shown from the 3D layer 0 % -> 93.7..95.7 %, map window 0 % -> 99.1 %. The golden scenes `tv0..5` ran with
  the pass page != the draw page (not a state of the game: `[4088]` = 1 = draw page = pass page there); they now poke `[4088]` = 1 and are 98..100 % shown.
  Test aids: `--mfd-probe PREFIX` (+ `F117_PROBE_FRAMES`), `F117_VGAME_SELECT` / `F117_VGAME_SITE` (select a site as the target, like the designation of key `n`).

### 2.10 As built (S3.3): the HUD and MFD vector graphics

Every 1x vector line of the cockpit goes through MGRAPHIC `line()` (slot 31): the asm HUD (`hud_draw_front`: frame, reference marks, the pitch ladder's rungs, reticle bars), `7898`
(`hud_overlay_frame`: designator cross, throttle / sink-rate bars, the ILS needles `hud_ils`, rings), the map MFD (`mfd_map_line`, `mfd_map_arc`, `mfd_map_route`), the scope and the TV
crosshair, all through `hud_line_clip` -> `line_clip_far` -> `line`. So one hook pair in `line()` routes them all; nothing in the callers changes (`hr_obj.c`, "the HUD and MFD lines").
* `enh_hud_begin` (before the 1x pixels are written; `line()` after its endpoint swap) decides and holds the footprint (`enh_vec_hold`: the 1x pixels become `SRC_VECHR` records, which show
  what was under them where no hr pixel lands); `enh_hud_end_line` draws the same line thin in the vector layer: `max(1, N/2)` layer pixels (Q3), brush offset `(N - w) / 2`, from the 1x
  endpoints (frame pixels incl. `draw_off`). The brush runs through the **1x Bresenham on layer coordinates** (`vbres`, the S3.1 code), so at N = 1 it is the original line pixel for pixel.
  Horizontal and vertical lines keep the full 1x length (block edge to block edge: ticks and the frame corners join); diagonals run between the block origins plus the brush offset.
* **Not taken** (they stay the original's pixels, N x N): colour 0 (the HUD's bar gauges draw black / dark lines that erase; also keeps the DAC-index rule: the lines are DAC indices, so the
  HUD colour sets F4 / Alt-N / the flashes still work), a single point (`hud_pixel`, the map's 2x2 blips and dots: they keep their solid look; an N x N block is also what the S3.1 dots are),
  lines of a 3D pass (`enh_route` 3D: 3D-layer geometry) and lines inside another emitter's hold (tracers, target boxes, S3.1).
* `hud_tick_strip` (071A, the airspeed / altitude / heading tapes' scale lines and ticks) writes the page directly: its pixels are now vector records (`TICK_PX`), and `enh_hud_tick` draws
  the 2- and 3-pixel ticks as thin horizontal lines and every scale-line pixel as a dash as tall as the 1x pixel (the scale line is dotted in the original, every second row).
* The ADI (`cockpit_draw_adi`, 17A3): its horizon is a filled quadrilateral. `enh_adi_begin/end` hold the `poly_fill` footprint (`enh_rec_vecfill`: the same record as a held line pixel) and fill
  the polygon again at N x (N >= 2): edges through the vertex-pixel centres, a layer pixel in when its centre is inside or on an edge: a straight horizon instead of the staircase. The
  bezel on top (`cockpit_keyblit`) and the copy to the other page stay art.
* **Page 0 and page 1.** The map's lines are drawn on both pages (`hud_line_clip` both), and the step draws the route legs on both pages after the flip. A vector write on page 0 (the displayed
  page) now belongs to the **shown** plane and layer (`plane_shown`), one on page 1 to the work ones; before, both went to the work plane and the second copy overwrote the first one's
  `under` with the other page's stale pixel. (3D writes and the other emitters are unchanged.)
* Test hooks: `--mfd-probe` prints the vector records of the shown page that are shown from the vector layer (`hudv`, `mapv`, `allv` and the HUD elements `tapel`, `taper`, `framel`, `framer`,
  `centre`; colour 0 excluded) next to the TV / map counts; `--hr-check` adds `vthin` (footprint blocks that show less than the line colour: the lines are thin) and `vstray` (blocks with a
  colour that is neither the line, nor what it replaced, nor a colour within two original pixels). Trace types for the reference rasteriser: 9 (a clipped vector line, S3.1), 11 (the ADI
  polygon), 12 / 13 (the map window is saved to / restored from the backup page: the reference keeps and restores its lines).
* What stays original pixels: all text (tape numbers, ladder labels, messages), the reticle and the tick bitmaps blitted from `256PIT.PIC` (`blit_key`, `cockpit_keyblit`: the flight-path
  marker, the aircraft marker, the lamps, the panel), the filled bars and boxes (`fill_rows_far`, `hud_fill_rect`: the threat bar, fuel bar, MFD backgrounds).
* Tests: 12 golden scenes `hud_l0 / l1 / l2 / l0s / lvl` (the HUD at levels 0 / 1 / 2 and the small view, rolled and pitched), `hud_adi0..2`, `hud_ils`, `hud_scope`, `hud_map3 / 6` (route lines,
  both pages, arcs, the backup round trip); golden total 176, original-vs-port byte for byte. N = 1: 0 differing frame pixels in all 176 scenes; N = 2 / 3 / 4: interior blocks agree
  99.98 / 99.94 / 99.92 %, every footprint pixel has an hr pixel within one original pixel (4631 of 4631 at N >= 2), `vstray` 0, the lines are thin (`vthin`); the reference rasteriser judges
  255 lines and polygons of the HUD scenes (52 484 layer pixels at N = 4): gap 0, wrong 0, stray 0. The real game path (`port/notes/e32/real_path.py`, ctest `r3d_hr_realpath`: the scripted
  chain with F4, v, c, F8, F3, Alt-N): 99.4-99.8 % of the HUD's vector records show a vector-layer pixel (tapes, frame and the ladder / reticle centre 100 %), the map window with the route
  legs 82 % (the rest are the blips), the scope 100 %.

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
| 0x41..0x43 rear/left/right | frame only | frame-mapped | not extended: the whole 320x200 picture (3D at N×, art NxN) **filling the window height** (scale = window height / 200 with the pixel aspect, non-integer: the canvas is the frame at N = ceil(height / 200) and the host scales it), black only at the sides (Decisions 2026-10-21 b) |
| second cockpit view (window frame up top) = **view 0 with `[294B]` = 1** (identified in S3.4, below) | canvas | frame-mapped | **filling the window height** (as 0x41..0x43) and **extended left and right only**: the canvas is 200N high and as wide as the window's aspect, the frame top centre, the world beside it from the wide pass; nothing above (no art for the frame above) (Decisions 2026-10-21 b) |
| TV, map MFD | their frame rect at N× | frame-mapped | not extended (panel) |
| SETUP/shell/START/END/debrief, VGAME menus | — | — | 320x200 (or 640x400 text) scaled by the largest whole N, centred, black bars |

**The second cockpit view (S3.4).** VGAME has two cockpit views of `[C07A]` = 0, switched with the key `c` (`1e9e`: `[294B] = ([294B] == 0)`, simflow.md 2.1;
reset to 0 at mission start by `39c4`): `[294B]` = 0 is the first (wide HUD, instrument panel at the bottom, the 3D window rows 0..90 with only the HUD over it),
`[294B]` = 1 the second, the "small view": half-scale projection (B85D and 1442 halve X / Y, `[85D4]` = 60), the compact HUD, and `hud_side_strips` (d9a3, simflow.md
section 4, port `sim_hud.c`) `cockpit_keyblit`s two 54 x 91 frames from the top of 256PIT.PIC (picture x 141 and 196, rows 0..90) onto screen x 33..86 and 233..286,
rows 0..90: the window-frame struts, which run up to row 0 of the frame and have no art above it. No other view mode has a second cockpit variant (`[2DFC]`, Alt-C,
only makes the window 107 rows tall with keyed panel corners at the bottom; 0x41..0x43 are the looking-out views). Measured in the real path
(`port/notes/e34/wide_realpath.py`, 16:9): the second cockpit view shows 32.5 % art in the canvas against 28.6 % in the first (the struts); it is placed FIT.

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

### 3.5 As built (S3.4): choices where 3.1-3.4 were silent
* **How the extension stays out of the sim.** The canonical frame runs exactly as before (its main pass is not widened). When the view is extended and the canvas is
  larger than the frame (`enh_wide_due`, decided at `enh_frame_begin` from `[C07A]` / `[294B]` and the canvas), A_k is taken and `enh_idle` runs **one wide display
  pass** (`enh_display_pass(0)` with `enh_wide_pass`): the same frame from A_k, with the main 3D pass widened (`enhanced/wide.c`). After its flip the pass's 3D layer
  (whole canvas) and vector layer are copied out (`enh_wide_capture`, pending) and the live state is restored as in S2.4; the canonical flip makes the pending
  picture current (`held_promote`). The compositor (`compose_src_live`) takes the **3D layer of the wide picture inside the frame's 3D window and everywhere
  outside the frame**, its vector layer outside the frame (tracers), and everything else (page, shadow records, the HUD's vector layer, the MFD windows below the 3D
  window) from the live frame, so HUD, panel and MFDs are the canonical frame's. The wide picture is used only while its geometry equals the live canvas and no menu
  is up. A frame that changes the view (`[2E48]` != `[C07A]`: `view_frame` would read the cockpit picture, a file access refused in a pass) gets no pass: that one
  frame shows the canonical picture (black outside). This is 4.1's "`--interp off`: one display pass with alpha = 1 per step"; S4.1 runs it at every refresh.
* **The main pass** is the pass whose window starts at x 0, y 0 and ends at x 319 (view_frame's 3D window; the TV and map windows are smaller). Wide: the hr clip and
  1001 windows are the canvas (`HrCtx.wide`; `wx, wy` keep the original window's layer origin for the R1 "fits s16" tests and the sprite placement; `e2dx, e2dy` move
  the 1001 window-relative points of the bands / horizon into the canvas window; the horizon line of hz_draw is at least the canvas half diagonal long; the slot-37
  single-pixel quirk is applied at the canvas edge). The frame below the 3D window (the panel and its MFD windows) is a **hole** that is never written (`HR_IN_HOLE` in
  `hr_put` / `put3d`, trace type 14), so the TV / map windows keep their layers. The layers outside the frame are cleared when the wide pass opens.
* **Widening factors** (`enh_wide_begin`, from the canvas and the projection centre, in 1x px): mx = the larger extension left / right of the centre, my = up / down.
  E8B5: `kx = max(1, mx / 160)`, `ky = max(1, my / [0D20])` (Q8, on the wrapped s16 tolerances, both branches of `[4360]`); 02B6: the bounds times
  `max(1, mx / 256)` and `max(1, my / 192)` (the 45-degree pyramid only grows past tangent 1); B85D (`enh_wide_pt`: the main pass, and A17E S2 tracers / S3 explosion
  right after it, `enh_wide_ovl`; never the S1 / S4 boxes, the HUD symbology): `|X| <= -Z max(1, mx / 256)`, sx in `[-ceil(fx / N), 319 + ceil(right / N)]`,
  sy in `[-ceil(fy / N), (lh - fy) / N - 1]` (the front view's objects beside the panel count), `sx = -1 -> -2`. The 1x sprites / lines of an out-of-frame point
  are clipped by their own windows (`sprite_scaled`, `hud_line_view`), so only the hr emitters draw them; tracers in a wide pass are clipped to the canvas.
* **Terrain cells** (04DA, `enh_wide_cell`): built at run time from the image's own tables (`DS:034C`, `04CC/04DE`), so no generated header: per octant the original
  cells in their order (path B 9, path A the 15 with the quirky overread tail) plus the cells of the 5 x 5 block with `dot(cell, octant direction) >= 0` they do not
  visit, each put before the first original cell that is not farther (far -> near, outer first; the camera cell and path A's tail stay last). New far cells (dot >=
  that of the original's first four) count as "i < 4" for the low-detail skip. The down-look table becomes the full 5 x 5 (the 16 ring cells first) when the pitch
  is below 60 degrees down **or** the lower edge of the extended view looks past the vertical (`pitch < -(90 deg - atan(down / 192))`).
* **Placement.** `enh_view_class(C07A, [294B])`: view 0 with `[294B]` = 0 FRONT, with `[294B]` = 1 SIDE (the second cockpit view), 0x41..0x43 FIT, the rest FULL.
  **S3.4 fix (Decisions 2026-10-21 b):** FIT and SIDE fill the window height. Both render at N = ceil(Lh / 200) (or the fixed `--render-scale`) with a canvas 200N high:
  FIT's canvas is the frame (320N x 200N), SIDE's is as wide as the window's aspect (`Lw 200N / Lh`), the frame at the top centre, the 3D viewport the whole canvas
  (extended left and right only; the wide pass runs for it with the half-scale tangents of `[294B]`: the frustum's mx / 128, my / 96; B85D's halved X keeps mx / 256).
  The host's letterbox presentation scales the canvas to the window height, so the shown scale is height / 200 (non-integer; black only at the sides of FIT).
  `--render-scale N` is kept also with wide when it is larger than the window holds (S2.1 lowered it to the fit): the canvas then holds at least the frame and the
  host scales it down, as without wide (so the headless tests keep their N). VGAME's own screens (the quit requester Alt-Q, the boss screen Alt-B) set the menu
  placement while they are up (`enh_menu`, hooks in `sim_input.c`): pillarboxed, no extended world. Pause (Alt-P) keeps the frozen extended frame.
* **`--wide` default: on** (S2.1's note "the default turns on with S3.4"; CLAUDE.md "enhanced defaults are on"); `--no-wide` and `--classic` turn it off; the launcher
  switch is S5.1's. With a 4:3 window (4:3 pixel aspect) the canvas is the frame and nothing changes.
* **HUD anchor (3.4)** needed no code: the HUD, panel and MFDs are drawn in frame coordinates by the canonical frame and composed from it; the projection centre and
  scale inside the frame are the original ones, so the HUD lines up with the extended world.
* **Test aids**: `--wide-test WxH` (the render test draws each scene's main pass wide on a W x H canvas placed for the scene's view; `enh_canvas_test_window` keeps
  the host's window from moving the canvas mid-scene), `--wide-geometry-test`, `F117_WINDOW=WxH` (the window a headless run pretends to have); `--mfd-probe` prints
  per flip the presented canvas by class (world / art / vector / black, outside the frame, seams).
* **Measured** (2026-10-21): sandbox hash with `--wide` (canvas 1280 x 600): 212 VGAME frames identical to the plain run in everything but the canvas, 211 wide passes,
  0 forbidden calls. Real path (16:9 canvas 1920 x 900 and 21:9 2560 x 900, N = 4): outside the frame 100 % world in the first cockpit view and all outside views,
  100 % black in the second cockpit view and left / right / rear; seams at the border 0-0.17 % of world pixel pairs (inside the frame 0.08-2.95 %); 331 wide passes in
  461 frames (the FIT views have none), 0 forbidden calls; a pass costs ~17 ms at 1920 x 900. Wide golden scenes (N = 2, 960 x 480 and 1280 x 480, 115 scenes with a
  wide pass): reference gap 0 / 0.0001 %, wrong 0.003 %, stray 0; unwritten extended pixels 0 / 49 (a sky-band edge sliver at the far right of the 21:9 canvas in two
  steep-roll / high scenes); nothing in the hole; inside the frame 99.6-99.7 % equal to the frame-only render; seams 0.96-0.99 % against 1.24-1.30 % inside.

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

### 4.7 As built (S2.4): choices where 4.2 was silent
* **Snapshot set** (`enhanced/sandbox.c`, a region table rebuilt at every frame start): `mem[]` whole (1 114 112 B: every DGROUP,
  the comm block, the pages in the DOS arena, the BIOS data area), `cur_dgroup`, `dg_sp` (the emulated stack of the render's
  locals), `timer_ticks`; the VGA model (vram 256 KB, DAC, CRTC/sequencer/GC/attribute registers and indices, the DAC port state,
  the `dirty` flag; the composition cache is output and not in it); `mgraphic.c` (page table, draw seg/off, flip state, shake,
  colour, last x/y, `rng`, the sprite-pool and encoder words, and the driver image itself, whose data segment is the original's);
  `pic.c` (decoder state, `rle_out`); `r3d_object.c bsp_out`, `r3d_bisect_last_bp`; the hr back end's fixed statics (`enh_hr`,
  `enh_hr_pt`, the clipper's vertices/edges/assembler state, the 1001 dirty rows, `hr_miny`). The hr row tables are scratch rebuilt
  from the faithful R3dWork tables when a pass opens and are not in it. Each module lists its own statics in an `ENH:`
  `*_sandbox_regions(add)` function (vga.c, mgraphic.c, pic.c, r3d_object.c, hr_pass.c); `sandbox.c` copies the regions into one
  buffer with one memcpy each. Image 1 418 421 B. No sound or flight static is in it: a pass never reaches them (below).
* **Canvas** (`enh_canvas.c`, `EnhCanvasSnap`): geometry, view, route, shadow plane (2 x 256 000 B) and the four layers (4 x lw x lh x 2 B;
  5 120 000 B in all at the headless 960 x 600, N = 3). A_k takes a deep copy once per frame; a pass copies A_k's canvas into a
  scratch set and **swaps** pointers with the live canvas, and swaps back afterwards (no copy back). The shadow planes became
  pointers for this (`sh_buf`).
* **Order**: `enh_frame_begin` (A_k) → canonical render → `frame_floor`: `enh_idle()` first, then once per wait iteration (S4.1
  will run one pass per refresh there; S2.4's `--sandbox-test N` runs its N passes at the first call) → flip → step.
  A pass: save live, canvas swap-in, load A_k, `enh_sbx = 1`, `view_frame`, `hud_overlay_frame`, (view 0) `hud_draw_far`,
  `hud_side_strips`, `cockpit_draw_adi`, `flip`; `enh_sbx = 0`, load live, canvas swap-out. Nothing is presented in S2.4 (S4.1
  composes the pass into the host frame), so a pass is invisible.
* **Inside a pass** (`enh_sbx`): `sim_snd_cmd` returns 0, `sim_snd_noise` returns; `frame_delay`, the `[262A]` wait,
  `vga_wait_vretrace_*` and `host_wait_until_ns` return at once. Refused and counted (`enh_sandbox_deny`, reported as violations):
  `host_pump` (so no tick, ISR body, sound poll, event or present), `host_opl_write`, `sim_snd_poll`, and every DOS/stdio file
  entry (`dos_open/creat/read/write`, `rtl_open/write`, `fopen_fn`, `unlink_fn`, `platform_load_file_paras`). Measured: 0 in the
  test chain. The comm block is in `mem[]` and comes back with it.
* **Test view**: `--sandbox-test N` renders pass v (1..N) of a frame with the heading + v x 0x400, own x/y + v x 96 / 64 world units
  and 2D94 rebuilt from the angles (`sim_matrix_from_angles`, as 4.2 step 2), so its pictures, designators and tracer hits really
  differ from the canonical frame's; a pass that rendered the same frame would hide leaks that the canonical frame overwrites.
* **Cost** (measured 2026-10-20, headless chain, canvas 960 x 600): A_k 0.4–0.8 ms per frame; per pass 0.55–0.95 ms snapshot (live save
  1.4 MB, canvas copy 5.1 MB, A_k load 1.4 MB) and 0.10–0.17 ms restore, plus the render (~1.9 ms). 60 passes a second cost
  about 40–70 ms of copies per second; the canvas copy dominates and shrinks to the not-redrawn rects if needed (4.2 step 1).
* **Determinism for the test**: `F117_VCLOCK=1` makes the host clock virtual (`host.c clk_ns`: +1 us per read, a pump without a
  tick due jumps to the next tick, no sleep; a pass reads it without advancing it), so two runs of the whole chain are the same
  machine run tick for tick (seeds from the BIOS clock included). `--sandbox-hash FILE` writes per VGAME frame (at
  `enh_frame_begin`, i.e. the state the previous step left) FNV-1a hashes of DGROUP, the comm block, the LCG `926E`, the sound
  driver's memory, the OPL write stream, all of `mem[]`, vram + DAC and the canvas, and a final line at exit. No field is excluded.

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
| `r3d_project_point` | `sim/sim_objovl.c` | hr sx/sy (+ invalid on early return); extended range when wide and route 3D | S2.2, S3.1, S3.4 |
| `r3d_draw_world_objects` (B billboards, D/E dots), `obj_overlay_frame` (S2 tracers, S3 explosion), `hud_target_box`, `hud_draw_diamond` | `render3d/r3d_world.c`, `sim/sim_objovl.c` | `enh_bill_size`, `enh_dot_begin/end`, `enh_tracer_*`, `enh_expl_pending`, `enh_box_*` (2.8) | S3.1 |
| `sprite`, `r3d_queue_billboard`, `r3d_draw_queued` | `render3d/r3d_scene.c` | `enh_sprite_begin/end`, `enh_bill_note`, `enh_bill_pending` (2.8) | S3.1 |
| `mfd_view_render`, `map_render_terrain` | `sim/sim_mfdview.c`, `render3d/r3d_terrain.c` | the DB71 hook covers them; `enh_tv_backdrop`, `enh_pass_continue`, `enh_tint_note` in `mfd_view_render` (2.9) | S3.2 |
| `line`, `hud_tick_strip`, `poly_fill`, `cockpit_draw_adi` | `video/mgraphic.c`, `sim/sim_cockpit.c` | `enh_hud_begin/_end_line`, `enh_hud_begin_tick/_tick/_end_tick`, `enh_rec_vecfill`, `enh_adi_begin/_end` (2.10) | S3.3 |
| `r3d_draw_queued` (TV sprite), `poly_fill` mode 3, `mfd_map_save_bg` / `mfd_map_restore_bg` | `render3d/r3d_scene.c`, `video/mgraphic.c`, `sim/sim_mfd.c` | `enh_tv_sprite_pending`, `enh_rec_remap`, `enh_window_save/restore` (2.9) | S3.2 |
| `r3d_cull_core`, `r3d_frustum_test` | `r3d_scene.c`, `r3d_object.c` | widen by kx/ky in wide display passes (`enh_wide_kx/ky`, `enh_wide_frustum`) | S3.4 |
| `r3d_draw_terrain` | `render3d/r3d_terrain.c` | wide cell list in wide display passes (`enh_wide_cell`) | S3.4 |
| `obj_overlay_frame` (S2 .. S3), `r3d_project_point` | `sim/sim_objovl.c` | `enh_wide_ovl`, `enh_wide_xfield`, `enh_wide_range` (3.5) | S3.4 |
| `sim_quit_requester`, `sim_boss_screen` | `sim/sim_input.c` | `enh_menu` (pillarboxed while up) | S3.4 |
| `frame_loop_body` | `sim/sim_main.c` | `enh_frame_begin()` before `view_frame` | S2.4 |
| `frame_floor` wait loop, `frame_delay` | `sim/sim_main.c` | `enh_idle()` → display pass; return at once in sandbox | S2.4, S4.1 |
| `sim_snd_cmd`, `sim_snd_noise` | `sim/sim_stubs.c` | no-op in sandbox | S2.4 |
| `sim_snd_poll`, `host_pump`, `host_opl_write`, `dos_*`, `rtl_open/write`, `fopen_fn`, `unlink_fn`, `platform_load_file_paras` | `sim/sim_stubs.c`, `host.c`, `platform/dosio.c` | refused + counted in sandbox (`enh_sandbox_deny`) | S2.4 |
| `host_wait_until_ns`, `vga_wait_vretrace_*` | `host.c`, `platform/vga.c` | return at once in sandbox | S2.4 |
| `host_ns`, `host_pump`, scripted keys | `host.c` | `clk_ns` (F117_VCLOCK virtual clock, test aid) | S2.4 |
| `host_opl_write` | `host.c` | `enh_opl_trace` (the OPL stream for `--sandbox-hash`) | S2.4 |
| statics | `vga.c`, `mgraphic.c`, `pic.c`, `r3d_object.c` | `*_sandbox_regions(add)`: the module's statics for the snapshot | S2.4 |
| `main` | `main.c` | `enh_env_args()` before anything runs | S2.4 |
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
| `enh_sandbox_hash` | as built (4.7): the `debrief_chain_sea` chain on the virtual clock, plain / 3 test-view display passes per VGAME frame / `--classic`: per-frame hashes of DGROUP, comm, LCG, sound driver, OPL stream, `mem[]`, VGA, canvas identical (canvas not compared with classic), final comm block and roster identical, 0 forbidden calls | S2.4 |
| `sim_interp_equal` | the same scenarios with N=4, wide, interp (3 display passes per step, alpha .25/.5/.75) vs `--classic`: DGROUP after every step and the comm block at the end identical | S4.1 |
| `enh_wide_geometry`, `r3d_hr_wide`, `r3d_hr_wide_realpath`, `enh_sandbox_hash` (`--wide` run) | the placement per view and window; the golden scenes with a wide main pass (reference, gaps, hole, seams); the real-path view sweep at 16:9 / 21:9 (3.5) | S3.4 |
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
* Q2 (decided after seeing it in the game: both modes built, S2.3 shows them) dither: default original-pixel checkerboard; alternative fine (per layer pixel, smoother tone).
* Q3 (decided 2026-10-19: max(1, N/2)) line width at N×: default `max(1, N/2)` layer pixels; alternative always 1.
* Q4 (decided 2026-10-19: the world beside the panel) front cockpit: default the world also beside the instrument panel; alternative black side strips below the window line.
* Q5 (decided 2026-10-19: 10/s) (U3-2) sim step rate: default 10/s (Game speed 7); alternative 15/s.

Plan changes (applied to PLAN.md, ids kept): the wider view needs the sandboxed display pass before Phase 4, so the sandbox
becomes its own foundation **S2.4 (O, M)**; S4.1 shrinks to the loop, alpha and the own-aircraft/camera lerp (**O, M**, was L);
S2.3 gains the reference-rasteriser test (**S, M**, was H, S); S3.1 is routing + sprites/dots/tracers + all view modes on a
finished back end (**S, M**, was O); S3.2 is mostly verification (**S, S**); S3.4 depends on S2.4.
