/* S3.6 true night (DESIGN.md 2.12): the night plane of the 3D layer.
 *
 * In a night mission ([3658] != 0) with --true-night, every layer pixel the hr back end draws in the main 3D window gets a brightness factor nd / 256 in the night
 * plane (0 = shown as the original): the backdrop (sky / ground of 1442) by its elevation or its ground distance from the band geometry of 2.11, the objects by the
 * depth their shade uses (1088:0120), lights (var 10 / 11 faces, the light colours) none. The compositor multiplies the colour from the live DAC by it.
 * Nothing here writes mem[], the page or the VGA model: only hr state and the night plane. */
#include "enhanced/hr.h"

#include <math.h>
#include <string.h>

#include "enhanced/enh_canvas.h"
#include "enhanced/enhanced.h"
#include "mem.h"

/* ---- the tuned constants (DESIGN 2.12) */
#define K_NEAR      0.50       /* ground and objects at depth 0: half their night brightness */
#define K_FAR       0.06       /* far away: near black */
#define D_LIT       3000.0     /* the e-folding depth of the fade (depth units of [4960] >> 16, the units of [4980]) */
#define SKY_HORIZON 0.30       /* the sky at the horizon */
#define SKY_TOP     0.10       /* the sky far above it */
#define SKY_E       0.15       /* the e-folding elevation (tan of the angle above the horizon) */
#define D_HORIZON   131072.0   /* the ground ring of the horizon line (the band formula: (16 - 0) 8192) */

#define LUT_STEP 32.0          /* the depth curve in steps of 32 depth units up to the horizon ring */
#define LUT_N 4097
static u8 lut_d[LUT_N];
#define SKY_LUT_N 1025         /* the sky factor in steps of 1/256 elevation */
static u8 lut_s[SKY_LUT_N];
static bool lut_ready;

/* the hr state of the frame being drawn (in the display-pass snapshot) */
static struct { u8 obj; u8 queued[50]; u8 cond, light; } st;

void hr_night_regions(void (*add)(void *p, size_t n)) { add(&st, sizeof st); }

static u8 to_nd(double k)
{
    int v = (int)lround(k * 256.0);
    return (u8)(v < 1 ? 1 : v > 255 ? 255 : v);
}

static double curve(double d) { if (d < 0) d = 0; return K_FAR + (K_NEAR - K_FAR) * exp(-d / D_LIT); }

static void lut_init(void)
{
    if (lut_ready) return;
    for (int i = 0; i < LUT_N; i++) lut_d[i] = to_nd(curve(i * LUT_STEP));
    for (int i = 0; i < SKY_LUT_N; i++) lut_s[i] = to_nd(SKY_TOP + (SKY_HORIZON - SKY_TOP) * exp(-(i / 256.0) / SKY_E));
    lut_ready = true;
}

static inline u8 nd_depth(double d)
{
    if (!(d > 0)) return lut_d[0];
    double i = d / LUT_STEP;
    return i >= LUT_N - 1 ? lut_d[LUT_N - 1] : lut_d[(int)(i + 0.5)];
}

void hr_night_pass_begin(bool main_win)
{
    enh_hr.night = NULL;
    memset(&st, 0, sizeof st);
    if (!main_win || !enh_true_night_on() || !DSW(0x3658) || DSB(0x7D5E)) return;   /* the main 3D window of a night mission, not the map */
    lut_init();
    enh_hr.night = enh_layer_night();
}

/* ---------------------------------------------------------------- objects */

/* at 1088:0120's inputs: the view depth C [4960] of the object's origin less 512 per size class [497A] (the depth the original shade steps on) */
static u8 obj_now(void)
{
    if (!enh_hr.night) return 0;
    s32 C = (s32)DSSL(0x4960);
    double d = (double)C / 65536.0 - 512.0 * DSB(0x497A);
    return nd_depth(d);
}

