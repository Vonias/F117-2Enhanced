/* The objects drawn at 1x on top of the 3D scene, at N x (DESIGN.md 2.6, 2.8; S3.1): the sprites of the world pass 9ACA (smoke billboards, 1AD6) and of the
 * overlay pass A17E (the explosion, 1A85), the far unit / projectile dots (9ACA, one-pixel hud_line_view), the cannon tracers (A17E S2) and the target boxes
 * (B3E6) and diamond (B339).
 *
 * The faithful code runs unchanged: the object is drawn at 1x on the page and recorded in the shadow plane. A hook before it asks for a "hold"
 * (enh_stamp_hold / enh_vec_hold), so the 1x footprint is not stamped into the 3D layer and a vector record shows nothing where no hr pixel lands, and a hook after it
 * draws the same object from the hr point of B85D (enh_hr_pt: layer pixels, sub-pixel), at N x:
 *   sprites  the original sprite bitmap scaled to N times its size with the same column / row DDA (source k = floor(i sw / dw)), centred on the hr point, into the
 *            3D layer (so later polygons cover it and the shadow's "3D" rule shows it); the explosion is drawn after the pass has closed: its 1x writes are
 *            recorded as 3D writes and its hr pixels go into the layer of the last pass;
 *   dots     an N x N block centred on the hr point (the same apparent size as the 1x pixel) in the 3D layer;
 *   tracers, boxes, diamond  thin lines (max(1, N/2) layer pixels, Decisions Q3) in the vector layer, clipped with the port of the 1001 clip core, then the
 *            1x Bresenham of MGRAPHIC slot 31 on layer coordinates.
 * At N = 1 every formula is the original one (rule R1), so the layers equal the page. */
#include <stdlib.h>
#include <string.h>

#include "enhanced/enh_canvas.h"
#include "enhanced/hr.h"
#include "enhanced/wide.h"
#include "render3d/r3d.h"
#include "gen/module_video.h"   /* offset_get */
#include "video/video.h"

#define BILL_MAX 50

typedef struct { bool valid; s32 x, y, size; } Pending;
static struct { bool valid; HrPoint pt; s32 size; } bill[BILL_MAX];
static Pending pend;
static HrPoint tracer0;
static struct { long sprites, sprite_px, dots, lines, line_px; } st;

/* the held states of the call in progress (a hook pair never nests) */
static bool sp_route_changed, sp_active, dot_active, vec_active;
static int sp_saved_route;
static Pending sp_cur;

void hr_obj_stats(long *sprites, long *sprite_px, long *dots, long *lines, long *line_px, bool reset)
{
    *sprites = st.sprites; *sprite_px = st.sprite_px; *dots = st.dots; *lines = st.lines; *line_px = st.line_px;
    if (reset) memset(&st, 0, sizeof st);
}

void hr_obj_pass_begin(void)
{
    memset(bill, 0, sizeof bill);
    pend.valid = false;
    tracer0.valid = false;
}

static void put3d(s32 x, s32 y, u8 c)
{
    if (x < enh_hr.xmin || x > enh_hr.xmax || y < enh_hr.ymin || y > enh_hr.ymax || HR_IN_HOLE(x, y)) return;   /* S3.4: the hole */
    enh_hr.layer[(size_t)y * enh_hr.lw + x] = (u16)(0x100 | c);
    if (enh_hr.night) enh_hr.night[(size_t)y * enh_hr.lw + x] = enh_hr.on ? 0 : HR_NIGHT_LIT;   /* S3.6: in the scene (smoke, far dots) the factor of the place
                                                                                                   * (pass end); in the overlay pass after it (explosions, tracers,
                                                                                                   * flares, target boxes) a light */
}

/* ---------------------------------------------------------------- hr sizes */

/* lib_scale_eighths (C8FA) on an unwrapped value */
static s32 scale_eighths(int mode, s32 v)
{
    switch ((s16)mode) {
    case 0:  return v >> 1;
    case 1:  return (v >> 1) + (v >> 3);
    case 2:  return (v >> 1) + (v >> 2);
    case 3:  return (v >> 1) + (v >> 2) + (v >> 3);
    default: return v;
    }
}

