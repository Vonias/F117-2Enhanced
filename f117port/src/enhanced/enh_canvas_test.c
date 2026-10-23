/* --compose-test: headless check of the canvas geometry (DESIGN 3.1, 3.2) and of the compositor (2.5).
 * With empty 3D / vector layers the composed canvas must equal the faithful 320x200 frame scaled N x N nearest, pixel for pixel,
 * whatever the CRTC start / pan, the DAC, the shadow records (lines drawn through mgraphic.c) and the canvas placement. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enhanced/enh_canvas.h"
#include "enhanced/enhanced.h"
#include "gen/module_video.h"
#include "platform/vga.h"
#include "video/video.h"

static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; if (bad < 20) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

typedef struct {
    EnhView view; int win_w, win_h; bool a43; int fixed; bool wide;
    int lw, lh, n, fx, fy, vx, vy, vw, vh;
} GeomCase;

static const GeomCase geom[] = {
    { ENH_VIEW_FRONT, 960, 720, true, 0, true,    960,  600, 3,   0,   0,   0,   0,  960,  600 },
    { ENH_VIEW_FRONT, 1920, 1080, true, 0, true, 1920,  900, 4, 320, 100,   0,   0, 1920,  900 },
    { ENH_VIEW_FULL,  1920, 1080, true, 0, true, 1920,  900, 4, 320, 100,   0,   0, 1920,  900 },
    { ENH_VIEW_FIT,   1920, 1080, true, 0, true, 1600, 1000, 5,   0,   0,   0,   0, 1600, 1000 },   /* S3.4 fix: the frame filling the height, the host scales it */
    { ENH_VIEW_MENU,  1920, 1080, true, 0, true, 1920,  900, 4, 320,  50, 320,  50, 1280,  800 },
    { ENH_VIEW_FRONT, 3440, 1440, false, 0, true, 3440, 1440, 7, 600,  40,   0,   0, 3440, 1440 },
    { ENH_VIEW_FIT,   3440, 1440, false, 0, true, 2560, 1600, 8,   0,   0,   0,   0, 2560, 1600 },
    { ENH_VIEW_SIDE,  1920, 1080, true, 0, true, 2133, 1000, 5, 266,   0,   0,   0, 2133, 1000 },   /* the second cockpit view: extended sideways only */
    { ENH_VIEW_FRONT, 1920, 1080, false, 8, true, 2845, 1600, 8, 142,   0,   0,   0, 2845, 1600 },   /* S3.4: a fixed scale is kept; the canvas is the window's aspect around the frame (the host scales it down) */
    { ENH_VIEW_FRONT, 1920, 1080, true,  8, true, 3414, 1600, 8, 427,   0,   0,   0, 3414, 1600 },   /* the same in 4:3 pixels (window 1920 x 900 logical) */
    { ENH_VIEW_FRONT, 1000, 1000, false, 0, true, 1000, 1000, 3,  20, 400,   0,   0, 1000, 1000 },
    { ENH_VIEW_FRONT, 200, 150, true, 0, true,    320,  200, 1,   0,   0,   0,   0,  320,  200 },
    { ENH_VIEW_FRONT, 1920, 1080, true, 0, false, 1280, 800, 4,   0,   0,   0,   0, 1280,  800 },
    { ENH_VIEW_FIT,   1920, 1080, true, 2, false,  640, 400, 2,   0,   0,   0,   0,  640,  400 },
    { ENH_VIEW_MENU,  640, 480, true, 0, false,    640, 400, 2,   0,   0,   0,   0,  640,  400 },
};

static void test_geometry(void)
{
    for (size_t i = 0; i < sizeof geom / sizeof geom[0]; i++) {
        const GeomCase *g = &geom[i];
        EnhCanvas c;
        enh_canvas_geometry(&c, g->view, g->win_w, g->win_h, g->a43, g->fixed, g->wide);
        CHECK(c.lw == g->lw && c.lh == g->lh && c.n == g->n && c.fx == g->fx && c.fy == g->fy
              && c.vx == g->vx && c.vy == g->vy && c.vw == g->vw && c.vh == g->vh,
              "geometry %zu (view %d, %dx%d): got %dx%d n=%d at %d,%d viewport %d,%d %dx%d", i, g->view, g->win_w, g->win_h,
              c.lw, c.lh, c.n, c.fx, c.fy, c.vx, c.vy, c.vw, c.vh);
    }
}

/* S3.4 (--wide-geometry-test, ctest wide_geometry): the placement of every view for windows of several aspects. Extended (the first cockpit view, the outside views):
 * the canvas is the window, the frame bottom centre, the 3D viewport the whole canvas. Not extended (the second cockpit view = view 0 with [294B] = 1, left, right,
 * rear, menus): the frame centred, the viewport the frame, so the rest of the canvas is black. Prints one line per window and view class. */
