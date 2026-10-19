/* The Enhanced canvas, layers, shadow plane and compositor (see enh_canvas.h; DESIGN.md 2.1, 2.5, 3.1, 3.2). */
#include "enhanced/enh_canvas.h"

#include <stdlib.h>
#include <string.h>

#include "enhanced/enhanced.h"
#include "enhanced/hr.h"
#include "platform/vga.h"
#include "video/video.h"

#define PAGE_PIXELS 64000
#define MAX_SIDE 8192

enum { SRC_NONE, SRC_3D, SRC_VEC };

typedef struct {
    u8 src;       /* SRC_*: who wrote the pixel last */
    u8 val;       /* the byte written */
    u8 under;     /* vector write: the page value it replaced */
    u8 under3d;   /* vector write: the pixel under it was a (fresh) 3D pixel */
} Shadow;

EnhCanvas enh_cv = { 320, 200, 1, 0, 0, 0, 0, 320, 200 };
bool enh_shadow_on;
int enh_route;

static bool active;
static EnhView view = ENH_VIEW_MENU;

static Shadow sh_work[PAGE_PIXELS], sh_shown[PAGE_PIXELS];
static u16 *l3d_work, *l3d_shown, *lvec_work, *lvec_shown;   /* lw * lh each; bit 8 = written */
static size_t layer_px;

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
    layer_px = 0;
}

static void layers_alloc(void)
{
    size_t px = (size_t)enh_cv.lw * (size_t)enh_cv.lh;
    layers_free();
    l3d_work = calloc(px, sizeof(u16)); l3d_shown = calloc(px, sizeof(u16));
    lvec_work = calloc(px, sizeof(u16)); lvec_shown = calloc(px, sizeof(u16));
    if (l3d_work && l3d_shown && lvec_work && lvec_shown) layer_px = px;
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
    if (!l3d_work || (enh_hr.on && seg != enh_hr.seg)) return;   /* the other page during an hr pass (the TV camera's backdrop): not this layer */
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
    if (r->src == SRC_VEC && fresh) { r->val = val; return; }       /* a line over a line keeps what was below both */
    r->under3d = (u8)(r->src == SRC_3D && fresh);
    r->under = old;
    r->src = SRC_VEC;
    r->val = val;
}

void enh_rec_fill(u16 seg, u16 off, u8 val)
{
    if (!rec_ok(seg, off)) return;
    Shadow *r = &sh_work[off];
    if (enh_route == ENH_ROUTE_3D) { r->src = SRC_3D; r->val = val; stamp3d(seg, off, val); }
    else r->src = SRC_NONE;                                          /* plain art over what was there */
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
    memset(sh_work, 0, sizeof sh_work);
    memset(sh_shown, 0, sizeof sh_shown);
    if (!layer_px) return;
    memset(l3d_work, 0, layer_px * sizeof(u16)); memset(l3d_shown, 0, layer_px * sizeof(u16));
    memset(lvec_work, 0, layer_px * sizeof(u16)); memset(lvec_shown, 0, layer_px * sizeof(u16));
}

void enh_page_cleared(u16 seg)
{
    if (!enh_page_tracked(seg)) return;
    memset(sh_work, 0, sizeof sh_work);
    if (seg == 0xA000) memset(sh_shown, 0, sizeof sh_shown);
}

void enh_flip(unsigned bytes)
{
    size_t n = bytes < PAGE_PIXELS ? bytes : PAGE_PIXELS;
    memcpy(sh_shown, sh_work, n * sizeof(Shadow));
    if (layer_px) {
        memcpy(l3d_shown, l3d_work, layer_px * sizeof(u16));
        memcpy(lvec_shown, lvec_work, layer_px * sizeof(u16));
    }
}

/* ---------------------------------------------------------------- compose (2.5) */

/* idx/sidx: the shown 320x200 page and the page offset of each pixel (0xFFFF = none); pal: the live DAC as XRGB. */
static void compose_canvas(u32 *out, const u8 *idx, const u16 *sidx, const u32 *pal, bool screen)
{
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
                if (!any) { for (int kx = 0; kx < n; kx++) o[kx] = pal[v]; continue; }   /* nothing emitted yet: the original pixel */
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
    compose_canvas(xrgb, idx, sidx, pal, screen);
    if (last_px == px && last && !memcmp(last, xrgb, px * sizeof(u32))) return false;
    if (last_px != px) { free(last); last = malloc(px * sizeof(u32)); last_px = last ? px : 0; }
    if (last) memcpy(last, xrgb, px * sizeof(u32));
    return true;
}

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
