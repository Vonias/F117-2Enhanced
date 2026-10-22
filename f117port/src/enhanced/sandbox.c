/* The sandboxed display pass (sandbox.h; DESIGN.md 4.1, 4.2, 4.7; S2.4). */
#include "enhanced/sandbox.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "mem.h"
#include "enhanced/enh_canvas.h"
#include "enhanced/enhanced.h"
#include "enhanced/wide.h"
#include "gen/module_simflow.h"
#include "gen/module_video.h"
#include "sim/sim_priv.h"
#include "platform/timer.h"
#include "platform/vga.h"
#include "sound/snd_priv.h"

bool enh_sbx;

/* ---------------------------------------------------------------- the region table (the machine image without the canvas) */

#define MAX_REGIONS 128
typedef struct { u8 *p; size_t n; } Region;
static Region reg[MAX_REGIONS];
static int nreg;
static size_t reg_bytes;
static bool reg_overflow;

static void reg_add(void *p, size_t n)
{
    if (!p || !n) return;
    if (nreg == MAX_REGIONS) { reg_overflow = true; return; }
    reg[nreg].p = p; reg[nreg].n = n; nreg++;
    reg_bytes += n;
}

/* Built at every frame start (cheap), so a driver image loaded again is followed. */
static void reg_build(void)
{
    nreg = 0; reg_bytes = 0;
    reg_add(mem, MEM_SIZE);                                   /* every DGROUP, the comm block, the DOS arena, the BIOS data area */
    reg_add(&cur_dgroup, sizeof cur_dgroup);
    reg_add(&dg_sp, sizeof dg_sp);                            /* the emulated stack of the render's locals */
    reg_add((void *)&timer_ticks, sizeof timer_ticks);
    vga_sandbox_regions(reg_add);
    mgraphic_sandbox_regions(reg_add);
    pic_sandbox_regions(reg_add);
    r3d_sandbox_regions(reg_add);
    hr_sandbox_regions(reg_add);
}

typedef struct {
    u8 *buf;
    size_t cap;
    EnhCanvasSnap cv;
    bool valid;
} Image;

static Image img_a;           /* A_k: the pre-render image of this frame */
static Image img_live;        /* the live machine during a pass */
static EnhCanvasSnap cv_scratch;   /* the copy of A_k's canvas a pass draws on; holds the live canvas while the pass runs */

static bool image_save(Image *im)
{
    if (reg_overflow) return false;
    if (im->cap < reg_bytes) {
        free(im->buf);
        im->buf = malloc(reg_bytes);
        im->cap = im->buf ? reg_bytes : 0;
        if (!im->buf) return false;
    }
    u8 *q = im->buf;
    for (int i = 0; i < nreg; i++) { memcpy(q, reg[i].p, reg[i].n); q += reg[i].n; }
    return true;
}

static void image_load(const Image *im)
{
    const u8 *q = im->buf;
    for (int i = 0; i < nreg; i++) { memcpy(reg[i].p, q, reg[i].n); q += reg[i].n; }
}

/* ---------------------------------------------------------------- statistics */

static unsigned long long n_passes, n_takes;
static unsigned long long ns_take, ns_snap, ns_render, ns_restore;
static unsigned violations;
static const char *first_violation;

bool enh_sandbox_deny(const char *what)
{
    if (!enh_sbx) return false;
    if (!violations++) first_violation = what;
    return true;
}

static void report(FILE *f)
{
    if (!n_passes && !n_takes) return;
    const double p = n_passes ? (double)n_passes : 1.0, t = n_takes ? (double)n_takes : 1.0;
    fprintf(f, "sandbox: %llu passes, image %zu B + canvas %zu B; A_k %.0f us, pass snapshot %.0f us, render %.0f us, restore %.0f us; violations %u%s%s\n",
            n_passes, reg_bytes, enh_canvas_snap_bytes(&img_a.cv), ns_take / t / 1000.0, ns_snap / p / 1000.0, ns_render / p / 1000.0,
            ns_restore / p / 1000.0, violations, violations ? " first " : "", violations ? first_violation : "");
}