void enh_bill_size(int mode, int d)
{
    if (d == 0) { enh_hr_pt.size = 0; return; }
    s32 q = (s32)(0x100 * enh_cv.n) / (s16)d;
    enh_hr_pt.size = scale_eighths(mode, q < 0 ? -q : q);
}

/* the record 14h-byte-aligned slot r3d_queue_insert (DDFE) is about to fill: 49F0 + 16h count, or the farthest record when the queue holds 50 */
void enh_bill_note(void)
{
    u16 e = DSW(0x49C4) == 50 ? DSW(0x4E3C) : (u16)(0x49F0 + 0x16 * DSW(0x49C4));
    if (e < 0x49F0 || (e - 0x49F0) % 0x16) return;
    unsigned k = (unsigned)(e - 0x49F0) / 0x16;
    if (k >= BILL_MAX) return;
    bill[k].valid = enh_hr.on && enh_hr_pt.valid;
    bill[k].pt = enh_hr_pt;
    s16 size1 = (s16)DSW(0x496E);
    bill[k].size = enh_hr_pt.size > 0 ? enh_hr_pt.size : (s32)size1 * enh_cv.n;
}

void enh_bill_pending(u16 rec)
{
    pend.valid = false;
    if (rec < 0x49F0 || (rec - 0x49F0) % 0x16) return;
    unsigned k = (unsigned)(rec - 0x49F0) / 0x16;
    if (k >= BILL_MAX || !bill[k].valid) return;
    pend.valid = true; pend.x = bill[k].pt.x; pend.y = bill[k].pt.y; pend.size = bill[k].size;
}

void enh_expl_pending(int dep)
{
    pend.valid = false;
    if (!enh_hr.have || !enh_hr_pt.valid || (s16)dep == 0) return;
    s32 q = (s32)(0x100 * enh_cv.n) / (s16)dep;
    pend.valid = true; pend.x = enh_hr_pt.x; pend.y = enh_hr_pt.y; pend.size = q < 0 ? -q : q;
}

/* S3.2: the TV camera's fire puffs and smoke (objects 24h..: r3d_draw_queued draws their model, then a sprite of size `size` centred on the last edge's first end).
 * The hr point is that end in the hr edge record; the size is N times the 1x one. */
void enh_tv_sprite_pending(u16 rec, int size)
{
    pend.valid = false;
    if (!enh_hr.on || size <= 0 || rec < R3D_EDGES || (rec - R3D_EDGES) % 0x18) return;
    s32 x = 0, y = 0;
    hr_edge_point(rec, &x, &y);
    pend.valid = true;
    pend.x = x - enh_hr.wx + enh_hr.fx; pend.y = y - enh_hr.wy + enh_hr.fy;
    pend.size = (s32)size * enh_cv.n;
}

/* ---------------------------------------------------------------- sprites (1AD6 / 1A85) */

bool enh_sprite_begin(u16 ctx)
{
    sp_active = false;
    if (!pend.valid) return false;
    Pending p = pend;
    pend.valid = false;
    if (!enh_hr.have || !enh_layer3d() || (ctx != DSW(0x0344) && ctx != DSW(0x4042)) || p.size <= 0) return false;   /* S3.2: the TV window's context too */
    sp_cur = p;
    sp_active = true;
    enh_stamp_hold++;
    sp_route_changed = false;
    if (!enh_hr.on) { sp_saved_route = enh_route; enh_route = ENH_ROUTE_3D; sp_route_changed = true; }   /* the overlay pass: its 1x writes are scene writes */
    return true;
}

