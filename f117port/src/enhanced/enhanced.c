/* The Enhanced options (see enhanced.h). */
#include "enhanced.h"

#include <stdio.h>
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
        enh.classic = true; enh.canvas = false; enh.wide = false; enh.render_scale = 0; enh.dither_fine = false; enh.hr_check = false; enh.hr_dump[0] = 0;
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
    if (!strcmp(argv[i], "--dither") && i + 1 < argc) {
        if (!strcmp(argv[i + 1], "fine")) enh.dither_fine = !enh.classic;
        else if (!strcmp(argv[i + 1], "grid")) enh.dither_fine = false;
        else return 0;
        return 2;
    }
    if (!strcmp(argv[i], "--hr-check")) { if (!enh.classic) enh.hr_check = true; return 1; }
    if (!strcmp(argv[i], "--hr-dump") && i + 1 < argc) {
        if (!enh.classic) { snprintf(enh.hr_dump, sizeof enh.hr_dump, "%s", argv[i + 1]); enh.hr_check = true; }
        return 2;
    }
    return 0;
}

void enh_env_args(void)
{
    static bool done;
    if (done) return;
    done = true;
    const char *e = getenv("F117_ENH_ARGS");
    if (!e || !*e) return;
    static char buf[512];
    char *argv[32];
    int argc = 0;
    snprintf(buf, sizeof buf, "%s", e);
    for (char *t = buf; *t && argc < 32;) {                 /* not strtok: the caller may be inside a strtok loop (simtest) */
        while (*t == ' ' || *t == '\t') *t++ = 0;
        if (!*t) break;
        argv[argc++] = t;
        while (*t && *t != ' ' && *t != '\t') t++;
    }
    for (int i = 0; i < argc;) { int used = enh_parse_arg(argc, argv, i); i += used ? used : 1; }
}
