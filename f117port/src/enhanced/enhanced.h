/* The Enhanced options (PLAN.md). Every engine edit that reads them is marked ENH:. --classic turns every
 * enhancement off: the game then behaves exactly as the faithful release v0.1.0 (the faithful ctests pass). */
#pragma once

#include <stdbool.h>

typedef struct {
    bool classic;        /* --classic: no enhancement at all */
    bool canvas;         /* the layered compositor and the canvas (default on; off with --classic) */
    int  render_scale;   /* --render-scale N: the frame is shown N x N (0 = the largest whole multiple that fits the window) */
    bool wide;           /* --wide: the canvas follows the window's aspect, the 320x200 frame bottom centre (DESIGN 3.1) */
    bool compose_test;   /* --compose-test: headless check of the canvas geometry and the compositor */
    bool dither_fine;    /* --dither fine: the 3D checkerboard per layer pixel; --dither grid (default): per original pixel (DESIGN 2.4, Q2) */
    bool hr_check;       /* --hr-check: the render test (--render-test) also checks the hr 3D layer against the 1x page (DIR/hr_check.txt) */
    char hr_dump[1024];  /* --hr-dump DIR (implies --hr-check): per render-test scenario DIR/<id>.hrd: the hr trace, the layer, the page (S2.3) */
    int  sandbox_test;   /* --sandbox-test N: N sandboxed display passes per VGAME frame, nothing shown (S2.4 test; S4.1 runs them for real) */
    char sandbox_hash[1024];   /* --sandbox-hash FILE: a hash of the sim state per VGAME frame (test aid; also in --classic, it changes nothing) */
    char mfd_probe[1024];      /* --mfd-probe PREFIX (S3.2 test aid): per page flip PREFIX.txt gets how much of the TV and map windows is shown from the 3D layer; F117_PROBE_FRAMES=a,b dumps those frames */
} EnhOptions;

extern EnhOptions enh;

/* Parses one Enhanced command-line option at argv[i]; returns how many arguments it took (0 = not ours). */
int enh_parse_arg(int argc, char **argv, int i);
/* True when the canvas compositor is wanted (not --classic). */
bool enh_canvas_on(void);
/* Display passes wanted (not --classic, not F117_CLASSIC). */
bool enh_sandbox_on(void);
/* Options from the environment variable F117_ENH_ARGS (for test drivers that build their own command line); once. */
void enh_env_args(void);