/* The sprite pool format (MGRAPHIC sprite_grab): header (u16 width, u16 height) at off, then per row (u16 len, u16 lead, len bytes), 0 = transparent. */
void enh_sprite_end(u16 off, u16 seg, int size)
{
    if (!sp_active) return;
    sp_active = false;
    enh_stamp_hold--;
    if (sp_route_changed) enh_route = sp_saved_route;
    const u8 *hdr = mp(seg, 0);
    if (seg == 0 || !(hdr[0] | hdr[1]) || !(hdr[2] | hdr[3]) || size <= 0) return;
    const u16 sw = rd16(seg, off), sh = rd16(seg, (u16)(off + 2));
    if (sw == 0 || sh == 0 || sh > 1024) return;
    const s32 dw = sp_cur.size, dh = dw - (dw >> 2);
    if (dw > 8192) return;
    static const u8 *rows[1024];
    u16 si = (u16)(off + 4);
    for (int r = 0; r < sh; r++) {
        rows[r] = mp(seg, si);
        si = (u16)(si + rd16(seg, si) + 4);
    }
    /* the origin: the window's layer origin + the hr point - half the size (the 1x call is x - size / 2, y - dh / 2 inside the window) */
    const s32 x0 = sp_cur.x + (enh_hr.wx - enh_hr.fx) - dw / 2, y0 = sp_cur.y + (enh_hr.wy - enh_hr.fy) - dh / 2;   /* S3.4: wx, wy (= xmin, ymin but in a wide pass) */
    static u8 rowbuf[0x400];
    long px = 0;
    if (hr_tracing) {                                 /* 7: x0 y0 dw dh sw sh and the source bitmap (sw sh words), for the reference */
        s32 *t = malloc(sizeof(s32) * (size_t)(6 + sw * sh));
        if (t) {
            t[0] = x0; t[1] = y0; t[2] = dw; t[3] = dh; t[4] = sw; t[5] = sh;
            for (int r = 0; r < sh; r++) {
                const u8 *rec = rows[r];
                const u16 len = (u16)(rec[0] | rec[1] << 8), lead = (u16)(rec[2] | rec[3] << 8);
                for (int i = 0; i < sw; i++) t[6 + r * sw + i] = (i >= lead && i < lead + len) ? rec[4 + i - lead] : 0;
            }
            hr_trace(7, 6 + sw * sh, t);
            free(t);
        }
    }
    for (s32 j = 0; j < dh; j++) {
        const s32 ly = y0 + j;
        if (ly < enh_hr.ymin || ly > enh_hr.ymax) continue;
        const u8 *rec = rows[(s64)j * sh / dh];
        const u16 len = (u16)(rec[0] | rec[1] << 8), lead = (u16)(rec[2] | rec[3] << 8);
        if (len == 0 || (unsigned)lead + len > sw || sw > sizeof rowbuf) continue;
        memset(rowbuf, 0, sw);
        memcpy(rowbuf + lead, rec + 4, len);
        for (s32 i = 0; i < dw; i++) {
            const s32 lx = x0 + i;
            if (lx < enh_hr.xmin || lx > enh_hr.xmax) continue;
            const u8 a = rowbuf[(s64)i * sw / dw];
            if (!a) continue;
            put3d(lx, ly, a);
            px++;
        }
    }
    st.sprites++;
    st.sprite_px += px;
}

/* ---------------------------------------------------------------- the far dot */

void enh_dot_begin(void)
{
    dot_active = enh_hr.on && enh_hr_pt.valid && enh_layer3d();
    if (dot_active) enh_stamp_hold++;
}

void enh_dot_end(int colour)
{
    if (!dot_active) return;
    dot_active = false;
    enh_stamp_hold--;
    const int n = enh_cv.n;
    const s32 x0 = enh_hr_pt.x - n / 2, y0 = enh_hr_pt.y - n / 2;
    if (hr_tracing) { s32 t[4] = { x0, y0, n, colour }; hr_trace(8, 4, t); }       /* 8: an N x N block */
    for (int ky = 0; ky < n; ky++)
        for (int kx = 0; kx < n; kx++) put3d(x0 + kx, y0 + ky, (u8)colour);
    st.dots++;
}

