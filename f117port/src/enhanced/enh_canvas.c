/* The Enhanced canvas, layers, shadow plane and compositor (see enh_canvas.h; DESIGN.md 2.1, 2.5, 3.1, 3.2). */
#include "enhanced/enh_canvas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enhanced/enhanced.h"
#include "enhanced/hr.h"
#include "enhanced/sandbox.h"
#include "render3d/r3d.h"
#include "platform/vga.h"
#include "video/video.h"

#define PAGE_PIXELS 64000
#define MAX_SIDE 8192

enum { SRC_NONE, SRC_3D, SRC_VEC, SRC_VECHR };   /* SRC_VECHR (S3.1): a 1x vector write whose N x look is emitted by an hr emitter (hr_obj.c): no hr pixel -> what was under it */

typedef struct {
    u8 src;       /* SRC_*: who wrote the pixel last */
    u8 val;       /* the byte written */
    u8 under;     /* vector write: the page value it replaced */
    u8 under3d;   /* vector write: the pixel under it was a (fresh) 3D pixel */
} Shadow;

EnhCanvas enh_cv = { 320, 200, 1, 0, 0, 0, 0, 320, 200 };
bool enh_shadow_on;
int enh_route;
int enh_stamp_hold;   /* S3.1: > 0 = the 1x writes of a 3D pass are recorded but not stamped (an hr emitter draws the object itself) */
int enh_mirror;       /* S3.3 */
int enh_vec_hold;     /* S3.1: > 0 = the 1x line()/put_pixel writes are an hr emitter's (SRC_VECHR) */

static bool active;
static EnhView view = ENH_VIEW_MENU;

static Shadow sh_buf[2][PAGE_PIXELS];
static Shadow *sh_work = sh_buf[0], *sh_shown = sh_buf[1];  /* pointers, so a display pass can swap them (S2.4, enh_canvas_snap_swap) */
static u16 *l3d_work, *l3d_shown, *lvec_work, *lvec_shown;   /* lw * lh each; bit 8 = written */
static size_t layer_px, layer_cap;                           /* layer_cap: the pixels the four buffers hold (>= layer_px) */

/* ---------------------------------------------------------------- geometry (3.1, 3.2) */

void enh_canvas_geometry(EnhCanvas *c, EnhView v, int win_w, int win_h, bool aspect43, int fixed_n, bool wide)
{
    int lw = win_w < 1 ? 1 : win_w;
    int lh = aspect43 ? win_h * 5 / 6 : win_h;                 /* floor(Ho / a), a = 1.2 */
    if (lh < 1) lh = 1;
    int fit = lw / 320 < lh / 200 ? lw / 320 : lh / 200;
    if (fit < 1) fit = 1;
    if (!wide) {                                               /* the canvas is the frame; the host letterboxes it */
        int n = fixed_n > 0 ? fixed_n : fit;
        if (n > MAX_SIDE / 320) n = MAX_SIDE / 320;
        c->lw = 320 * n; c->lh = 200 * n; c->n = n;
        c->fx = c->fy = 0;
        c->vx = c->vy = 0; c->vw = c->lw; c->vh = c->lh;
        return;
    }
    int n = fixed_n > 0 && fixed_n < fit ? fixed_n : fit;      /* wide: never larger than the window holds */
    if (lw < 320 * n) lw = 320 * n;
    if (lh < 200 * n) lh = 200 * n;
    if (lw > MAX_SIDE) lw = MAX_SIDE;
    if (lh > MAX_SIDE) lh = MAX_SIDE;
    c->lw = lw; c->lh = lh; c->n = n;
    c->fx = (lw - 320 * n) / 2;
    bool extended = v == ENH_VIEW_FRONT || v == ENH_VIEW_FULL;
    c->fy = extended ? lh - 200 * n : (lh - 200 * n) / 2;      /* bottom centre, or centred */
    if (extended) { c->vx = c->vy = 0; c->vw = lw; c->vh = lh; }
    else { c->vx = c->fx; c->vy = c->fy; c->vw = 320 * n; c->vh = 200 * n; }
}

bool enh_canvas_active(void) { return active && enh_canvas_on(); }

void enh_canvas_set_active(bool on)
{
    active = on;
    enh_shadow_on = on && enh_canvas_on();
    view = ENH_VIEW_MENU;                        /* a mode set starts a new program: the menu placement */
    enh_route = ENH_ROUTE_ART;
}

void enh_set_view(int m)
{
    if (m == 0) view = ENH_VIEW_FRONT;
    else if (m >= 0x41 && m <= 0x43) view = ENH_VIEW_FIT;
    else view = ENH_VIEW_FULL;
}

EnhView enh_view_get(void) { return view; }

