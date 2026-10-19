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
} EnhOptions;

extern EnhOptions enh;

/* Parses one Enhanced command-line option at argv[i]; returns how many arguments it took (0 = not ours). */
int enh_parse_arg(int argc, char **argv, int i);
/* True when the canvas compositor is wanted (not --classic). */
bool enh_canvas_on(void);
/* Options from the environment variable F117_ENH_ARGS (for test drivers that build their own command line); once. */
void enh_env_args(void);
