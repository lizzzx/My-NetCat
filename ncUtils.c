#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/time.h>

static bool show_verbose = false;

static bool show_log = false;

void enable_verbose()
{
    show_verbose = true;
}

void nc_log(const char *format, ...)
{
    if (show_log)
    {
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
    }
}

void nc_verbose(const char *format, ...)
{
    if (show_verbose)
    {
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
    }
}

long get_current_time()
{
    struct timeval time;
    gettimeofday(&time, 0);
    return time.tv_sec;
}
