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
    { ENH_VIEW_FIT,   1920, 1080, true, 0, true, 1920,  900, 4, 320,  50, 320,  50, 1280,  800 },
    { ENH_VIEW_MENU,  1920, 1080, true, 0, true, 1920,  900, 4, 320,  50, 320,  50, 1280,  800 },
    { ENH_VIEW_FRONT, 3440, 1440, false, 0, true, 3440, 1440, 7, 600,  40,   0,   0, 3440, 1440 },
    { ENH_VIEW_FIT,   3440, 1440, false, 0, true, 3440, 1440, 7, 600,  20, 600,  20, 2240, 1400 },
    { ENH_VIEW_FRONT, 1920, 1080, false, 8, true, 1920, 1080, 5, 160,  80,   0,   0, 1920, 1080 },
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
    test_compose();
    test_layers();
    if (bad) { printf("compose-test: %d failures\n", bad); return 1; }
    printf("compose-test OK: %zu geometries, 8 canvases x 4 frames pixel-exact against the classic frame\n", sizeof geom / sizeof geom[0]);
    return 0;
}