/* ---------------------------------------------------------------- layers */

static void layers_free(void)
{
    free(l3d_work); free(l3d_shown); free(lvec_work); free(lvec_shown);
    l3d_work = l3d_shown = lvec_work = lvec_shown = NULL;
    layer_px = layer_cap = 0;
}

static void layers_alloc(void)
{
    size_t px = (size_t)enh_cv.lw * (size_t)enh_cv.lh;
    layers_free();
    l3d_work = calloc(px, sizeof(u16)); l3d_shown = calloc(px, sizeof(u16));
    lvec_work = calloc(px, sizeof(u16)); lvec_shown = calloc(px, sizeof(u16));
    if (l3d_work && l3d_shown && lvec_work && lvec_shown) layer_px = layer_cap = px;
    else layers_free();
}

u16 *enh_layer3d(void) { return l3d_work; }
u16 *enh_layer_vec(void) { return lvec_work; }

bool enh_canvas_update(int win_w, int win_h, bool aspect43)
{
    EnhCanvas c;
    enh_canvas_geometry(&c, view, win_w, win_h, aspect43, enh.render_scale, enh.wide);
    if (!memcmp(&c, &enh_cv, sizeof c) && layer_px) return false;
    bool resized = c.lw != enh_cv.lw || c.lh != enh_cv.lh;
    enh_cv = c;
    layers_alloc();                                   /* a new placement: the old layer contents no longer line up */
    return resized;
}

/* ---------------------------------------------------------------- shadow records (2.5) */

/* the pages the shadow follows: page 0 (A000) and page 1, the back page (S2.2: in the game page 1 is a DOS block, gfx_alloc(1), not A400) */
bool enh_page_tracked(u16 seg) { return seg == 0xA000 || (seg != 0 && seg == video_page_seg(1)); }
static bool rec_ok(u16 seg, u16 off) { return off < PAGE_PIXELS && enh_page_tracked(seg); }

/* a 1x write inside a 3D pass that the hr back end does not mirror (sprites, dots, the 1001 fills before the hr backdrop) shows as its
 * original pixel: its block of the 3D layer gets the value (bit 9 marks it, for --hr-check); a later hr write replaces it */
static void stamp3d(u16 seg, u16 off, u8 val)
{
    if (enh_stamp_hold > 0 || !l3d_work || (enh_hr.on && seg != enh_hr.seg)) return;   /* the other page during an hr pass (the TV camera's backdrop): not this layer */
    int x = off % 320, y = off / 320, n = enh_cv.n;
    u16 *p = l3d_work + (size_t)(enh_cv.fy + y * n) * enh_cv.lw + (size_t)(enh_cv.fx + x * n);
    for (int ky = 0; ky < n; ky++, p += enh_cv.lw)
        for (int kx = 0; kx < n; kx++) p[kx] = (u16)(0x300 | val);
}

static void vec_clear_block(u16 off)
{
    if (!lvec_work) return;
    int x = off % 320, y = off / 320, n = enh_cv.n;
    int bx = enh_cv.fx + x * n, by = enh_cv.fy + y * n;
    for (int ky = 0; ky < n; ky++) memset(lvec_work + (size_t)(by + ky) * enh_cv.lw + bx, 0, (size_t)n * sizeof(u16));
}

void enh_rec_line(u16 seg, u16 off, u8 val)
{
    if (!rec_ok(seg, off)) return;
    Shadow *r = &sh_work[off];
    if (enh_route == ENH_ROUTE_3D) { r->src = SRC_3D; r->val = val; stamp3d(seg, off, val); return; }
    u8 old = seg_ptr(seg)[off];
    bool fresh = r->src != SRC_NONE && old == r->val;
    if (!fresh) vec_clear_block(off);
    if ((r->src == SRC_VEC || r->src == SRC_VECHR) && fresh) {      /* a line over a line keeps what was below both */
        r->val = val;
        if (enh_vec_hold <= 0) r->src = SRC_VEC;                    /* a classic line over an hr one: shown as its original pixel */
        return;
    }
    r->under3d = (u8)(r->src == SRC_3D && fresh);
    r->under = old;
    r->src = enh_vec_hold > 0 ? SRC_VECHR : SRC_VEC;
    r->val = val;
}

/* S3.3: a polygon pixel written (call AFTER the write; `old` = the page value it replaced) as the footprint of an hr vector fill: the same record as a held line pixel */
void enh_rec_vecfill(u16 seg, u16 off, u8 old, u8 val)
{
    if (!rec_ok(seg, off)) return;
    Shadow *r = &sh_work[off];
    bool fresh = r->src != SRC_NONE && old == r->val;
    if (!fresh) vec_clear_block(off);
    if ((r->src == SRC_VEC || r->src == SRC_VECHR) && fresh) { r->val = val; return; }
    r->under3d = (u8)(r->src == SRC_3D && fresh);
    r->under = old;
    r->src = SRC_VECHR;
    r->val = val;
}