/* ---------------------------------------------------------------- vector lines */

/* The 1001 clip core (0336, spec render3d.md A.8) on layer coordinates relative to the window origin: Cohen-Sutherland with the idiv and its rounding block,
 * ported with 64-bit arithmetic (no 16-bit overflow cases: the endpoints are screen points). false = rejected. */
static s64 rdiv(s64 num, s64 D, s64 h)
{
    if (D == 0) return num < 0 ? -(1 << 20) : (1 << 20);
    s64 q = num / D, r = num % D;
    if ((num < 0) != (D < 0)) { r = -r; q = q - 1; }
    r = r - h;
    if ((r < 0) == (num < 0)) q = q + 1;
    return q;
}

static int outcode(s64 x, s64 y, s64 xmax, s64 ymax) { return (x < 0 ? 8 : 0) | (x > xmax ? 1 : 0) | (y < 0 ? 4 : 0) | (y > ymax ? 2 : 0); }

static bool clip_line(s64 *px0, s64 *py0, s64 *px1, s64 *py1, s64 xmax, s64 ymax)
{
    s64 cx = *px0, dx = *py0, si = *px1, di = *py1, sx = 0, sy = 0;
    bool have_start = false;
    int oc = outcode(cx, dx, xmax, ymax), al = outcode(si, di, xmax, ymax);
    if (al == 0) {
        if (oc == 0) return true;
        s64 t = si; si = cx; cx = t;
        t = di; di = dx; dx = t;
        int tm = oc; oc = al; al = tm;
    }
    s64 bp = dx;
    if (oc & al) return false;
    const s64 Dx = si - cx, Dy = di - bp, hx = Dx >> 1, hy = Dy >> 1;
    for (int guard = 0; guard < 3; guard++) {
        s64 rx, ry, bx = 0;
        if (al & 9) {
            bx = si < 0 ? 0 : xmax;
            s64 y = rdiv((bx - cx) * Dy, Dx, hx) + bp;
            if (y >= 0 && y <= ymax) { rx = bx; ry = y; goto got; }
            bx = y < 0 ? 0 : (di > ymax ? ymax : 0);
        } else {
            bx = di > ymax ? ymax : 0;
        }
        {
            s64 x = rdiv((bx - bp) * Dx, Dy, hy) + cx;
            if (x < 0 || x > xmax) return false;
            rx = x; ry = bx;
        }
    got:
        if (oc == 0) {                                   /* the other end is inside (or already clipped): done */
            if (have_start) { *px0 = sx; *py0 = sy; } else { *px0 = cx; *py0 = bp; }
            *px1 = rx; *py1 = ry;
            return true;
        }
        sx = rx; sy = ry; have_start = true;             /* both ends outside: this end is clipped; clip the other one */
        { s64 t = si; si = cx; cx = t; t = di; di = bp; bp = t; }
        al = oc; oc = 0;
    }
    return false;
}

static int vl_w, vl_o;   /* the brush of the line being drawn */

static void vbrush(s32 x, s32 y, u8 c, s32 wx0, s32 wy0, s32 wx1, s32 wy1)
{
    for (int ky = 0; ky < vl_w; ky++)
        for (int kx = 0; kx < vl_w; kx++) {
            s32 px = x + vl_o + kx, py = y + vl_o + ky;
            if (px < wx0 || px > wx1 || py < wy0 || py > wy1) continue;
            enh_vec_put(px, py, c);
            st.line_px++;
        }
}

