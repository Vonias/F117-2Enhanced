/* The Enhanced options (see enhanced.h). */
#include "enhanced.h"

#include <stdlib.h>
#include <string.h>

EnhOptions enh = { .canvas = true };

/* F117_CLASSIC=1 in the environment acts as --classic: the faithful ctests set it, because their Python drivers start the program with
 * their own command lines. */
static bool env_classic(void)
{
    static int v = -1;
    if (v < 0) { const char *e = getenv("F117_CLASSIC"); v = e && *e && *e != '0'; }
    return v != 0;
}

bool enh_canvas_on(void) { return !enh.classic && enh.canvas && !env_classic(); }

int enh_parse_arg(int argc, char **argv, int i)
{
    if (!strcmp(argv[i], "--classic")) {              /* every enhancement off, whatever else is on the command line */
        enh.classic = true; enh.canvas = false; enh.wide = false; enh.render_scale = 0;
        return 1;
    }
    if (!strcmp(argv[i], "--render-scale") && i + 1 < argc) {
        int n = atoi(argv[i + 1]);
        if (!enh.classic) enh.render_scale = n < 0 ? 0 : n > 16 ? 16 : n;
        return 2;
    }
    if (!strcmp(argv[i], "--wide")) { if (!enh.classic) enh.wide = true; return 1; }
    if (!strcmp(argv[i], "--no-wide")) { enh.wide = false; return 1; }
    if (!strcmp(argv[i], "--compose-test")) { enh.compose_test = true; return 1; }
    return 0;
}