void enh_rec_fill(u16 seg, u16 off, u8 val)
{
    if (!rec_ok(seg, off)) return;
    Shadow *r = &sh_work[off];
    if (enh_route == ENH_ROUTE_3D) { r->src = SRC_3D; r->val = val; stamp3d(seg, off, val); }
    else r->src = SRC_NONE;                                          /* plain art over what was there */
}

/* S3.2: the 1x page pixel at off was recoloured in place, new = f(old) (the FLIR ramp 98h-9Fh of the TV window, poly_fill mode 3). A 3D record that still holds
 * `old` follows: the colours of its N x N layer block go through the same f (written and stamped pixels alike) and the record takes the new value, so the tinted
 * window stays at N x. Anything else is plain art over what was there (false: the caller records it as art). */
bool enh_rec_remap(u16 seg, u16 off, u8 old, u8 val, u8 (*f)(u8))
{
    if (!rec_ok(seg, off)) return false;
    Shadow *r = &sh_work[off];
    if (r->src != SRC_3D || r->val != old || !l3d_work || (enh_hr.on && seg != enh_hr.seg)) return false;
    const int n = enh_cv.n;
    u16 *p = l3d_work + (size_t)(enh_cv.fy + (off / 320) * n) * enh_cv.lw + (size_t)(enh_cv.fx + (off % 320) * n);
    for (int ky = 0; ky < n; ky++, p += enh_cv.lw)
        for (int kx = 0; kx < n; kx++)
            if (p[kx] & 0x100) p[kx] = (u16)((p[kx] & 0xFF00) | f((u8)p[kx]));
    r->val = val;
    return true;
}

void enh_rec_3d(u16 seg, u16 off, u8 val)
{
    if (!enh_hr.on) { enh_rec_fill(seg, off, val); return; }
    if (!rec_ok(seg, off)) return;
    sh_work[off].src = SRC_3D;
    sh_work[off].val = val;
}

void enh_rec_span3d(u16 seg, u16 off, int len)
{
    if (!enh_page_tracked(seg)) return;
    const u8 *pg = seg_ptr(seg);
    for (int i = 0; i < len; i++) enh_rec_3d(seg, (u16)(off + i), pg[(u16)(off + i)]);
}

bool enh_shadow_vechr(u16 off, u8 *val)
{
    if (off >= PAGE_PIXELS || sh_work[off].src != SRC_VECHR || sh_work[off].under == sh_work[off].val) return false;   /* a footprint: the page changed under it */
    *val = sh_work[off].val;
    return true;
}

bool enh_shadow_under(u16 off, u8 *under, bool *under3d)      /* S3.3: what a vector record replaced */
{
    if (off >= PAGE_PIXELS || (sh_work[off].src != SRC_VECHR && sh_work[off].src != SRC_VEC)) return false;
    *under = sh_work[off].under; *under3d = sh_work[off].under3d != 0;
    return true;
}

bool enh_shadow_3d(u16 off, u8 *val)
{
    if (off >= PAGE_PIXELS || sh_work[off].src != SRC_3D) return false;
    *val = sh_work[off].val;
    return true;
}

void enh_pass_clear(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 319) x1 = 319;
    if (y1 > 199) y1 = 199;
    if (x1 < x0 || y1 < y0) return;
    for (int y = y0; y <= y1; y++) memset(&sh_work[y * 320 + x0], 0, (size_t)(x1 - x0 + 1) * sizeof(Shadow));
    if (!layer_px) return;
    const int n = enh_cv.n, lw = enh_cv.lw;
    for (int y = enh_cv.fy + n * y0; y < enh_cv.fy + n * (y1 + 1); y++) {
        size_t at = (size_t)y * lw + (size_t)(enh_cv.fx + n * x0), cnt = (size_t)n * (size_t)(x1 - x0 + 1);
        memset(l3d_work + at, 0, cnt * sizeof(u16));
        memset(lvec_work + at, 0, cnt * sizeof(u16));
    }
}

void enh_layers_reset(void)
{
    memset(sh_work, 0, PAGE_PIXELS * sizeof(Shadow));
    memset(sh_shown, 0, PAGE_PIXELS * sizeof(Shadow));
    if (!layer_px) return;
    memset(l3d_work, 0, layer_px * sizeof(u16)); memset(l3d_shown, 0, layer_px * sizeof(u16));
    memset(lvec_work, 0, layer_px * sizeof(u16)); memset(lvec_shown, 0, layer_px * sizeof(u16));
}