int enh_wide_geometry_test(void)
{
    static const struct { const char *name; int w, h; bool a43; } win[] = {
        { "16:9", 1920, 1080, true }, { "16:9 1:1", 1920, 1080, false }, { "21:9", 2560, 1080, true }, { "21:9 1:1", 3440, 1440, false },
        { "32:9", 5120, 1440, true }, { "4:3", 1600, 1200, true }, { "16:10", 1920, 1200, true }, { "tall", 1080, 1920, true }, { "small", 700, 500, true },
    };
    static const struct { const char *name; int mode, style; EnhView want; } views[] = {
        { "first cockpit (0)", 0, 0, ENH_VIEW_FRONT }, { "second cockpit (0, 294B=1)", 0, 1, ENH_VIEW_SIDE }, { "rear (41)", 0x41, 0, ENH_VIEW_FIT },
        { "left (42)", 0x42, 0, ENH_VIEW_FIT }, { "right (43)", 0x43, 0, ENH_VIEW_FIT }, { "front clean (44)", 0x44, 0, ENH_VIEW_FULL },
        { "delayed (84)", 0x84, 0, ENH_VIEW_FULL }, { "side (85)", 0x85, 0, ENH_VIEW_FULL }, { "chase (87)", 0x87, 0, ENH_VIEW_FULL },
        { "target shoulder (88)", 0x88, 0, ENH_VIEW_FULL }, { "weapon (89)", 0x89, 0, ENH_VIEW_FULL }, { "target far (8B)", 0x8B, 0, ENH_VIEW_FULL },
        { "eject (8C)", 0x8C, 0, ENH_VIEW_FULL },
    };
    int fails = 0, lines = 0;
    for (size_t v = 0; v < sizeof views / sizeof views[0]; v++)
        if (enh_view_class(views[v].mode, views[v].style) != views[v].want) { printf("FAIL: %s is class %d, want %d\n", views[v].name, enh_view_class(views[v].mode, views[v].style), views[v].want); fails++; }
    for (size_t k = 0; k < sizeof win / sizeof win[0]; k++)
        for (int pass = 0; pass < 4; pass++) {
            const EnhView cls = pass == 0 ? ENH_VIEW_FRONT : pass == 1 ? ENH_VIEW_FULL : pass == 2 ? ENH_VIEW_FIT : ENH_VIEW_SIDE;
            for (int fixed = 0; fixed <= 4; fixed += 4) {
                EnhCanvas c;
                enh_canvas_geometry(&c, cls, win[k].w, win[k].h, win[k].a43, fixed, true);
                const int lw = win[k].w, lh = win[k].a43 ? win[k].h * 5 / 6 : win[k].h;
                int fit = lw / 320 < lh / 200 ? lw / 320 : lh / 200;
                if (fit < 1) fit = 1;
                const bool hfit = cls == ENH_VIEW_FIT || cls == ENH_VIEW_SIDE;     /* S3.4 fix: filling the window height (the host scales the canvas) */
                const int n = fixed ? fixed : hfit ? (lh + 199) / 200 : fit;
                int clw = lw, clh = lh;
                if (lw < 320 * n || lh < 200 * n) {                       /* the window's aspect, grown to hold the frame */
                    const long long w = ((long long)200 * n * lw + lh - 1) / lh;
                    if (w >= 320 * n) { clw = (int)w; clh = 200 * n; } else { clw = 320 * n; clh = (int)(((long long)320 * n * lh + lw - 1) / lw); }
                }
                if (cls == ENH_VIEW_FIT) { clw = 320 * n; clh = 200 * n; }
                if (cls == ENH_VIEW_SIDE) { clh = 200 * n; clw = (int)((long long)lw * clh / lh); if (clw < 320 * n) clw = 320 * n; }
                const bool ext = cls != ENH_VIEW_FIT;
                bool ok = c.n == n && c.lw == clw && c.lh == clh && c.fx == (clw - 320 * n) / 2;
                if (hfit) ok = ok && c.fy == 0 && c.lh == 200 * n;   /* top to bottom: the canvas is the frame's height, the host fits it to the window height */
                if (cls == ENH_VIEW_FIT) ok = ok && c.lw == 320 * n && c.vx == c.fx && c.vw == 320 * n && c.vh == 200 * n;   /* not extended: the sides are the host's black bars */
                else if (cls == ENH_VIEW_SIDE) ok = ok && c.vx == 0 && c.vy == 0 && c.vw == clw && c.vh == 200 * n;          /* extended left and right only */
                else ok = ok && c.fy == clh - 200 * n && c.vx == 0 && c.vy == 0 && c.vw == clw && c.vh == clh;
                /* how large one original pixel is shown: hfit = the window height / 200 (the host scales the canvas), else N */
                const double scale = hfit ? (double)win[k].h / (200.0 * (win[k].a43 ? 1.2 : 1.0)) : (double)n;
                /* the extensions in original pixels (DESIGN 3.1): left, right, top */
                const int el = c.fx / n, er = (c.lw - c.fx - 320 * n) / n, et = c.fy / n;
                if (!ok) fails++;
                if (lines++ < 200)
                    printf("%-9s %4dx%-4d %s render-scale %d %-5s canvas %4dx%-4d N=%d frame at %4d,%-4d viewport %4d,%-4d %4dx%-4d extension L %3d R %3d T %3d, 1x pixel shown x%.3f%s\n",
                           win[k].name, win[k].w, win[k].h, win[k].a43 ? "4:3 px" : "1:1 px", fixed,
                           cls == ENH_VIEW_FRONT ? "front" : cls == ENH_VIEW_FULL ? "full" : cls == ENH_VIEW_FIT ? "fit" : "side",
                           c.lw, c.lh, c.n, c.fx, c.fy, c.vx, c.vy, c.vw, c.vh, ext ? el : 0, ext ? er : 0, ext && !hfit ? et : 0, scale, ok ? "" : "  FAIL");
            }
        }
    printf("wide-geometry %s: %d views classified, %d window / view / scale cases, %d failures\n", fails ? "FAILED" : "OK",
           (int)(sizeof views / sizeof views[0]), lines, fails);
    return fails ? 1 : 0;
}

