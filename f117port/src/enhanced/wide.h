/* The wider view (DESIGN.md 3; S3.4).
 *
 * In the extended views (the first cockpit view: view 0 with [294B] = 0; the full-screen 3D and outside views 0x44, 0x84..0x8C) the canvas is wider and taller
 * than the 320x200 frame. The canonical faithful frame runs unchanged (its culling, B85D range and terrain cells are the original's, so the sim is the original's);
 * then one sandboxed display pass (sandbox.c, DESIGN 4.2) renders the same frame again from the pre-render image A_k with the culling widened to the canvas, and
 * its 3D layer (and the vector layer outside the frame) is what the compositor shows in the 3D window and around the frame. Everything the pass changed is
 * thrown away, so the extra objects it draws (designators, tracer hits, puffs, spinners) never reach the sim.
 *
 * Widened in the main 3D pass of a wide display pass only (enh_wide_on): E8B5 r3d_cull_core (kx, ky), 11EB:02B6 r3d_frustum_test (the tangents), 04DA the terrain
 * cells (the forward half of the 5x5 block per octant; the down-look table the full 5x5), and B85D r3d_project_point (the x field and the screen range), the last
 * one also in A17E S2 / S3 (tracers, explosion) after the pass (enh_wide_pt). The 1x page keeps its original window; the hr window is the canvas less the frame
 * below the 3D window (the panel: HrCtx.hole). */
#pragma once

#include "types.h"

extern bool enh_wide_pass;     /* a wide display pass is running (sandbox.c), or the wide render test (--wide-test) */
extern bool enh_wide_on;       /* the main 3D pass of it is open: cull, frustum, terrain cells and B85D widened */
extern bool enh_wide_pt;       /* B85D accepts the extended field and range (enh_wide_on, and A17E S2 / S3 after a wide main pass) */
extern int  enh_wide_win[4];   /* the main pass's window (frame px x0 y0 x1 y1) when enh_wide_drawn */
extern bool enh_wide_drawn;    /* the wide main pass ran in this display pass */

void enh_wide_begin(void);     /* hr_pass.c: a wide main pass opened (enh_hr holds the canvas window): the widening factors */
void enh_wide_end(void);       /* hr_pass.c: it closed */
void enh_wide_ovl(bool on);    /* sim_objovl.c obj_overlay_frame: true before S2 (tracers), false after S3 (explosion) */

/* engine hooks (behind enh_wide_on / enh_wide_pt) */
s32  enh_wide_kx(s16 sx);                   /* E8B5: the x tolerance times kx (int32, on the wrapped 1x value) */
s32  enh_wide_ky(s16 sy);                   /* E8B5: the y tolerance times ky */
void enh_wide_frustum(u32 *tx, u32 *ty);    /* 02B6: the x / y bounds times max(1, tangent of the extended half angle) */
bool enh_wide_xfield(s32 X, s32 nz);        /* B85D: |X| <= -Z max(1, T) (nz = -Z > 0) */
void enh_wide_range(int *xlo, int *xhi, int *ylo, int *yhi);   /* B85D: the accepted sx / sy range: the canvas in frame pixels */
/* 04DA: cell i of the wide list (path A = level 4 at detail >= 2, else path B; the down-look table when looking down). Returns the index the original's
 * "i < 4" tests see (the original index for an original cell, 0 for a new far cell, 8 for a new near one), or -1 at the end of the list. */
int  enh_wide_cell(bool pathA, s16 pitch, int oct, int i, s16 *ox, s16 *oy);

/* sandbox.c */
bool enh_wide_due(void);                    /* enh_frame_begin: this frame gets a wide display pass (wide, an extended view whose canvas is larger than the frame) */