void enh_page_cleared(u16 seg)
{
    if (!enh_page_tracked(seg)) return;
    memset(sh_work, 0, PAGE_PIXELS * sizeof(Shadow));
    if (seg == 0xA000) memset(sh_shown, 0, PAGE_PIXELS * sizeof(Shadow));
}

static void probe_flip(void);

void enh_flip(unsigned bytes)
{
    size_t n = bytes < PAGE_PIXELS ? bytes : PAGE_PIXELS;
    memcpy(sh_shown, sh_work, n * sizeof(Shadow));
    if (layer_px) {
        memcpy(l3d_shown, l3d_work, layer_px * sizeof(u16));
        memcpy(lvec_shown, lvec_work, layer_px * sizeof(u16));
    }
    if (enh.mfd_probe[0] && !enh_sbx) probe_flip();
}

/* ---------------------------------------------------------------- compose (2.5) */

/* idx/sidx: the shown 320x200 page and the page offset of each pixel (0xFFFF = none); pal: the live DAC as XRGB. */
typedef struct { const Shadow *sh; const u16 *l3, *lv; } ComposeSrc;   /* the shadow plane and the 3D / vector layers being composed */

static void compose_canvas(u32 *out, const u8 *idx, const u16 *sidx, const u32 *pal, bool screen, const ComposeSrc *cs)
{
    const Shadow *sh_shown = cs->sh;
    const u16 *l3d_shown = cs->l3, *lvec_shown = cs->lv;
    const int lw = enh_cv.lw, lh = enh_cv.lh, n = enh_cv.n, fx = enh_cv.fx, fy = enh_cv.fy;
    memset(out, 0, (size_t)lw * (size_t)lh * sizeof(u32));
    if (!screen) return;
    const bool layers = layer_px != 0;
    if (layers && (enh_cv.vw > 320 * n || enh_cv.vh > 200 * n)) {    /* the extended area: the 3D layer around the frame */
        for (int y = 0; y < lh; y++) {
            u32 *o = out + (size_t)y * lw;
            const u16 *l = l3d_shown + (size_t)y * lw;
            bool in_rows = y >= fy && y < fy + 200 * n;
            for (int x = 0; x < lw; x++) {
                if (in_rows && x >= fx && x < fx + 320 * n) { x = fx + 320 * n - 1; continue; }
                if (l[x] & 0x100) o[x] = pal[l[x] & 255];
            }
        }
    }
    for (int y = 0; y < 200; y++) {
        for (int ky = 0; ky < n; ky++) {
            u32 *o = out + (size_t)(fy + y * n + ky) * lw + fx;
            for (int x = 0; x < 320; x++, o += n) {
                const u8 v = idx[y * 320 + x];
                const u16 si = sidx[y * 320 + x];
                const Shadow *r = si < PAGE_PIXELS ? &sh_shown[si] : NULL;
                if (!layers || !r || r->src == SRC_NONE || r->val != v) {
                    for (int kx = 0; kx < n; kx++) o[kx] = pal[v];
                    continue;
                }
                const size_t at = (size_t)(fy + y * n + ky) * lw + (size_t)(fx + x * n);
                if (r->src == SRC_3D) {
                    for (int kx = 0; kx < n; kx++) {
                        u16 l = l3d_shown[at + kx];
                        o[kx] = pal[(l & 0x100) ? (l & 255) : v];
                    }
                    continue;
                }
                bool any = false;                                      /* vector pixel: its layer pixels over what was below it */
                for (int by = 0; by < n && !any; by++)
                    for (int kx = 0; kx < n; kx++)
                        if (lvec_shown[(size_t)(fy + y * n + by) * lw + (size_t)(fx + x * n + kx)] & 0x100) { any = true; break; }
                if (!any && r->src != SRC_VECHR) { for (int kx = 0; kx < n; kx++) o[kx] = pal[v]; continue; }   /* nothing emitted: the original pixel */
                for (int kx = 0; kx < n; kx++) {
                    u16 lv = lvec_shown[at + kx];
                    u16 l3 = l3d_shown[at + kx];
                    o[kx] = (lv & 0x100) ? pal[lv & 255] : (r->under3d && (l3 & 0x100)) ? pal[l3 & 255] : pal[r->under];
                }
            }
        }
    }
}