static unsigned rng_state = 12345;
static unsigned rnd(void) { rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 8; }

static void random_dac(void)
{
    for (int i = 0; i < 256; i++) vga_dac_set((u8)i, (u8)rnd(), (u8)rnd(), (u8)rnd());
}

static void random_page(int seg)
{
    u8 *p = vga_page((u16)seg);
    for (int i = 0; i < 64000; i++) p[i] = (u8)rnd();
}

/* canvas == the faithful frame scaled n x n at (fx, fy), black elsewhere */
static void compare(const char *what, const u32 *canvas, const u32 *classic)
{
    const EnhCanvas *c = &enh_cv;
    long diff = 0;
    for (int y = 0; y < c->lh; y++)
        for (int x = 0; x < c->lw; x++) {
            int fxp = x - c->fx, fyp = y - c->fy;
            u32 want = 0;
            if (fxp >= 0 && fyp >= 0 && fxp < 320 * c->n && fyp < 200 * c->n) want = classic[(fyp / c->n) * 320 + fxp / c->n];
            if (canvas[y * c->lw + x] != want) diff++;
        }
    CHECK(diff == 0, "%s: %ld canvas pixels differ from the classic frame scaled %dx%d", what, diff, c->n, c->n);
}

static void test_compose(void)
{
    static const struct { int win_w, win_h; bool a43; int fixed; bool wide; int view; } cfgs[] = {
        { 960, 720, true, 0, false, 0 },    { 960, 720, true, 1, false, 0 },    { 960, 720, true, 3, false, 0x41 },
        { 1280, 720, false, 0, true, 0 },   { 1000, 700, true, 2, true, 0x42 }, { 2560, 1080, false, 0, true, 0x44 },
        { 1920, 1080, true, 0, true, 0x1234 }, { 1000, 1000, false, 0, true, -1 },
    };
    static u32 classic[64000];
    for (size_t k = 0; k < sizeof cfgs / sizeof cfgs[0]; k++) {
        enh.wide = cfgs[k].wide;
        enh.render_scale = cfgs[k].fixed;
        vga_set_mode(0x13);                                       /* resets the view to the menu placement */
        if (cfgs[k].view >= 0) enh_set_view(cfgs[k].view);
        enh_canvas_update(cfgs[k].win_w, cfgs[k].win_h, cfgs[k].a43);
        u32 *canvas = malloc((size_t)enh_cv.lw * (size_t)enh_cv.lh * sizeof(u32));
        if (!canvas) { bad++; return; }
        for (int f = 0; f < 4; f++) {
            vga_set_mode(0x13);
            if (cfgs[k].view >= 0) enh_set_view(cfgs[k].view);
            random_dac();
            random_page(0xA000);
            random_page(0xA400);
            page_seg_set(0, 0xA000);
            page_seg_set(1, 0xA400);
            page_select(1);                                       /* draw into page 1 through the driver, flip to page 0 */
            for (int i = 0; i < 40; i++) {
                color_set((int)(rnd() & 255));
                line((int)(rnd() % 320), (int)(rnd() % 200), (int)(rnd() % 320), (int)(rnd() % 200));
            }
            flip(32000);
            vga_outw(0x3D4, (u16)((f & 3) << 8 | 0x0D));          /* the explosion shake: CRTC start low */
            vga_in(0x3DA); vga_out(0x3C0, 0x13); vga_out(0x3C0, (u8)((f * 2) & 7));   /* pel panning */
            if (f == 3) { vga_out(0x3C4, 1); vga_out(0x3C5, 0x20); }   /* screen off: black */
            enh_canvas_update(cfgs[k].win_w, cfgs[k].win_h, cfgs[k].a43);
            CHECK((size_t)enh_cv.lw * enh_cv.lh * sizeof(u32) != 0, "empty canvas");
            enh_compose_vga(canvas);
            if (f == 3) memset(classic, 0, sizeof classic); else vga_compose_13(classic);
            char what[96];
            snprintf(what, sizeof what, "config %zu frame %d (%dx%d n=%d at %d,%d)", k, f, enh_cv.lw, enh_cv.lh, enh_cv.n, enh_cv.fx, enh_cv.fy);
            compare(what, canvas, classic);
        }
        free(canvas);
    }
}

