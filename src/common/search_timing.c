#define _POSIX_C_SOURCE 200809L

#include "search_timing.h"

#include <stdio.h>
#include <time.h>

int search_monotonic_seconds(double *clock_seconds)
{
    struct timespec current_time;
    if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0) {
        perror("Error al consultar el reloj");
        return 0;
    }
    *clock_seconds = (double)current_time.tv_sec +
                     (double)current_time.tv_nsec / 1000000000.0;
    return 1;
}