bool enh_compose_vga(u32 *xrgb)
{
    static u8 idx[PAGE_PIXELS];
    static u16 sidx[PAGE_PIXELS];
    static u32 *last;
    static size_t last_px;
    u32 pal[256];
    const size_t px = (size_t)enh_cv.lw * (size_t)enh_cv.lh;
    bool screen = vga_shown_frame(idx, sidx, pal);
    const ComposeSrc cs = { sh_shown, l3d_shown, lvec_shown };
    compose_canvas(xrgb, idx, sidx, pal, screen, &cs);
    if (last_px == px && last && !memcmp(last, xrgb, px * sizeof(u32))) return false;
    if (last_px != px) { free(last); last = malloc(px * sizeof(u32)); last_px = last ? px : 0; }
    if (last) memcpy(last, xrgb, px * sizeof(u32));
    return true;
}

/* S3.1 test aid (the hr check and the dumps of the render test): the frame being drawn (the work shadow and layers) composed from `page` (the
 * 320x200 page of its pass) with `pal` (256 entries). Same rule as the present path. */
void enh_compose_work(u32 *out, const u8 *page, const u32 *pal)
{
    static u16 ident[PAGE_PIXELS];
    if (ident[1] == 0) for (int i = 0; i < PAGE_PIXELS; i++) ident[i] = (u16)i;
    const ComposeSrc cs = { sh_work, l3d_work, lvec_work };
    compose_canvas(out, page, ident, pal, true, &cs);
}

/* ---------------------------------------------------------------- --mfd-probe (S3.2 test aid) */

/* After every page flip: the pixels of the TV window (x 169..245, y 110..169) and of the map window (x 74..150, y 110..169) of the shown page that come from a 3D
 * record equal to the page (so the compositor shows the 3D layer there) against the window size; F117_PROBE_FRAMES=a,b writes the composed shown canvas of those frames
 * to PREFIX_<frame>.raw (s32 lw, lh, then lw * lh XRGB u32 as composed, then the same frame as the classic 1x page scaled N x N). */
static void probe_flip(void)
{
    static int frame;
    static FILE *f;
    if (!f) { char p[1100]; snprintf(p, sizeof p, "%s.txt", enh.mfd_probe); f = fopen(p, "w"); if (!f) return; }
    const u8 *pg = seg_ptr(0xA000);
    const struct { int x0, y0, x1, y1; } w[2] = { { 169, 110, 245, 169 }, { 74, 110, 150, 169 } };
    long c3[2] = { 0, 0 }, cn[2] = { 0, 0 };
    for (int k = 0; k < 2; k++)
        for (int y = w[k].y0; y <= w[k].y1; y++)
            for (int x = w[k].x0; x <= w[k].x1; x++) {
                const Shadow *r = &sh_shown[y * 320 + x];
                cn[k]++;
                if (r->src == SRC_3D && r->val == pg[y * 320 + x]) c3[k]++;
            }
    /* S3.3: the vector records of the shown page (a fresh line record) and how many of them show an hr pixel: the HUD window, the map window, the whole frame */
    long vh[3] = { 0, 0, 0 }, vt[3] = { 0, 0, 0 };
    const struct { int x0, y0, x1, y1; } vw[3] = { { 48, 2, 271, 90 }, { 74, 110, 150, 169 }, { 0, 0, 319, 199 } };
    for (int k = 0; k < 3; k++)
        for (int y = vw[k].y0; y <= vw[k].y1; y++)
            for (int x = vw[k].x0; x <= vw[k].x1; x++) {
                const Shadow *r = &sh_shown[y * 320 + x];
                if (!(r->src == SRC_VEC || r->src == SRC_VECHR) || r->val != pg[y * 320 + x]) continue;
                vt[k]++;
                if (r->src != SRC_VECHR || !lvec_shown) continue;
                const int nn = enh_cv.n;
                bool any = false;
                for (int ky = 0; ky < nn && !any; ky++)
                    for (int kx = 0; kx < nn; kx++)
                        if (lvec_shown[(size_t)(enh_cv.fy + y * nn + ky) * enh_cv.lw + (size_t)(enh_cv.fx + x * nn + kx)] & 0x100) { any = true; break; }
                if (any) vh[k]++;
            }
    fprintf(f, "frame %d tv %ld/%ld map %ld/%ld rmfd=%x lmfdoff=%x cockpit=%x view=%x p4088=%x draw=%x master=%x site=%x unit=%x hudv %ld/%ld mapv %ld/%ld allv %ld/%ld\n", frame, c3[0], cn[0], c3[1], cn[1],
            DSW(0xDFDC), DSW(0xDEDA), DSW(0x365E), DSW(0xC07A), DSB(0x4088), video_draw_seg(), DSW(0xE2FE), DSW(0x3D66), DSW(0x3D64), vh[0], vt[0], vh[1], vt[1], vh[2], vt[2]);
    fflush(f);
    const char *e = getenv("F117_PROBE_FRAMES");
    for (const char *t = e; t && *t; ) {
        char *end;
        long v = strtol(t, &end, 10);
        if (end == t) break;
        if (v == frame && layer_px) {
            u32 *img = malloc(layer_px * sizeof(u32));
            u32 pal[256];
            u8 dac[768];
            dac_get(0, 256, dac);
            for (int i = 0; i < 256; i++) {
                u32 r = (u32)dac[3 * i] << 2, g = (u32)dac[3 * i + 1] << 2, b = (u32)dac[3 * i + 2] << 2;
                pal[i] = ((r | r >> 6) << 16) | ((g | g >> 6) << 8) | (b | b >> 6);
            }
            static u16 ident[PAGE_PIXELS];
            for (int i = 0; i < PAGE_PIXELS; i++) ident[i] = (u16)i;
            if (img) {
                const ComposeSrc cs = { sh_shown, l3d_shown, lvec_shown };
                compose_canvas(img, pg, ident, pal, true, &cs);
                char p[1100];
                snprintf(p, sizeof p, "%s_%04d.raw", enh.mfd_probe, frame);
                FILE *o = fopen(p, "wb");
                if (o) {
                    s32 hd[2] = { enh_cv.lw, enh_cv.lh };
                    fwrite(hd, 4, 2, o);
                    fwrite(img, 4, layer_px, o);
                    static Shadow none[PAGE_PIXELS];                       /* no records: the 1x page scaled N x N, the classic picture of the same frame */
                    const ComposeSrc c1 = { none, l3d_shown, lvec_shown };
                    compose_canvas(img, pg, ident, pal, true, &c1);
                    fwrite(img, 4, layer_px, o);
                    fclose(o);
                }
                free(img);
            }
        }
        t = *end == ',' ? end + 1 : end;
    }
    frame++;
}

