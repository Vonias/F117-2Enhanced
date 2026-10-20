/* The hr copy of render3d/r3d_raster.c (VGAME 1359, mode M; DESIGN.md 2.2, 2.4): the same integer edge DDA, walls, span fill and
 * Bresenham line on layer coordinates (int32), into the 3D layer. Row tables have Lh rows; the sentinels 7FFFh / 8001h become
 * INT32_MAX / INT32_MIN + 1. The colour word is the faithful one (DS:85D6 after r3d_setcolor_M, read only).
 * Dither: --dither grid (default) evaluates the checkerboard on the original-pixel phase ((X - fx) / N ^ (Y - fy) / N) & 1, so a
 * dithered face keeps the original look at any N; --dither fine uses the layer pixel ((X - fx) ^ (Y - fy)) & 1. Both are the
 * original's (x ^ y) & 1 at N = 1. Lines are max(1, N/2) layer pixels wide (Q3), the brush centred in the original pixel. */
#include "enhanced/hr.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "enhanced/enh_canvas.h"
#include "render3d/r3d.h"

#define IDLE_L INT32_MAX
#define IDLE_R (INT32_MIN + 1)

static s32 *tab_l, *tab_r;
static int tab_rows;
s32 hr_miny = IDLE_L;
void hr_raster_regions(void (*add)(void *p, size_t n)) { add(&hr_miny, sizeof hr_miny); }

s32 *hr_tab_l(void) { return tab_l; }
s32 *hr_tab_r(void) { return tab_r; }

static s32 fdiv(s32 a, s32 n) { return a >= 0 ? a / n : -((-a + n - 1) / n); }

static bool tables_alloc(void)
{
    if (tab_rows == enh_hr.lh && tab_l) return true;
    free(tab_l); free(tab_r);
    tab_l = malloc(sizeof(s32) * (size_t)enh_hr.lh);
    tab_r = malloc(sizeof(s32) * (size_t)enh_hr.lh);
    if (!tab_l || !tab_r) { free(tab_l); free(tab_r); tab_l = tab_r = NULL; tab_rows = 0; return false; }
    tab_rows = enh_hr.lh;
    for (int i = 0; i < tab_rows; i++) { tab_l[i] = IDLE_L; tab_r[i] = IDLE_R; }
    return true;
}

/* The faithful row tables carry state from one polygon to the next (fill_M stops at the first idle row and leaves the rows below it:
 * the "gap row" behaviour), so a pass starts from the faithful tables, scaled: row r -> rows fy + N r .. + N - 1, L -> fx + N L,
 * R -> fx + N R + N - 1. At N = 1 this is a copy. */
void hr_raster_begin(void)
{
    if (!tables_alloc()) { enh_hr.on = false; return; }
    const int n = enh_hr.n;
    for (int i = 0; i < tab_rows; i++) { tab_l[i] = IDLE_L; tab_r[i] = IDLE_R; }
    for (int r = 0; r < 480; r++) {
        s16 L = R3D_L(2 * r), R = R3D_R(2 * r);
        if (L == 0x7FFF && R == (s16)0x8001) continue;
        for (int k = 0; k < n; k++) {
            int row = enh_hr.fy + n * r + k;
            if (row < 0 || row >= tab_rows) continue;
            tab_l[row] = L == 0x7FFF ? IDLE_L : enh_hr.fx + n * L;
            tab_r[row] = R == (s16)0x8001 ? IDLE_R : enh_hr.fx + n * R + n - 1;
        }
    }
    s16 my = R3D_W->edge_miny;
    hr_miny = my == 0x7FFF ? IDLE_L : enh_hr.fy + n * my;
}

/* ---------------------------------------------------------------- layer pixels */

void hr_put(s32 x, s32 y, u8 c) { enh_hr.layer[(size_t)y * enh_hr.lw + x] = (u16)(0x100 | c); }

