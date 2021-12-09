
#include <GL/freeglut.h>
#include "freeglut_internal.h"

#include <stdio.h>
#include <stdarg.h>

void fgWarning(const char* fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);

    fprintf(stderr, "freeglut ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");

    va_end(ap);
}

void fgError(const char* fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);

    fprintf(stderr, "freeglut ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");

    va_end(ap);

    exit(1);
}

SFG_State fgState;