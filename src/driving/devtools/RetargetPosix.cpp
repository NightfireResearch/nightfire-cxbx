// CodeRetarget.cpp and X87Count.cpp off Windows: moving references reads the executable's PE image, and the x87
// counts find their functions in MSVC's link map, so neither has a Linux form. NIGHTFIRE_HEAP_PAD and
// NIGHTFIRE_X87COUNT say so and change nothing.

#include "CodeRetarget.h"
#include "X87Count.h"
#include "Win32Harness.h"

#include <stdio.h>

int Retarget(RetargetTarget *targets, int count, const void *, size_t) {
    for (int i = 0; i < count; i++)
        targets[i].sites = 0;
    printf("[retarget] needs the Windows build's PE image: no references moved\n");
    return 0;
}

void X87Count_Install(void) {
    char text[16];
    if (GetEnvironmentVariableA("NIGHTFIRE_X87COUNT", text, sizeof(text)) != 0 && text[0] != '0')
        printf("[x87count] needs the Windows build's link map: not counting\n");
}

void X87Count_Tick(void) {
}
