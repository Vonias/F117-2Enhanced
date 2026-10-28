#pragma once
/* VR: the two SDL names enhanced/sandbox.c uses, for the Quest build (no SDL). */
#include <stdint.h>
#include <time.h>
typedef uint64_t Uint64;
static inline Uint64 SDL_GetTicksNS(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (Uint64)ts.tv_sec * 1000000000ull + (Uint64)ts.tv_nsec;
}