/* MGRAPHIC line (slot 31) on layer coordinates, with the brush: x1 <= x2 first, Bresenham from the left end, the major axis steps every pixel */
static void vbres(s32 x1, s32 y1, s32 x2, s32 y2, u8 c, s32 wx0, s32 wy0, s32 wx1, s32 wy1)
{
    if (x1 > x2) { s32 t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    s32 cx = x2 - x1, dy = y2 - y1, ystep = 1;
    if (dy < 0) { ystep = -1; dy = -dy; }
    if (x1 == x2 && y1 == y2) { vbrush(x1, y1, c, wx0, wy0, wx1, wy1); return; }
    s32 x = x1, y = y1, major, minor, e;
    const bool xmajor = !(cx < dy);
    major = xmajor ? cx : dy;
    minor = xmajor ? dy : cx;
    e = -((major + 1) >> 1);
    for (s32 i = major; i >= 0; i--) {
        vbrush(x, y, c, wx0, wy0, wx1, wy1);
        if (i == 0) break;
        if (xmajor) x++; else y += ystep;
        e += minor;
        if (e >= 0) { e -= major; if (xmajor) y += ystep; else x++; }
    }
}

/* One segment of an outline, in layer pixels, clipped to the layer window [ox, wx1] x [oy, wy1] */
static void vseg_layer(s32 x0, s32 y0, s32 x1, s32 y1, u8 c, s32 ox, s32 oy, s32 wx1, s32 wy1)
{
    s64 ax = x0 - ox, ay = y0 - oy, bx = x1 - ox, by = y1 - oy;
    if (!clip_line(&ax, &ay, &bx, &by, wx1 - ox, wy1 - oy)) return;
    if (hr_tracing) { s32 t[11] = { (s32)(ax + ox), (s32)(ay + oy), (s32)(bx + ox), (s32)(by + oy), c, vl_w, vl_o, ox, oy, wx1, wy1 }; hr_trace(9, 11, t); }   /* 9: a clipped vector line + its window */
    vbres((s32)(ax + ox), (s32)(ay + oy), (s32)(bx + ox), (s32)(by + oy), c, ox, oy, wx1, wy1);
}

/* ... clipped to the 1x window [wxmin, wxmax] x [wymin, wymax] (frame pixels) */
static void vseg(s32 x0, s32 y0, s32 x1, s32 y1, u8 c, int wxmin, int wxmax, int wymin, int wymax)
{
    const int n = enh_cv.n;
    vseg_layer(x0, y0, x1, y1, c, enh_cv.fx + n * wxmin, enh_cv.fy + n * wymin, enh_cv.fx + n * (wxmax + 1) - 1, enh_cv.fy + n * (wymax + 1) - 1);
}

static void vec_hold_begin(void)
{
    vec_active = true;
    enh_vec_hold++;
    vl_w = enh_hr.line_w; vl_o = enh_hr.line_o;
}

static void vec_hold_end(void)
{
    vec_active = false;
    enh_vec_hold--;
}

/* ---------------------------------------------------------------- tracers (A17E S2) */

void enh_tracer_p0(void) { tracer0 = enh_hr_pt; }

bool enh_tracer_begin(void)
{
    vec_active = false;
    if (!enh_hr.have || !tracer0.valid || !enh_hr_pt.valid || !enh_layer_vec()) return false;
    vec_hold_begin();
    return true;
}

void enh_tracer_end(int colour)
{
    if (!vec_active) return;
    vec_hold_end();
    const TextCtx *c = (const TextCtx *)DSP(DSW(0x3FE2));    /* hud_line_view's window: ctx0 */
    if (enh_hr.wide && enh_wide_pt)                          /* S3.4: in a wide pass the tracer is clipped to the canvas (the compositor takes the pass's vector layer outside the frame only) */
        vseg_layer(enh_hr_pt.x, enh_hr_pt.y, tracer0.x, tracer0.y, (u8)colour, 0, 0, enh_cv.lw - 1, enh_cv.lh - 1);
    else vseg(enh_hr_pt.x, enh_hr_pt.y, tracer0.x, tracer0.y, (u8)colour, c->xmin, c->xmax, c->ymin, c->ymax);
    st.lines++;
}

/* ---------------------------------------------------------------- target boxes (B3E6), diamond (B339) */

bool enh_box_begin(void)
{
    vec_active = false;
    if (!enh_hr.have || !enh_layer_vec()) return false;
    vec_hold_begin();
    return true;
}

/* the centre in layer px: the hr point when it belongs to this (x, y) (the box follows the last B85D), else the middle of the 1x pixel */
static void centre(int x, int y, s32 *cx, s32 *cy)
{
    const int n = enh_cv.n;
    if (enh_hr_pt.valid && abs(enh_hr_pt.x - (enh_cv.fx + n * x)) < n && abs(enh_hr_pt.y - (enh_cv.fy + n * y)) < n) { *cx = enh_hr_pt.x; *cy = enh_hr_pt.y; return; }
    *cx = enh_cv.fx + n * x + (n - 1) / 2; *cy = enh_cv.fy + n * y + (n - 1) / 2;
}

static void hud_window(int *x0, int *x1, int *y0, int *y1)   /* hud_line_win */
{
    if (DSB(0x294B)) { *x0 = 0x63; *x1 = 0xDC; *y0 = 0x1E; *y1 = 0x5A; }
    else { *x0 = 0x30; *x1 = 0x10F; *y0 = 0x0F; *y1 = 0x5A; }
}

void enh_box_end(int x, int y, int sz, int hex, int colour)
{
    if (!vec_active) return;
    vec_hold_end();
    const int n = enh_cv.n;
    int wx0, wx1, wy0, wy1;
    hud_window(&wx0, &wx1, &wy0, &wy1);
    s32 cx, cy;
    centre(x, y, &cx, &cy);
    const s16 t = (s16)(sz - (s16)(sz >> 2));
    const s32 R = cx + n * sz, L = cx - n * sz, B = cy + n * t, T = cy - n * t;
    const u8 c = (u8)colour;
#define SEG(a, b, d, e) vseg((a), (b), (d), (e), c, wx0, wx1, wy0, wy1)
    if (hex == 0) {
        SEG(L, T, L, B); SEG(L, B, R, B); SEG(R, B, R, T); SEG(R, T, L, T);
    } else {
        const s16 q = (s16)(t >> 1);
        const s32 si = cy - n * q, di = cy + n * q;
        SEG(cx, T, R, si); SEG(R, si, R, di); SEG(R, di, cx, B); SEG(cx, B, L, di); SEG(L, di, L, si); SEG(L, si, cx, T);
    }
#undef SEG
    st.lines++;
}

void enh_diamond_end(int sx, int sy, int colour)
{
    if (!vec_active) return;
    vec_hold_end();
    const int n = enh_cv.n;
    int wx0, wx1, wy0, wy1;
    hud_window(&wx0, &wx1, &wy0, &wy1);
    s32 cx, cy;
    centre(sx, sy, &cx, &cy);
    const s32 d = 4 * n;
    const u8 c = (u8)colour;
    vseg(cx - d, cy, cx, cy - d, c, wx0, wx1, wy0, wy1);
    vseg(cx, cy - d, cx + d, cy, c, wx0, wx1, wy0, wy1);
    vseg(cx + d, cy, cx, cy + d, c, wx0, wx1, wy0, wy1);
    vseg(cx, cy + d, cx - d, cy, c, wx0, wx1, wy0, wy1);
    st.lines++;
}

/* ---------------------------------------------------------------- the HUD and MFD lines (S3.3) */

/* Every 1x vector line of the cockpit (MGRAPHIC line(), slot 31: the HUD frame, tapes' reference marks, pitch ladder, reticle, ILS bars, the map MFD's route lines, arcs and
 * symbols, the scope, the TV crosshair) and the tick strip (hud_tick_strip, 071A) is drawn again here at N x: a thin line of max(1, N/2) layer pixels (Decisions Q3) in the vector
 * layer from the 1x endpoints. begin (before the 1x pixels are written) decides and holds the footprint (enh_vec_hold: the 1x pixels are SRC_VECHR records, which show what was
 * under them where no hr pixel lands); end draws the hr line. Not taken (the 1x pixels stay the original's, N x N blocks): colour 0 (the HUD erases with it), a single point (the
 * map's 2x2 blobs and the dots keep their solid look), lines of the 3D passes (they are 3D-layer geometry), and lines inside another emitter's hold (tracers, boxes). Axis-aligned
 * lines keep the full length of the 1x line (block edge to block edge, so ticks and frame corners join); diagonals run between the block centres. */
static struct { int x1, y1, x2, y2, c; } hl;
bool enh_hud_hr_off;

static bool hud_ok(u16 seg, int colour)
{
    return !enh_hud_hr_off && colour != 0 && enh_route == ENH_ROUTE_ART && enh_stamp_hold <= 0 && enh_vec_hold <= 0 && enh_page_tracked(seg) && enh_layer_vec() && enh_cv.lw > 0;
}

/* one line in layer pixels with the brush: the window is the frame; trace type 9 for the reference rasteriser */
static void hud_seg(s32 x0, s32 y0, s32 x1, s32 y1, u8 c)
{
    const int n = enh_cv.n;
    const s32 wx0 = enh_cv.fx, wy0 = enh_cv.fy, wx1 = enh_cv.fx + 320 * n - 1, wy1 = enh_cv.fy + 200 * n - 1;
    vl_w = n / 2 > 1 ? n / 2 : 1;
    vl_o = (n - vl_w) / 2;
    if (hr_tracing && !(video_draw_seg() == 0xA000 && video_page_seg(1) != 0xA000)) { s32 t[11] = { x0, y0, x1, y1, c, vl_w, vl_o, wx0, wy0, wx1, wy1 }; hr_trace(9, 11, t); }   /* (the reference judges the work layer: a copy on page 0 goes to the shown one) */
    vbres(x0, y0, x1, y1, c, wx0, wy0, wx1, wy1);
}

/* the frame pixels (xa..xb, ya..yb) of an axis-aligned run, as one line whose brush covers it block edge to block edge */
static void hud_run(int xa, int ya, int xb, int yb, u8 c)
{
    const int n = enh_cv.n, w = n / 2 > 1 ? n / 2 : 1, o = (n - w) / 2;
    if (xa > xb) { int t = xa; xa = xb; xb = t; }
    if (ya > yb) { int t = ya; ya = yb; yb = t; }
    if (ya == yb) hud_seg(enh_cv.fx + n * xa - o, enh_cv.fy + n * ya, enh_cv.fx + n * (xb + 1) - o - w, enh_cv.fy + n * ya, c);    /* horizontal: thin across, full along */
    else          hud_seg(enh_cv.fx + n * xa, enh_cv.fy + n * ya - o, enh_cv.fx + n * xa, enh_cv.fy + n * (yb + 1) - o - w, c);    /* vertical */
}

bool enh_hud_begin(u16 seg, int colour, int x1, int y1, int x2, int y2, u16 draw_off)
{
    if (!hud_ok(seg, colour)) return false;
    const unsigned o1 = (u16)(draw_off + (u16)y1 * 320u + (u16)x1), o2 = (u16)(draw_off + (u16)y2 * 320u + (u16)x2);
    if (o1 >= 64000 || o2 >= 64000) return false;
    const int ax = (int)(o1 % 320), ay = (int)(o1 / 320), bx = (int)(o2 % 320), by = (int)(o2 / 320);
    if (bx - ax != x2 - x1 || by - ay != y2 - y1) return false;       /* the line wrapped over a row: not a clipped HUD line, leave it */
    if (ax == bx && ay == by) return false;                           /* a point */
    hl.x1 = ax; hl.y1 = ay; hl.x2 = bx; hl.y2 = by; hl.c = colour;
    enh_vec_hold++;
    return true;
}

void enh_hud_end_line(void)
{
    enh_vec_hold--;
    const u8 c = (u8)hl.c;
    if (hl.x1 == hl.x2 || hl.y1 == hl.y2) hud_run(hl.x1, hl.y1, hl.x2, hl.y2, c);
    else {
        const int n = enh_cv.n;
        hud_seg(enh_cv.fx + n * hl.x1, enh_cv.fy + n * hl.y1, enh_cv.fx + n * hl.x2, enh_cv.fy + n * hl.y2, c);
    }
    st.lines++;
}

bool enh_hud_begin_tick(u16 seg, int colour)
{
    if (!hud_ok(seg, colour)) return false;
    hl.c = colour;
    enh_vec_hold++;
    return true;
}

void enh_hud_tick(int x, int y, int dir, int npx)
{
    const u8 c = (u8)hl.c;
    if (npx >= 2) { hud_run(x, y, x + dir * (npx - 1), y, c); st.lines++; return; }
    /* one pixel of the scale line: a dash as tall as the 1x pixel, thin across */
    const int n = enh_cv.n, w = n / 2 > 1 ? n / 2 : 1, o = (n - w) / 2;
    hud_seg(enh_cv.fx + n * x, enh_cv.fy + n * y - o, enh_cv.fx + n * x, enh_cv.fy + n * (y + 1) - o - w, c);
    st.lines++;
}

void enh_hud_end_tick(void) { enh_vec_hold--; }

/* ---------------------------------------------------------------- the ADI horizon polygon (S3.3) */

/* cockpit_draw_adi (17A3) fills a quadrilateral (the ground half of the instrument, vertices in window pixels, window 33 x 29 at the draw offset) with colour 1 over the
 * background. begin holds the 1x footprint (poly_fill records it as a vector footprint); end fills the same polygon at N x in the vector layer, the edges through the
 * centres of the vertex pixels (a straight horizon edge instead of the 1x staircase) and a layer pixel in when its centre is inside or on an edge (trace type 11). */
static u8 adi_c;

bool enh_adi_begin(u16 seg, int colour)
{
    if (enh_cv.n < 2 || !hud_ok(seg, colour)) return false;
    adi_c = (u8)colour;
    enh_vec_hold++;
    return true;
}

void enh_adi_end(const s16 *pts, int n)
{
    enh_vec_hold--;
    const int N = enh_cv.n;
    const unsigned off = offset_get();
    const int ox = (int)(off % 320), oy = (int)(off / 320), ww = (s16)DSW(0x2293) + 1, wh = (s16)DSW(0x2295) + 1;
    const s32 wx0 = enh_cv.fx + N * ox, wy0 = enh_cv.fy + N * oy, wx1 = enh_cv.fx + N * (ox + ww) - 1, wy1 = enh_cv.fy + N * (oy + wh) - 1;
    if (n > 8) n = 8;
    s64 vx[8], vy[8];
    s32 t[6 + 16] = { adi_c, n, wx0, wy0, wx1, wy1 };
    for (int i = 0; i < n; i++) {                                   /* doubled layer coordinates of the vertex pixel centres */
        vx[i] = 2 * (s64)(enh_cv.fx + N * (ox + pts[2 * i])) + N;
        vy[i] = 2 * (s64)(enh_cv.fy + N * (oy + pts[2 * i + 1])) + N;
        t[6 + 2 * i] = (s32)vx[i]; t[7 + 2 * i] = (s32)vy[i];
    }
    if (hr_tracing) hr_trace(11, 6 + 2 * n, t);
    s64 area = 0;
    for (int i = 0; i < n; i++) area += vx[i] * vy[(i + 1) % n] - vx[(i + 1) % n] * vy[i];
    if (area == 0) return;
    const int sg = area > 0 ? 1 : -1;
    for (s32 ly = wy0; ly <= wy1; ly++)
        for (s32 lx = wx0; lx <= wx1; lx++) {
            const s64 px = 2 * (s64)lx + 1, py = 2 * (s64)ly + 1;
            bool in = true;
            for (int i = 0; i < n && in; i++) {
                const int j = (i + 1) % n;
                if (sg * ((vx[j] - vx[i]) * (py - vy[i]) - (vy[j] - vy[i]) * (px - vx[i])) < 0) in = false;
            }
            if (in) { enh_vec_put(lx, ly, adi_c); st.line_px++; }
        }
    st.lines++;
}