u8 hr_get(s32 x, s32 y)
{
    u16 l = enh_hr.layer[(size_t)y * enh_hr.lw + x];
    if (l & 0x100) return (u8)l;
    s32 px = fdiv(x - enh_hr.fx, enh_hr.n), py = fdiv(y - enh_hr.fy, enh_hr.n);
    if (px < 0 || px > 319 || py < 0 || py > 199) return 0;
    return seg_ptr(enh_hr.seg)[py * 320 + px];
}

static inline int phase(s32 x, s32 y)
{
    s32 dx = x - enh_hr.fx, dy = y - enh_hr.fy;
    if (enh_hr.fine) return (int)((dx ^ dy) & 1);
    return (int)((fdiv(dx, enh_hr.n) ^ fdiv(dy, enh_hr.n)) & 1);
}

/* ---------------------------------------------------------------- 1359:0719 edge_dda */

static inline bool row_ok(s32 r) { return r >= 0 && r < tab_rows; }
#define TL(r) tab_l[r]
#define TR(r) tab_r[r]

void hr_edge_dda(const HrEdge *e)
{
    if (hr_tracing) { s32 a[4] = { e->x0, e->y0, e->x1, e->y1 }; hr_trace(1, 4, a); }
    s32 ax = e->x0, dx = e->x1, bx = e->y0, sv = e->y1, t;
    if (ax > dx) { t = ax; ax = dx; dx = t; t = sv; sv = bx; bx = t; }
    s32 di = bx;
    dx -= ax;
    s32 bp = 1;
    if (sv < bx) { bp = -1; t = bx; bx = sv; sv = t; }
    if (bx < enh_hr.ymin || bx > enh_hr.ymax || sv < enh_hr.ymin || sv > enh_hr.ymax) return;
    if (!row_ok(bx) || !row_ok(sv)) return;
    if (bx < hr_miny) hr_miny = bx;
    sv -= bx;
    if (sv <= dx) {                                         /* x-major */
        s32 cx = dx + 1, err = -(cx >> 1);
        if (ax < TL(di)) TL(di) = ax;
        for (;;) {
            err += sv;
            if (err >= 0) {
                if (ax > TR(di)) TR(di) = ax;
                err -= dx;
                cx--;
                if (cx == 0) return;
                ax++;
                di += bp;
                if (ax < TL(di)) TL(di) = ax;
            } else {
                ax++;
                cx--;
                if (cx == 0) {
                    ax--;
                    if (ax > TR(di)) TR(di) = ax;
                    return;
                }
            }
        }
    } else {                                                /* y-major */
        s32 cx = sv + 1, err = -(cx >> 1);
        for (;;) {
            if (ax < TL(di)) TL(di) = ax;
            if (ax > TR(di)) TR(di) = ax;
            err += dx;
            if (err >= 0) {
                cx--;
                if (cx == 0) return;
                err -= sv;
                ax++;
                di += bp;
            } else {
                di += bp;
                cx--;
                if (cx == 0) return;
            }
        }
    }
}

/* ---------------------------------------------------------------- 1359:07D6 wall_fill */

void hr_wall_fill(s32 a, s32 b, s32 x)
{
    if (hr_tracing) { s32 t[3] = { a, b, x }; hr_trace(2, 3, t); }
    s32 bx = a, cx = b;
    if (cx < bx) { s32 t = bx; bx = cx; cx = t; }
    if (cx <= enh_hr.ymin || bx >= enh_hr.ymax) return;
    if (bx < enh_hr.ymin) bx = enh_hr.ymin;
    if (cx > enh_hr.ymax) cx = enh_hr.ymax;
    if (cx == bx) return;
    if (bx < hr_miny) hr_miny = bx;
    for (s32 row = bx; row <= cx; row++) {
        if (!row_ok(row)) continue;
        if (x < TL(row)) TL(row) = x;
        if (x > TR(row)) TR(row) = x;
    }
}

/* ---------------------------------------------------------------- 1359:0AF7 fill_M */