/* ---------------------------------------------------------------- hr vector emitters (S3.1) */

/* A vector layer pixel (layer coordinates) of an hr emitter. The page pixel whose block it lies in becomes a vector record whose "nothing emitted" look is what
 * was under it (SRC_VECHR) unless it is one already, so an hr line shows where the 1x line did not reach and the 1x footprint shows nothing where it did. */
void enh_vec_put(int lx, int ly, u8 c)
{
    const int n = enh_cv.n;
    if (!lvec_work || lx < enh_cv.fx || ly < enh_cv.fy || lx >= enh_cv.lw || ly >= enh_cv.lh) return;
    int px = (lx - enh_cv.fx) / n, py = (ly - enh_cv.fy) / n;
    if (px > 319 || py > 199) return;
    const u16 seg = video_draw_seg(), off = (u16)(py * 320 + px);
    if (enh_page_tracked(seg)) {
        Shadow *r = &sh_work[off];
        const u8 pv = seg_ptr(seg)[off];
        const bool fresh = r->src != SRC_NONE && r->val == pv;
        if (!(fresh && (r->src == SRC_VEC || r->src == SRC_VECHR))) {
            if (!fresh) vec_clear_block(off);
            r->under3d = (u8)(r->src == SRC_3D && fresh);
            r->under = pv;
            r->src = SRC_VECHR;
            r->val = pv;
        }
    }
    lvec_work[(size_t)ly * enh_cv.lw + lx] = (u16)(0x100 | c);
}

/* ---------------------------------------------------------------- page-to-page blits (S3.2 fix) */

/* The shadow has two planes: work (the page being drawn, page 1) and shown (page 0, the displayed one; enh_flip copies work to shown for the bytes the flip copies).
 * The game also moves rectangles between the pages with blit_rect: every frame view_frame copies the two MFD windows (map x 74.., TV x 169.., 77x60) from the draw page to the
 * other page, and the flip does not cover those rows, so without this the displayed page's records for the MFDs were never refreshed and the windows showed as 1x pixels.
 * enh_blit follows such a blit: records and the three layers go along between two tracked pages (page 0 = shown, page 1 = work); a blit from any other page (the
 * picture page 2: panels, fonts, saved backgrounds) is art, so the records under its destination rectangle are voided. */
