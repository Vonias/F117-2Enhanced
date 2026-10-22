/* The wider view: the widening of the culling, the terrain cells and B85D in the main 3D pass of a wide display pass (wide.h; DESIGN.md 3.3; S3.4). */
#include "enhanced/wide.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "enhanced/enh_canvas.h"
#include "enhanced/enhanced.h"
#include "enhanced/hr.h"

bool enh_wide_pass, enh_wide_on, enh_wide_pt, enh_wide_drawn;
int enh_wide_win[4];

/* the factors of the open wide pass (Q8 fixed point: 256 = 1) and the screen range in frame pixels */
static struct { s32 kx, ky, tx, ty, xf; int xlo, xhi, ylo, yhi; s16 down_thr; } wf;

static int cdiv(int a, int n) { return a <= 0 ? 0 : (a + n - 1) / n; }

void enh_wide_begin(void)
{
    const HrCtx *h = &enh_hr;
    const int n = h->n;
    const int ext_l = cdiv(h->cx, n), ext_r = cdiv(h->lw - 1 - h->cx, n);       /* 1x pixels from the projection centre to the canvas edges */
    const int ext_u = cdiv(h->cy, n), ext_d = cdiv(h->lh - 1 - h->cy, n);
    const int mx = ext_l > ext_r ? ext_l : ext_r, my = ext_u > ext_d ? ext_u : ext_d;
    int up0 = (s16)DSW(0x0D20);                                                    /* the original half height above the centre: 52 (60 with [294B]) or 100 */
    if (up0 < 1) up0 = 1;
    /* E8B5: the tolerances were made for a half width of 160 and a half height of up0 (DESIGN 3.3: kx = (160 + E) / 160, ky = (up0 + E) / up0) */
    wf.kx = mx * 256 / 160 > 256 ? (mx * 256 + 159) / 160 : 256;
    wf.ky = my * 256 / up0 > 256 ? (my * 256 + up0 - 1) / up0 : 256;
    /* 02B6 and B85D test a 45-degree field (tangent 1); the extended half angles have the tangents mx / 256 (x) and my / 192 (y), with [294B] (the second cockpit
     * view: half-scale projection, 128 / 96 px per unit) mx / 128 and my / 96 */
    const int sx = DSB(0x294B) ? 128 : 256, sy = DSB(0x294B) ? 96 : 192;
    wf.tx = mx > sx ? (mx * 256 + sx - 1) / sx : 256;
    wf.ty = my * 256 / sy > 256 ? (my * 256 + sy - 1) / sy : 256;
    wf.xf = mx > 256 ? mx : 256;                               /* B85D halves X for [294B] before its 256-scale projection: its field is mx / 256 in both styles */
    wf.xlo = -cdiv(h->fx, n);
    wf.xhi = 319 + cdiv(h->lw - h->fx - 320 * n, n);
    wf.ylo = -cdiv(h->fy, n);
    wf.yhi = cdiv(h->lh - h->fy, n) - 1;
    /* 04DA: the full 5x5 cells (instead of the forward half) when the lower edge of the view looks behind the camera, or past 60 degrees down as in the original */
    double a = atan((double)ext_d / (double)sy) * 32768.0 / 3.14159265358979;                        /* the lower half angle in the 16-bit angle unit (4000h = 90 degrees) */
    int thr = -(0x4000 - (int)a);
    wf.down_thr = (s16)(thr > (s16)0xD555 ? thr : (s16)0xD555);
    enh_wide_on = enh_wide_pt = true;
    enh_wide_drawn = true;
    enh_wide_win[0] = h->x0; enh_wide_win[1] = h->y0; enh_wide_win[2] = h->x1; enh_wide_win[3] = h->y1;
}

void enh_wide_end(void) { enh_wide_on = enh_wide_pt = false; }

void enh_wide_ovl(bool on) { enh_wide_pt = on && enh_wide_pass && enh_hr.have && enh_hr.wide; }

s32 enh_wide_kx(s16 sx) { return (s32)(((s64)sx * wf.kx) >> 8); }
s32 enh_wide_ky(s16 sy) { return (s32)(((s64)sy * wf.ky) >> 8); }

void enh_wide_frustum(u32 *tx, u32 *ty)
{
    *tx = (u32)(((u64)*tx * (u64)wf.tx) >> 8);
    *ty = (u32)(((u64)*ty * (u64)wf.ty) >> 8);
}

bool enh_wide_xfield(s32 X, s32 nz)
{
    s64 ax = X < 0 ? -(s64)X : (s64)X;
    return ax * 256 <= (s64)nz * wf.xf;
}

void enh_wide_range(int *xlo, int *xhi, int *ylo, int *yhi)
{
    if (wf.xlo < *xlo) *xlo = wf.xlo;
    if (wf.xhi > *xhi) *xhi = wf.xhi;
    if (wf.ylo < *ylo) *ylo = wf.ylo;
    if (wf.yhi > *yhi) *yhi = wf.yhi;
}

/* ---------------------------------------------------------------- 04DA, the wide cell lists */

typedef struct { s8 ox, oy; u8 ii; } Cell;
static Cell lists[3][8][32];       /* [0 path B, 1 path A, 2 down-look][octant] */
static int list_n[3][8];
static bool lists_ready;