/* ---------------------------------------------------------------- --sandbox-hash */

static FILE *hash_f;
static unsigned long long hash_frame;
static unsigned long long opl_n;
static u64 opl_h = 0xCBF29CE484222325ull;

static u64 fnv(u64 h, const void *p, size_t n)
{
    const u8 *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001B3ull; }
    return h;
}

void enh_opl_trace(u8 reg_, u8 val)
{
    const u8 w[2] = { reg_, val };
    opl_h = fnv(opl_h, w, 2);
    opl_n++;
}

static void at_exit(void)
{
    if (hash_f) {
        fprintf(hash_f, "final comm=%016llx opl=%llu:%016llx passes=%llu violations=%u\n",
                (unsigned long long)fnv(0xCBF29CE484222325ull, comm_get(), COMM_SIZE), opl_n, (unsigned long long)opl_h, n_passes, violations);
        report(hash_f);
        fclose(hash_f);
        hash_f = NULL;
    }
    report(stdout);
}

/* One line per VGAME frame: the state the sim step k-1 left (DGROUP, comm block, the LCG 926E, the sound driver memory, the OPL stream). */
static void hash_line(void)
{
    static bool opened;
    if (!opened) {
        opened = true;
        hash_f = fopen(enh.sandbox_hash, "w");
        atexit(at_exit);
    }
    if (!hash_f) return;
    const u64 h0 = 0xCBF29CE484222325ull;
    u64 dg = fnv(h0, DSP(0), 0x10000);
    u64 cm = fnv(h0, comm_get(), COMM_SIZE);
    u64 sd = snd_m ? fnv(h0, snd_m, SND_MEM) : 0;
    u64 all = fnv(h0, mem, MEM_SIZE);                         /* also the pages, the DOS arena, the other programs' memory */
    u64 vg = fnv(h0, vga_vram(), VGA_VRAM_SIZE);              /* the canonical frame shown: a display leak would show here */
    for (int i = 0; i < 256; i++) { u8 rgb[3]; vga_dac_get((u8)i, &rgb[0], &rgb[1], &rgb[2]); vg = fnv(vg, rgb, 3); }
    EnhCanvasSnap c;                                          /* the canvas: shadow plane and the four layers (a leak only the hr picture shows) */
    enh_canvas_snap_peek(&c);
    u64 cv = fnv(h0, &c.cv, sizeof c.cv);
    cv = fnv(cv, c.sh_work, 64000 * 4); cv = fnv(cv, c.sh_shown, 64000 * 4);
    const size_t lb = c.layer_px * sizeof(u16);
    if (lb) { cv = fnv(cv, c.l3d_work, lb); cv = fnv(cv, c.l3d_shown, lb); cv = fnv(cv, c.lvec_work, lb); cv = fnv(cv, c.lvec_shown, lb); }
    fprintf(hash_f, "%llu dg=%016llx comm=%016llx seed=%08x snd=%016llx opl=%llu:%016llx mem=%016llx vga=%016llx canvas=%016llx\n", hash_frame++,
            (unsigned long long)dg, (unsigned long long)cm, (unsigned)DSL(0x926E), (unsigned long long)sd, opl_n, (unsigned long long)opl_h,
            (unsigned long long)all, (unsigned long long)vg, (unsigned long long)cv);
}

/* ---------------------------------------------------------------- hooks */

static int frame_passes;
static bool enh_test_night(void) { static int v = -1; if (v < 0) { const char *e = getenv("F117_TEST_NIGHT"); v = e && *e == '1'; } return v == 1 && !enh.classic; }
static bool wide_due;          /* S3.4: this frame gets a wide display pass */