/* The layers do show where the shadow plane says so (3D pixel, vector pixel over what was under it). */
static void test_layers(void)
{
    enh.wide = false; enh.render_scale = 3;
    vga_set_mode(0x13);
    enh_canvas_update(960, 720, true);
    const EnhCanvas c = enh_cv;
    u32 *canvas = malloc((size_t)c.lw * c.lh * sizeof(u32));
    if (!canvas) { bad++; return; }
    for (int i = 0; i < 256; i++) vga_dac_set((u8)i, (u8)i, (u8)(255 - i), (u8)(i * 7));
    memset(vga_page(0xA400), 5, 64000);
    page_seg_set(0, 0xA000); page_seg_set(1, 0xA400); page_select(1);
    color_set(200);
    enh_route = ENH_ROUTE_3D;                                       /* a 3D pixel at (10,10) */
    vga_page(0xA400)[10 * 320 + 10] = 200;
    enh_rec_line(0xA400, 10 * 320 + 10, 200);
    enh_route = ENH_ROUTE_ART;
    for (int ky = 0; ky < c.n; ky++)
        for (int kx = 0; kx < c.n; kx++) enh_layer3d()[(c.fy + 10 * c.n + ky) * c.lw + c.fx + 10 * c.n + kx] = 0x100 | 77;
    color_set(150);
    line(50, 50, 50, 50);                                           /* a vector pixel at (50,50) over the 5 */
    enh_layer_vec()[(c.fy + 50 * c.n) * c.lw + c.fx + 50 * c.n] = 0x100 | 99;
    flip(32000);
    enh_compose_vga(canvas);
    u32 pal[256];
    for (int i = 0; i < 256; i++) { u8 r, g, b; vga_dac_get((u8)i, &r, &g, &b); pal[i] = (u32)(r << 2 | r >> 4) << 16 | (u32)(g << 2 | g >> 4) << 8 | (u32)(b << 2 | b >> 4); }
    CHECK(canvas[(c.fy + 10 * c.n + 1) * c.lw + c.fx + 10 * c.n + 2] == pal[77], "3D layer block not shown");
    CHECK(canvas[(c.fy + 50 * c.n) * c.lw + c.fx + 50 * c.n] == pal[99], "vector layer pixel not shown");
    CHECK(canvas[(c.fy + 50 * c.n + 1) * c.lw + c.fx + 50 * c.n + 1] == pal[5], "vector block does not show what was under it");
    CHECK(canvas[(c.fy + 20 * c.n) * c.lw + c.fx + 20 * c.n] == pal[5], "plain art not shown");
    free(canvas);
}

int enh_compose_selftest(void)
{
    if (!enh_canvas_on()) { printf("compose-test: needs the canvas (not --classic)\n"); return 1; }
    test_geometry();
    enh_hud_hr_off = true;     /* the compositor alone: the 1x lines of this test have no hr look (S3.3 emits one for every line) */
    test_compose();
    enh_hud_hr_off = false;
    test_layers();
    if (bad) { printf("compose-test: %d failures\n", bad); return 1; }
    printf("compose-test OK: %zu geometries, 8 canvases x 4 frames pixel-exact against the classic frame\n", sizeof geom / sizeof geom[0]);
    return 0;
}