void hr_night_object(void) { st.obj = obj_now(); }
void hr_night_queue(int slot) { if (slot >= 0 && slot < 50) st.queued[slot] = obj_now(); }
void hr_night_dequeue(int slot) { st.obj = slot >= 0 && slot < 50 ? st.queued[slot] : 0; }

/* r3d_face_list: a condition on the lights variables (10, 11) holds for the next element; the element begins */
void hr_night_cond(u16 v) { if (v == 10 || v == 11) st.cond = 1; }
void hr_night_elem(void) { st.light = st.cond; st.cond = 0; }

/* the factor of the object fill / line being drawn (0 = none: no night pass, a light element) */
u8 hr_night_fill(void) { return enh_hr.night && !st.light ? st.obj : 0; }

/* the light colours (unchanged by the night palettes): the EGA colours, the fire ramp, 90h..97h */
bool hr_night_lit(u8 c) { return c < 0x10 || (c >= 0x50 && c <= 0x5F) || (c >= 0x90 && c <= 0x97); }

/* ---------------------------------------------------------------- the backdrop */

/* after 1442 (every detail level): each written, not stamped pixel of the pass window gets the sky factor of its elevation or the ground factor of its distance.
 * t(q) = -(sn (qx - w2) + cs (qy - h2) 4/3) in 1x t-units (2.11); H the horizon; the focal f = 256 << [433A], halved with [294B]; alt = [4980]. */
void hr_night_backdrop(s16 H)
{
    if (!enh_hr.night || !enh_hr.on || !enh_hr.layer) return;
    const double N = enh_hr.n;
    const double sn = (double)(s16)(u16)-DSW(0x498A) / 32768.0, cs = (double)(s16)DSW(0x498C) / 32768.0;
    const double w2 = (s16)DSW(0x0D1E), h2 = (s16)DSW(0x0D20);
    const double ox = (double)enh_hr.ox + enh_hr.e2dx, oy = (double)enh_hr.oy + enh_hr.e2dy;
    const double ax = -sn / N, ay = -cs * (4.0 / 3.0) / N;           /* t = ax X + ay Y + c0 (layer px) */
    const double c0 = sn * (ox / N + w2) + cs * (4.0 / 3.0) * (oy / N + h2);
    double f = 256.0;
    const u8 z = DSB(0x433A);
    if (z && (z & 0x1F) < 16) f *= (double)(1 << (z & 0x1F));
    if (DSB(0x294B)) f /= 2;
    s16 alt = (s16)DSW(0x4980);
    const double fa = f * (alt > 1 ? alt : 1);
    for (s32 y = enh_hr.ymin; y <= enh_hr.ymax; y++) {
        if (y < 0 || y >= enh_hr.lh) continue;
        const u16 *row = enh_hr.layer + (size_t)y * enh_hr.lw;
        u8 *nrow = enh_hr.night + (size_t)y * enh_hr.lw;
        const double ty = ay * y + c0;
        for (s32 x = enh_hr.xmin < 0 ? 0 : enh_hr.xmin; x <= enh_hr.xmax && x < enh_hr.lw; x++) {
            const u16 l = row[x];
            if ((l & 0x300) != 0x100 || HR_IN_HOLE(x, y)) continue;
            const u8 c = (u8)l;
            const double t = ax * x + ty - H;                      /* > 0 above the horizon */
            bool sky = t >= 0;
            if (c >= 0x80 && c <= 0x8F) sky = true;
            else if (c >= 0x70 && c <= 0x7F) sky = false;
            if (sky) {
                const double e = t > 0 ? t / f * 256.0 + 0.5 : 0;
                nrow[x] = lut_s[e < SKY_LUT_N - 1 ? (int)e : SKY_LUT_N - 1];
            } else {
                const double G = t < 0 ? -t : 0;
                nrow[x] = nd_depth(1.0 / (G / fa + 1.0 / D_HORIZON));
            }
        }
    }
}
