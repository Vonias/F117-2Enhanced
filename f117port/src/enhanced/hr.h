/* The high-resolution 3D back end (DESIGN.md 2.2-2.5; S2.2).
 *
 * The faithful render3d runs unchanged and writes the 320x200 page. At each geometry point an ENH: hook hands the same primitive to
 * this back end, which recomputes it at N x (rule R1: N enters inside the innermost rounding of each formula, so at N = 1 every formula
 * is the original one) and rasterises it into the 3D layer of the canvas (enh_canvas.h) as 8-bit DAC indices.
 *
 *   hr_pass.c    the pass context: viewport hook, route, layer / shadow clear, the B85D point, the --hr-check statistics
 *   hr_clip.c    copy of render3d/r3d_clip.c + the 0906 projection and 0B3F edges of r3d_object.c (int32 / int64)
 *   hr_raster.c  copy of render3d/r3d_raster.c (row tables of Lh rows, dither modes, wide lines)
 *   hr_edge2d.c  copy of render3d/edge2d.c (1001) + the sky / ground bands of r3d_sky.c (1442)
 *
 * The back end never writes mem[], the VGA model or the driver statics: the 1x page and the sim cannot see it. */
#pragma once

#include "types.h"

typedef struct HrCtx {
    bool on;                      /* a 3D pass that draws into the layer is open */
    bool have;                    /* S3.1: the window / layer fields below describe the last pass on a tracked page (stays true after r3d_end_scene, for the overlay pass A17E) */
    int n, fx, fy;                /* scale and frame origin in layer pixels */
    int lw, lh;                   /* layer size */
    u16 *layer;                   /* the 3D layer being drawn (enh_layer3d) */
    u16 seg;                      /* the page segment of the pass (DS:85EA) */
    int x0, y0, x1, y1;           /* the pass window in frame pixels (DB71) */
    s32 xmin, ymin, xmax, ymax;   /* the 12EF / 1359 window in layer pixels: [fx + N x0, fx + N (x1 + 1) - 1] */
    s32 cx, cy;                   /* the projection centre in layer pixels: fx + N [85D2], fy + N [85D4] */
    s32 ox, oy;                   /* the 1001 window origin (layer pixel of window pixel 0,0) */
    s32 exmax, eymax;             /* the 1001 XMAX / YMAX: N W - 1, N H - 1 */
    int line_w, line_o;           /* line brush: max(1, N/2) pixels, offset (N - w) / 2 (DESIGN 2.4, Q3) */
    bool fine;                    /* --dither fine */
} HrCtx;

extern HrCtx enh_hr;

/* hr position of the last r3d_project_point (B85D) call, for the emitters of the same object (S3.1) */
typedef struct { bool valid; s32 x, y; s32 size; } HrPoint;   /* size: the hr size (layer px) of the sprite of this point when a caller computed it, else 0 */
extern HrPoint enh_hr_pt;

#define ENH_HR(call) do { if (enh_hr.on) { call; } } while (0)

/* ---- hooks in the faithful render3d (each behind enh_shadow_on / enh_hr.on) */
void enh_r3d_viewport(u16 vp);                     /* end of r3d_set_viewport (DB71): opens a pass, route 3D */
void enh_r3d_end(void);                            /* end of r3d_end_scene (DB3E): closes it, route art */
void hr_project(u16 di, u16 bx);                   /* r3d_project (11EB:0906): the hr vertex of the same slot */
void hr_make_edge(u16 rec, u16 v0x8, u16 v1x8);    /* r3d_make_edge (11EB:0B3F): the hr edge record, clipped */
void hr_poly_add_edge(u16 rec);                    /* r3d_poly_add_edge (12EF:003E) */
void hr_poly_finish(void);                         /* end of r3d_poly_finish (12EF:010A): closing edge, walls, fill */
void hr_line(u16 rec);                             /* r3d_line_M (1359:045D) */
void hr_sky_ground(void *ctx);                     /* end of r3d_draw_sky_ground (1442) */
void hr_project_point(s32 X, s32 Y, s32 Z, bool visible);   /* end of r3d_project_point (B85D) */

/* ---- S3.1 (hr_obj.c): the objects the world pass 9ACA and the overlay pass A17E draw at 1x on top of the scene: sprites (smoke billboards 1AD6, the
 * explosion 1A85), the far dots, the tracers and the target boxes. Each is emitted at N x from the hr point of B85D; the 1x footprint stays on the page
 * (and in the shadow) but is not stamped / shown. All hooks are behind enh_shadow_on. */
