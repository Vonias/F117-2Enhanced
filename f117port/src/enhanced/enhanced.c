/* The Enhanced options (see enhanced.h). */
#include "enhanced.h"

#include <string.h>

EnhOptions enh;

int enh_parse_arg(int argc, char **argv, int i)
{
    (void)argc;
    if (!strcmp(argv[i], "--classic")) { enh.classic = true; return 1; }
    return 0;
}