typedef struct { s32 row; } Spans;
static bool spans_begin(Spans *s, bool unsigned_check)
{
    s32 r = hr_miny;
    if (r == IDLE_L || (unsigned_check && r < 0)) { hr_miny = IDLE_L; return false; }
    s->row = r;
    return true;
}
static bool spans_next(Spans *s, s32 *row, s32 *l, s32 *r)
{
    for (;;) {
        if (!row_ok(s->row)) { hr_miny = IDLE_L; return false; }
        s32 L = TL(s->row), R = TR(s->row);
        if (L == IDLE_L) { hr_miny = IDLE_L; return false; }
        *row = s->row;
        TL(s->row) = IDLE_L;
        TR(s->row) = IDLE_R;
        s->row++;
        if (!(L > enh_hr.xmax || R < enh_hr.xmin)) {
            *l = L > enh_hr.xmin ? L : enh_hr.xmin;
            *r = R < enh_hr.xmax ? R : enh_hr.xmax;
            return true;
        }
    }
}

void hr_fill(u16 colour)
{
    if (hr_tracing) { s32 t = colour; hr_trace(3, 1, &t); }
    u8 al = (u8)colour, ah = (u8)(colour >> 8);
    Spans sp;
    s32 y, l, r;
    if (ah < 0xFB || ah == 0xFF) {
        if (ah == 0xFF) ah = al;
        if (!spans_begin(&sp, false)) return;
        while (spans_next(&sp, &y, &l, &r))
            for (s32 x = l; x <= r; x++) hr_put(x, y, phase(x, y) ? ah : al);
    } else if (ah == 0xFE || ah == 0xFD) {
        if (!spans_begin(&sp, false)) return;
        while (spans_next(&sp, &y, &l, &r))
            for (s32 x = l; x <= r; x++) {
                u8 v = hr_get(x, y);
                hr_put(x, y, ah == 0xFE ? (u8)(v & al) : (u8)(v | al));
            }
    } else if (ah == 0xFC) {
        if (!spans_begin(&sp, true)) return;
        while (spans_next(&sp, &y, &l, &r))
            for (s32 x = l; x <= r; x++)
                if (phase(x, y)) hr_put(x, y, al);
    } else {                                                /* FBh: clear only */
        if (!spans_begin(&sp, false)) return;
        while (spans_next(&sp, &y, &l, &r)) {}
    }
}

/* ---------------------------------------------------------------- 1359:045D line_M */

static void brush(s32 x, s32 y, u8 c)
{
    const int w = enh_hr.line_w, o = enh_hr.line_o;
    for (int ky = 0; ky < w; ky++) {
        s32 py = y + o + ky;
        if (py < enh_hr.ymin || py > enh_hr.ymax) continue;
        for (int kx = 0; kx < w; kx++) {
            s32 px = x + o + kx;
            if (px >= enh_hr.xmin && px <= enh_hr.xmax) hr_put(px, py, c);
        }
    }
}

void hr_draw_line(s32 bx, s32 di, s32 bp, s32 cx, u8 c)
{
    if (hr_tracing) { s32 t[7] = { bx, di, bp, cx, c, enh_hr.line_w, enh_hr.line_o }; hr_trace(4, 7, t); }
    s32 ady = cx - di, adx = bp - bx, t;
    if (ady < 0) ady = -ady;
    if (adx < 0) adx = -adx;
    s32 x, y, n, err;
    if (ady <= adx) {                                       /* x-major */
        if (bp < bx) { t = bx; bx = bp; bp = t; t = di; di = cx; cx = t; }
        s32 step = cx < di ? -1 : 1;
        x = bx; y = di;
        n = bp - bx + 1;
        err = -(n >> 1);
        for (;;) {
            brush(x, y, c);
            x++;
            err += ady;
            if (err >= 0) { y += step; err -= adx; }
            if (--n == 0) return;
        }
    } else {                                                /* y-major */
        if (cx < di) { t = bx; bx = bp; bp = t; t = di; di = cx; cx = t; }
        s32 step = bp < bx ? -1 : 1;
        n = cx - di + 1;
        x = bx; y = di;
        err = -(n >> 1);
        for (;;) {
            brush(x, y, c);
            y++;
            err += adx;
            if (err >= 0) { x += step; err -= ady; }
            if (--n == 0) return;
        }
    }
}
