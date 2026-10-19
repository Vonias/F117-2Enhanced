/* The Enhanced options (PLAN.md). Every engine edit that reads them is marked ENH:. --classic turns every
 * enhancement off: the game then behaves exactly as the faithful release v0.1.0 (the faithful ctests pass). */
#pragma once

#include <stdbool.h>

typedef struct {
    bool classic;      /* --classic: no enhancement at all */
} EnhOptions;

extern EnhOptions enh;

/* Parses one Enhanced command-line option at argv[i]; returns how many arguments it took (0 = not ours). */
int enh_parse_arg(int argc, char **argv, int i);