void enh_blit(u16 sseg, int sx, int sy, u16 dseg, int dx, int dy, int w, int h)
{
    const bool dt = enh_page_tracked(dseg);
    if (!dt || !sh_work || w <= 0 || h <= 0 || sx < 0 || sy < 0 || dx < 0 || dy < 0 || sx + w > 320 || dx + w > 320 || sy + h > 200 || dy + h > 200) return;
    const bool d_shown = dseg == 0xA000, s_shown = sseg == 0xA000, st = enh_page_tracked(sseg);
    Shadow *dsh = d_shown ? sh_shown : sh_work;
    if (!st) {                                                   /* from an art page: void the destination records */
        for (int j = 0; j < h; j++) memset(&dsh[(dy + j) * 320 + dx], 0, (size_t)w * sizeof(Shadow));
        return;
    }
    if (s_shown == d_shown) return;                              /* the same plane */
    const Shadow *ssh = s_shown ? sh_shown : sh_work;
    const int n = enh_cv.n, lw = enh_cv.lw;
    u16 *dl3 = d_shown ? l3d_shown : l3d_work, *dlv = d_shown ? lvec_shown : lvec_work;
    const u16 *sl3 = s_shown ? l3d_shown : l3d_work, *slv = s_shown ? lvec_shown : lvec_work;
    const bool lay = layer_px != 0;
    for (int j = 0; j < h; j++) {
        memcpy(&dsh[(dy + j) * 320 + dx], &ssh[(sy + j) * 320 + sx], (size_t)w * sizeof(Shadow));
        if (!lay) continue;
        for (int k = 0; k < n; k++) {
            const size_t so = (size_t)(enh_cv.fy + (sy + j) * n + k) * lw + (size_t)(enh_cv.fx + sx * n);
            const size_t d = (size_t)(enh_cv.fy + (dy + j) * n + k) * lw + (size_t)(enh_cv.fx + dx * n);
            memcpy(dl3 + d, sl3 + so, (size_t)w * n * sizeof(u16));
            memcpy(dlv + d, slv + so, (size_t)w * n * sizeof(u16));
        }
    }
}

/* ---------------------------------------------------------------- window store (S3.2) */

/* The map MFD keeps its finished window in the backup page 2 (mfd_map_save_bg, 973C) and copies it back every frame to erase the aircraft marker (mfd_map_restore_bg,
 * 9770) instead of drawing the terrain again. A blit between pages is a plain page write, so without help the restored pixels would lose their records and show as 1x
 * pixels. The store keeps the work records and the three layers of the window beside the backup page (one slot, the map window). Not touched in a display pass. */
static struct { bool valid; int x, y, w, h, n; Shadow *rec; u16 *l3, *lv; } store;

void enh_window_save(int x, int y, int w, int h)
{
    if (enh_sbx || !l3d_work || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > 320 || y + h > 200) return;
    const int n = enh_cv.n;
    free(store.rec); free(store.l3); free(store.lv);
    store.valid = false;
    if (hr_tracing) { s32 t[4] = { enh_cv.fx + n * x, enh_cv.fy + n * y, enh_cv.fx + n * (x + w) - 1, enh_cv.fy + n * (y + h) - 1 }; hr_trace(13, 4, t); }   /* S3.3: 13 = the window's vector layer is saved (the reference keeps its lines) */
    store.rec = malloc((size_t)w * h * sizeof(Shadow));
    store.l3 = malloc((size_t)w * h * n * n * sizeof(u16));
    store.lv = malloc((size_t)w * h * n * n * sizeof(u16));
    if (!store.rec || !store.l3 || !store.lv) { free(store.rec); free(store.l3); free(store.lv); store.rec = NULL; store.l3 = store.lv = NULL; return; }
    for (int j = 0; j < h; j++) memcpy(store.rec + (size_t)j * w, sh_work + (y + j) * 320 + x, (size_t)w * sizeof(Shadow));
    for (int j = 0; j < h * n; j++) {
        const size_t at = (size_t)(enh_cv.fy + y * n + j) * enh_cv.lw + (size_t)(enh_cv.fx + x * n);
        memcpy(store.l3 + (size_t)j * w * n, l3d_work + at, (size_t)w * n * sizeof(u16));
        memcpy(store.lv + (size_t)j * w * n, lvec_work + at, (size_t)w * n * sizeof(u16));
    }
    store.x = x; store.y = y; store.w = w; store.h = h; store.n = n;
    store.valid = true;
}

void enh_window_restore(int x, int y, int w, int h)
{
    if (!store.valid || !l3d_work || store.x != x || store.y != y || store.w != w || store.h != h || store.n != enh_cv.n) return;
    const int n = enh_cv.n;
    if (hr_tracing) { s32 t[4] = { enh_cv.fx + n * x, enh_cv.fy + n * y, enh_cv.fx + n * (x + w) - 1, enh_cv.fy + n * (y + h) - 1 }; hr_trace(12, 4, t); }   /* S3.3: 12 = the vector layer of this rectangle is replaced by the saved one (the reference clears its lines) */
    for (int j = 0; j < h; j++) memcpy(sh_work + (y + j) * 320 + x, store.rec + (size_t)j * w, (size_t)w * sizeof(Shadow));
    for (int j = 0; j < h * n; j++) {
        const size_t at = (size_t)(enh_cv.fy + y * n + j) * enh_cv.lw + (size_t)(enh_cv.fx + x * n);
        memcpy(l3d_work + at, store.l3 + (size_t)j * w * n, (size_t)w * n * sizeof(u16));
        memcpy(lvec_work + at, store.lv + (size_t)j * w * n, (size_t)w * n * sizeof(u16));
    }
}