void hr_obj_pass_begin(void);                      /* a pass opens: the billboard table is cleared */
void enh_bill_size(int mode, int d);               /* 9ACA (B): the hr size of the puff just projected (lib_scale_eighths(mode, |0x100 N / d|)) */
void enh_bill_note(void);                          /* r3d_queue_billboard, before the insert: the hr point and size of the record the insert will fill */
void enh_bill_pending(u16 rec);                    /* r3d_draw_queued, before the billboard sprite of `rec` */
void enh_expl_pending(int dep);                    /* A17E S3, before the explosion sprite: B85D's point, size |0x100 N / dep| */
bool enh_sprite_begin(u16 ctx);                    /* sprite(), before sprite_scaled: true = an hr sprite follows (1x footprint recorded, not stamped) */
void enh_sprite_end(u16 off, u16 seg, int size);   /* sprite(), after: draws the hr sprite (the source bitmap at off in seg, 1x size `size`) */
void enh_dot_begin(void);                          /* 9ACA (D)/(E): around the one-pixel far dot (hud_line_view of one point) */
void enh_dot_end(int colour);
void enh_tracer_p0(void);                          /* A17E S2: B85D's point after the first projection (the tail of the tracer) */
bool enh_tracer_begin(void);                       /* before the hud_line_view of the tracer: true = an hr line follows */
void enh_tracer_end(int colour);
bool enh_box_begin(void);                          /* hud_target_box / hud_draw_diamond: true = an hr outline follows (the 1x lines are a footprint) */
void enh_box_end(int x, int y, int sz, int hex, int colour);   /* hud_target_box after the lines: sz = the half size after the 294B halving */
void enh_diamond_end(int sx, int sy, int colour);

/* ---- S2.4: the fixed statics of each file for the display-pass snapshot (the row tables tl/tr, tab_l/tab_r are scratch rebuilt from the
 * faithful R3dWork tables when a pass opens, and are not in it) */
#include <stddef.h>
void hr_clip_regions(void (*add)(void *p, size_t n));
void hr_edge2d_regions(void (*add)(void *p, size_t n));
void hr_raster_regions(void (*add)(void *p, size_t n));

/* ---- inside the back end */
typedef struct HrEdge {
    s32 x0, y0, x1, y1;           /* before the clip: projected + centre; after it: the clipped ends */
    u16 flags;                    /* the rec+2 flag word of the original */
    s32 yc0, yc1;                 /* rec+6 / rec+0E: y clamped to the window */
    s32 nx, ny;                   /* rec+10h: the near-plane point */
} HrEdge;

void hr_clip_edge(HrEdge *e);
void hr_clip_begin(void);                          /* the assembler state (walls, flags, near-plane points) from the faithful one */
void hr_raster_begin(void);                       /* the row tables and assembler state from the faithful ones (R3dWork) */
void hr_edge_dda(const HrEdge *e);
void hr_wall_fill(s32 a, s32 b, s32 x);
void hr_fill(u16 colour);                          /* fill_M on the row tables */
void hr_draw_line(s32 x0, s32 y0, s32 x1, s32 y1, u8 c);   /* line_M with the wide brush */
void hr_put(s32 x, s32 y, u8 c);                   /* one layer pixel (no clip) */
u8   hr_get(s32 x, s32 y);                         /* the layer pixel, or the page pixel under it when not written */
s32 *hr_tab_l(void);
s32 *hr_tab_r(void);
extern s32 hr_miny;

/* 16-bit wrap rule of R1: a value that wraps at the 1x magnitude is used wrapped, then scaled */
static inline s64 hr_keepwrap(s64 exact1, s64 hr, int n) { return exact1 == (s16)exact1 ? hr : (s64)n * (s16)exact1; }
static inline bool hr_fits16(s64 v, int n) { return v >= -32768LL * n && v <= 32767LL * n; }

/* ---- --hr-dump (S2.3): the primitives the hr back end rasterised, as int32 words, for the independent reference (port/notes/e23/r3d_ref.py).
 * 0 B xmin ymin xmax ymax (a pass opens; layer px)   1 E x0 y0 x1 y1 (a clipped edge entering the DDA)   2 W a b x (a wall)
 * 3 F colour (the polygon of the E / W since the last F is filled)   4 L x0 y0 x1 y1 colour w o (a line, brush w at offset o)
 * 5 Q ox oy colour n x y ... (a 1001 polygon, points relative to the window origin)   6 H (hz_draw fills a half plane: not modelled) */
extern bool hr_tracing;
void hr_trace(int type, int nargs, const s32 *a);
void hr_trace_poly(s32 ox, s32 oy, u8 colour, int n, const s64 *pts);

/* ---- --hr-check (the render test): per scenario statistics of the hr layer against the 1x page */
void enh_hr_test_begin(void);                      /* simtest "scn": canvas on at --render-scale, layers and shadow cleared */
void enh_hr_test_end(const char *dir, const char *id);
void hr_obj_stats(long *sprites, long *sprite_px, long *dots, long *lines, long *line_px, bool reset);   /* S3.1: what the emitters drew since the last reset */   /* simtest "end": one line into DIR/hr_check.txt */
