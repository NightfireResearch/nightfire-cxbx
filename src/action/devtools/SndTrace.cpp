#include "SndTrace.h"
#include "../game.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// See SndTrace.h. Opened on the first call; flushed by line, so a run cut off still leaves its trace.
void SndTrace(const char *format, ...) {
    static FILE *file;
    static bool checked;
    if (!checked) {
        checked = true;
        const char *name = getenv("NIGHTFIRE_SNDTRACE");
        if (name != NULL && name[0] != 0)
            file = fopen(name, "w");
    }
    if (file == NULL)
        return;
    fprintf(file, "f=%u ", (unsigned)GameState.NumFrames);
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputc('\n', file);
    fflush(file);
}