void enh_frame_begin(void)
{
    if (enh_test_night()) DSW(0x3658) = 1;   /* S3.4 test aid (F117_TEST_NIGHT=1, real-path tests only): the night flag, so 7898 draws its bars in 0x2A */
    if (enh.sandbox_hash[0]) hash_line();
    frame_passes = 0;
    img_a.valid = false;
    if (!enh_sbx) enh_canvas_frame_view(DSW(0xC07A), DSB(0x294B));   /* S3.4 fix 4: this frame's placement first */
    wide_due = !enh_sbx && enh_wide_due();
    if (!enh_sandbox_on() && !wide_due) return;
    static bool at_exit_set;
    if (!at_exit_set) { at_exit_set = true; if (!enh.sandbox_hash[0]) atexit(at_exit); }
    Uint64 t0 = SDL_GetTicksNS();
    reg_build();
    img_a.valid = image_save(&img_a) && enh_canvas_snap_take(&img_a.cv);
    ns_take += SDL_GetTicksNS() - t0;
    n_takes++;
}

void enh_idle(void)
{
    if (!img_a.valid || enh_sbx) return;
    while (frame_passes < enh.sandbox_test) {
        frame_passes++;
        enh_display_pass(frame_passes);
    }
    if (wide_due) {                                           /* S3.4: the frame again with the culling widened to the canvas; its picture is shown with the frame */
        wide_due = false;
        enh_wide_pass = true;
        enh_display_pass(0);
        enh_wide_pass = false;
    }
}

/* The test view of --sandbox-test: pass v of a frame looks from another place and heading than the canonical frame (v x 5.6 degrees,
 * v x 96 / 64 world units), so that its pictures, designators, tracer hits and driver state really differ from the frame's and any of
 * it that leaked out of the pass would show in the next frames' hashes. 2D94 (the camera matrix of the front view) is rebuilt from
 * the angles as DESIGN 4.2 step 2 does for the interpolated passes of S4.1. */
static void test_view(int v)
{
    if (v <= 0) return;
    DSW(0x2DEE) = (u16)(DSW(0x2DEE) + 0x0400 * v);
    DSSL(0xBA10) += 96 * v;
    DSSL(0xC02A) += 64 * v;
    sim_matrix_from_angles(0x2D94, (s16)DSW(0x2DEE), (s16)DSW(0x2DF0), (s16)DSW(0x2DF2));
}

/* DESIGN 4.2 steps 1-5. S2.4 renders A_k (with the test view v > 0, or as it is with v = 0; no interpolated fields, no wide culling)
 * and presents nothing. */
void enh_display_pass(int v)
{
    if (!img_a.valid || enh_sbx) return;
    Uint64 t0 = SDL_GetTicksNS();
    if (!image_save(&img_live) || !enh_canvas_snap_copy(&cv_scratch, &img_a.cv)) { img_a.valid = false; return; }
    enh_canvas_snap_swap(&cv_scratch);                        /* the live canvas is now a copy of A_k's; cv_scratch holds the live one */
    image_load(&img_a);
    Uint64 t1 = SDL_GetTicksNS();

    enh_sbx = true;
    enh_wide_drawn = false;
    if (enh_wide_pass) DSW(0x2E48) = DSW(0xC07A);             /* S3.4 fix 4: the cockpit picture counts as loaded (no file read in a pass); only the pass's 3D layer is used */
    test_view(v);
    view_frame();                                             /* the faithful frame's render, as frame_loop_body runs it */
    hud_overlay_frame();
    if (DSW(0xC07A) == 0) {
        hud_draw_far();
        hud_side_strips(DSB(0x4088));
        cockpit_draw_adi();
    }
    flip(DSW(0x2E34) * 160);
    if (enh_wide_pass) enh_wide_capture();                    /* S3.4: the pass's layers (after its flip) are the pending wide picture */
    enh_wide_on = enh_wide_pt = false;
    enh_sbx = false;
    Uint64 t2 = SDL_GetTicksNS();

    image_load(&img_live);
    enh_canvas_snap_swap(&cv_scratch);                        /* the live canvas back, untouched */
    Uint64 t3 = SDL_GetTicksNS();
    ns_snap += t1 - t0; ns_render += t2 - t1; ns_restore += t3 - t2;
    n_passes++;
}
