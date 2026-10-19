/* The hr pass context (DESIGN.md 2.2, 2.5; S2.2): r3d_set_viewport (DB71) opens a 3D pass (route 3D, the window's shadow, 3D and
 * vector layer cleared, the hr window and centre at N x); r3d_end_scene (DB3E) closes it. A pass draws into the layer only when its
 * page is page 0 or 1 (the pages the shadow plane follows). Also the hr point of B85D and the --hr-check statistics of the render test. */
#include "enhanced/hr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enhanced/enh_canvas.h"
#include "enhanced/enhanced.h"
#include "render3d/r3d.h"

HrCtx enh_hr;
HrPoint enh_hr_pt;

/* --hr-check statistics of the current scenario */
static struct {
    long checked;     /* 1x pixels of a pass window whose last write was 3D and is still on the page */
    long hr, stamped; /* layer pixels over them written by the hr back end / stamped with the 1x value */
    long agree, differ, empty;   /* layer pixels shown (written, else the 1x value: the compositor) equal / not equal to the 1x pixel; not written */
    long inner, inner_differ;    /* the same in "interior" blocks (the 3x3 1x pixels around are all the same value): away from every edge */
    int passes;
} hs;

static void pass_stats(void)
{
    const int n = enh_hr.n;
    const u8 *pg = seg_ptr(enh_hr.seg);
    for (int y = enh_hr.y0; y <= enh_hr.y1 && y < 200; y++)
        for (int x = enh_hr.x0; x <= enh_hr.x1 && x < 320; x++) {
            u8 val;
            u16 off = (u16)(y * 320 + x);
            if (!enh_shadow_3d(off, &val) || pg[off] != val) continue;
            hs.checked++;
            bool inner = x > enh_hr.x0 && x < enh_hr.x1 && y > enh_hr.y0 && y < enh_hr.y1;
            for (int dy = -1; dy <= 1 && inner; dy++)
                for (int dx = -1; dx <= 1; dx++) if (pg[off + dy * 320 + dx] != val) { inner = false; break; }
            for (int ky = 0; ky < n; ky++)
                for (int kx = 0; kx < n; kx++) {
                    u16 l = enh_hr.layer[(size_t)(enh_hr.fy + y * n + ky) * enh_hr.lw + (size_t)(enh_hr.fx + x * n + kx)];
                    if (l & 0x200) hs.stamped++; else if (l & 0x100) hs.hr++;
                    if (!(l & 0x100)) hs.empty++;
                    bool same = !(l & 0x100) || (u8)l == val;
                    if (same) hs.agree++; else hs.differ++;
                    if (inner) { hs.inner++; if (!same) hs.inner_differ++; }
                }
        }
    hs.passes++;
}

void enh_r3d_viewport(u16 vp)
{
    (void)vp;
    if (enh_hr.on && enh.hr_check) pass_stats();             /* a pass left open (a nested begin without its end) */
    enh_route = ENH_ROUTE_3D;
    enh_hr.on = false;
    u16 seg = DSW(0x85EA);
    if (!enh_page_tracked(seg)) return;
    const int x0 = DSS(0x85CA), y0 = DSS(0x85CC), x1 = DSS(0x85CE), y1 = DSS(0x85D0);
    enh_pass_clear(x0, y0, x1, y1);
    u16 *layer = enh_layer3d();
    if (!layer || x0 < 0 || y0 < 0 || x1 > 319 || y1 > 199 || x1 < x0 || y1 < y0) return;
    HrCtx *h = &enh_hr;
    const int n = enh_cv.n;
    h->n = n; h->fx = enh_cv.fx; h->fy = enh_cv.fy; h->lw = enh_cv.lw; h->lh = enh_cv.lh;
    h->layer = layer; h->seg = seg;
    h->x0 = x0; h->y0 = y0; h->x1 = x1; h->y1 = y1;
    h->xmin = h->fx + n * x0; h->xmax = h->fx + n * (x1 + 1) - 1;
    h->ymin = h->fy + n * y0; h->ymax = h->fy + n * (y1 + 1) - 1;
    h->cx = h->fx + n * DSS(0x85D2); h->cy = h->fy + n * DSS(0x85D4);
    h->ox = h->xmin; h->oy = h->ymin;
    h->exmax = n * (DSS(0x2293) + 1) - 1; h->eymax = n * (DSS(0x2295) + 1) - 1;
    h->line_w = n / 2 > 1 ? n / 2 : 1;
    h->line_o = (n - h->line_w) / 2;
    h->fine = enh.dither_fine;
    h->on = true;
    hr_raster_begin();
    hr_clip_begin();
}

void enh_r3d_end(void)
{
    if (enh_hr.on && enh.hr_check) pass_stats();
    enh_hr.on = false;
    enh_route = ENH_ROUTE_ART;
}

/* B85D at N x: lsl32(X, 8) wraps at 1x first (quirk), then N; sy - (sy >> 2) on the N x value; offsets N x. The point is meaningful
 * only when the call left [49E0] != FFFFh (an early return of B85D does not reach this hook). */
void hr_project_point(s32 X, s32 Y, s32 Z, bool visible)
{
    if (!enh_layer3d() || Z == 0) { enh_hr_pt.valid = false; return; }
    const int n = enh_cv.n;
    s64 xw = (s32)((u32)X << 8), yw = (s32)((u32)Y << 8);
    s64 qx = xw * n / Z, qy = yw * n / Z;
    qy -= qy >> 2;
    s64 oy = DSW(0x365E) != 0 ? (DSB(0x294B) == 0 ? 0x34 : 0x3C) : 0x64;
    enh_hr_pt.x = (s32)(enh_cv.fx + (s64)n * 0xA0 + qx);
    enh_hr_pt.y = (s32)(enh_cv.fy + (s64)n * oy + qy);
    enh_hr_pt.valid = visible;
}

/* ---------------------------------------------------------------- --hr-check (sim/simtest.c, --render-test) */

static bool hr_check_on(void)
{
    enh_env_args();
    return enh.hr_check && enh_canvas_on();
}

void enh_hr_test_begin(void)
{
    if (!hr_check_on()) return;
    int n = enh.render_scale > 0 ? enh.render_scale : 1;
    enh_canvas_set_active(true);
    enh_canvas_update(320 * n, 200 * n, false);
    enh_layers_reset();
    enh_hr.on = false;
    memset(&hs, 0, sizeof hs);
}

void enh_hr_test_end(const char *dir, const char *id)
{
    static bool first = true;
    if (!hr_check_on()) return;
    if (enh_hr.on) pass_stats();
    enh_hr.on = false;
    enh_route = ENH_ROUTE_ART;
    char path[2300];
    snprintf(path, sizeof path, "%s/hr_check.txt", dir);
    FILE *f = fopen(path, first ? "w" : "a");
    first = false;
    if (!f) return;
    fprintf(f, "%s n=%d passes=%d checked=%ld hr=%ld stamped=%ld agree=%ld differ=%ld empty=%ld inner=%ld inner_differ=%ld\n", id, enh_cv.n, hs.passes, hs.checked, hs.hr, hs.stamped,
            hs.agree, hs.differ, hs.empty, hs.inner, hs.inner_differ);
    fclose(f);
}
