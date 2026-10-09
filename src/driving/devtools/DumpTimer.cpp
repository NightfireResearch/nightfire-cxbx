// NIGHTFIRE_DUMP_WALL_MS=N (with NIGHTFIRE_DUMP_WALL_COUNT=M, default 60): asks the D3D9 backend for a frame dump every
// N ms of wall-clock time from start-up, M times, whatever the game is doing - loading screens and movies included,
// where the tick-based dumps in Teleport.cpp never run. Each request prints the frame number it will be.

#include "DumpTimer.h"

#include "../../common/gfx/d3d9Backend.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int g_intervalMs = 0;
static int g_count = 60;

static DWORD WINAPI DumpTimerThread(LPVOID) {
    DWORD start = GetTickCount();
    for (int i = 1; i <= g_count; i++) {
        int wait = (int)(start + (DWORD)(i * g_intervalMs) - GetTickCount());
        if (wait > 0)
            Sleep(wait);
        uint32_t frame = D3D9_RequestDump();
        printf("[dumptimer] %d ms: dumping frame %u (d3d9_dump_frame_%u.bmp)%s\n", i * g_intervalMs, frame, frame,
               i == g_count ? " - the last" : "");
        fflush(stdout);
    }
    return 0;
}

void DumpTimer_Start(void) {
    char text[16] = "";
    if (!GetEnvironmentVariableA("NIGHTFIRE_DUMP_WALL_MS", text, sizeof(text)) || atoi(text) <= 0)
        return;
    g_intervalMs = atoi(text);
    if (GetEnvironmentVariableA("NIGHTFIRE_DUMP_WALL_COUNT", text, sizeof(text)) && atoi(text) > 0)
        g_count = atoi(text);
    printf("[dumptimer] a frame every %d ms, %d of them\n", g_intervalMs, g_count);
    fflush(stdout);
    CloseHandle(CreateThread(NULL, 0, DumpTimerThread, NULL, 0, NULL));
}