/* ---------------------------------------------------------------- display-pass snapshots (S2.4, sandbox.c) */

static void snap_get_live(EnhCanvasSnap *s)
{
    s->cv = enh_cv; s->active = active; s->shadow_on = enh_shadow_on; s->view = (int)view; s->route = enh_route;
    s->sh_work = sh_work; s->sh_shown = sh_shown;
    s->l3d_work = l3d_work; s->l3d_shown = l3d_shown; s->lvec_work = lvec_work; s->lvec_shown = lvec_shown;
    s->layer_px = layer_px; s->cap = layer_cap;
}

static void snap_set_live(const EnhCanvasSnap *s)
{
    enh_cv = s->cv; active = s->active; enh_shadow_on = s->shadow_on; view = (EnhView)s->view; enh_route = s->route;
    sh_work = s->sh_work; sh_shown = s->sh_shown;
    l3d_work = s->l3d_work; l3d_shown = s->l3d_shown; lvec_work = s->lvec_work; lvec_shown = s->lvec_shown;
    layer_px = s->layer_px; layer_cap = s->cap;
}

bool enh_canvas_snap_copy(EnhCanvasSnap *d, const EnhCanvasSnap *s)
{
    if (!d->sh_work) d->sh_work = malloc(PAGE_PIXELS * sizeof(Shadow));
    if (!d->sh_shown) d->sh_shown = malloc(PAGE_PIXELS * sizeof(Shadow));
    if (!d->sh_work || !d->sh_shown) return false;
    if (d->cap < s->layer_px) {
        free(d->l3d_work); free(d->l3d_shown); free(d->lvec_work); free(d->lvec_shown);
        d->l3d_work = malloc(s->layer_px * sizeof(u16)); d->l3d_shown = malloc(s->layer_px * sizeof(u16));
        d->lvec_work = malloc(s->layer_px * sizeof(u16)); d->lvec_shown = malloc(s->layer_px * sizeof(u16));
        d->cap = d->l3d_work && d->l3d_shown && d->lvec_work && d->lvec_shown ? s->layer_px : 0;
        if (!d->cap) {
            free(d->l3d_work); free(d->l3d_shown); free(d->lvec_work); free(d->lvec_shown);
            d->l3d_work = d->l3d_shown = d->lvec_work = d->lvec_shown = NULL;
            return false;
        }
    }
    memcpy(d->sh_work, s->sh_work, PAGE_PIXELS * sizeof(Shadow));
    memcpy(d->sh_shown, s->sh_shown, PAGE_PIXELS * sizeof(Shadow));
    const size_t nb = s->layer_px * sizeof(u16);
    if (nb) {
        memcpy(d->l3d_work, s->l3d_work, nb); memcpy(d->l3d_shown, s->l3d_shown, nb);
        memcpy(d->lvec_work, s->lvec_work, nb); memcpy(d->lvec_shown, s->lvec_shown, nb);
    }
    d->cv = s->cv; d->active = s->active; d->shadow_on = s->shadow_on; d->view = s->view; d->route = s->route;
    d->layer_px = s->layer_px;
    return true;
}

bool enh_canvas_snap_take(EnhCanvasSnap *d)
{
    EnhCanvasSnap live;
    snap_get_live(&live);
    return enh_canvas_snap_copy(d, &live);
}

void enh_canvas_snap_swap(EnhCanvasSnap *s)
{
    EnhCanvasSnap live;
    snap_get_live(&live);
    snap_set_live(s);
    *s = live;
}

void enh_canvas_snap_peek(EnhCanvasSnap *s) { snap_get_live(s); }

size_t enh_canvas_snap_bytes(const EnhCanvasSnap *s) { return 2 * PAGE_PIXELS * sizeof(Shadow) + 4 * s->layer_px * sizeof(u16); }

/* ---------------------------------------------------------------- mouse */

void enh_canvas_to_frame(int *x, int *y)
{
    int n = enh_cv.n;
    int fxp = (*x - enh_cv.fx) / n, fyp = (*y - enh_cv.fy) / n;
    if (*x < enh_cv.fx) fxp = 0;
    if (*y < enh_cv.fy) fyp = 0;
    *x = fxp > 319 ? 319 : fxp;
    *y = fyp > 199 ? 199 : fyp;
}

void enh_frame_to_canvas(int *x, int *y)
{
    *x = enh_cv.fx + *x * enh_cv.n + enh_cv.n / 2;
    *y = enh_cv.fy + *y * enh_cv.n + enh_cv.n / 2;
}