static const int dirx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, diry[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };   /* the octant's forward direction (oct 0 = +y) */

/* base = the original cells in their order; the new cells are the cells of the 5x5 block with dot >= dmin (the forward half; the whole block for the down-look)
 * that the base does not visit, ordered by dot (far first), the outer ones first; each is put before the first base cell that is not farther, so the list stays
 * far -> near and the original cells keep their order (the quirky tail of path A after the camera cell included) */
static int build(Cell *out, const s8 (*base)[2], int nb, int dx, int dy, int dmin, bool ring, int far_min)
{
    Cell nw[25];
    int nn = 0;
    for (int y = -2; y <= 2; y++)
        for (int x = -2; x <= 2; x++) {
            bool seen = false;
            for (int k = 0; k < nb && !seen; k++) seen = base[k][0] == x && base[k][1] == y;
            if (seen) continue;
            int d = ring ? (abs(x) > abs(y) ? abs(x) : abs(y)) : x * dx + y * dy;      /* down-look: the ring distance from the camera cell */
            if (d < dmin) continue;
            nw[nn].ox = (s8)x; nw[nn].oy = (s8)y;
            nw[nn].ii = (u8)(ring || d >= far_min ? 0 : 8);
            nn++;
        }
    int dot[25], side[25];
    for (int k = 0; k < nn; k++) {
        dot[k] = ring ? (abs(nw[k].ox) > abs(nw[k].oy) ? abs(nw[k].ox) : abs(nw[k].oy)) + 8 : nw[k].ox * dx + nw[k].oy * dy;
        side[k] = abs(nw[k].ox * dy - nw[k].oy * dx);
    }
    for (int a = 0; a < nn; a++)                                                 /* sort: dot descending, then the outer side first */
        for (int b = a + 1; b < nn; b++)
            if (dot[b] > dot[a] || (dot[b] == dot[a] && side[b] > side[a])) {
                Cell t = nw[a]; nw[a] = nw[b]; nw[b] = t;
                int u = dot[a]; dot[a] = dot[b]; dot[b] = u;
                u = side[a]; side[a] = side[b]; side[b] = u;
            }
    int m = 0, j = 0;
    for (int k = 0; k < nb; k++) {
        int db = ring ? (abs(base[k][0]) > abs(base[k][1]) ? abs(base[k][0]) : abs(base[k][1])) : base[k][0] * dx + base[k][1] * dy;
        while (j < nn && dot[j] >= db) out[m++] = nw[j++];
        out[m].ox = base[k][0]; out[m].oy = base[k][1]; out[m].ii = (u8)k;
        m++;
    }
    while (j < nn) out[m++] = nw[j++];
    return m;
}

static void lists_build(void)
{
    for (int oct = 0; oct < 8; oct++) {
        s8 base[15][2];
        for (int i = 0; i < 15; i++) {                                           /* DS:034C as 04DA reads it (path A reads 15, past the octant's row: quirk) */
            base[i][0] = (s8)DSS((u16)(0x034C + 2 * (oct * 9 + i)));
            base[i][1] = (s8)DSS((u16)(0x034C + 2 * (((oct + 2) & 7) * 9 + i)));
        }
        int far_min = 99;
        for (int i = 0; i < 4; i++) { int d = base[i][0] * dirx[oct] + base[i][1] * diry[oct]; if (d < far_min) far_min = d; }
        list_n[0][oct] = build(lists[0][oct], (const s8 (*)[2])base, 9, dirx[oct], diry[oct], 0, false, far_min);
        list_n[1][oct] = build(lists[1][oct], (const s8 (*)[2])base, 15, dirx[oct], diry[oct], 0, false, 99);
        s8 down[9][2];
        for (int i = 0; i < 9; i++) { down[i][0] = (s8)DSS((u16)(0x04CC + 2 * i)); down[i][1] = (s8)DSS((u16)(0x04DE + 2 * i)); }
        list_n[2][oct] = build(lists[2][oct], (const s8 (*)[2])down, 9, 0, 0, 0, true, 0);
    }
    lists_ready = true;
}

int enh_wide_cell(bool pathA, s16 pitch, int oct, int i, s16 *ox, s16 *oy)
{
    if (!lists_ready) lists_build();
    const int kind = pathA ? 1 : pitch < wf.down_thr ? 2 : 0;
    oct &= 7;
    if (i < 0 || i >= list_n[kind][oct]) return -1;
    const Cell *c = &lists[kind][oct][i];
    *ox = c->ox; *oy = c->oy;
    return c->ii;
}

/* ---------------------------------------------------------------- the decision per frame (sandbox.c) */

bool enh_wide_due(void)
{
    if (!enh.wide || !enh_canvas_on() || !enh_canvas_active()) return false;
    const u16 mode = DSW(0xC07A);
    if (enh_view_class((int)mode, DSB(0x294B)) == ENH_VIEW_FIT || enh_view_class((int)mode, DSB(0x294B)) == ENH_VIEW_MENU) return false;
    const EnhView v = enh_view_get();
    if (v != ENH_VIEW_FRONT && v != ENH_VIEW_FULL && v != ENH_VIEW_SIDE) return false;   /* the canvas still has another view's placement (it follows at the next present) */
    return enh_cv.vw > 320 * enh_cv.n || enh_cv.vh > 200 * enh_cv.n;
}
