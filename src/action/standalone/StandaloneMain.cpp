// The standalone build's entry point (docs/action-standalone.md): our code alone, no default.xbe mapped or read. It
// does what the loader (src/loader/loadermain.cpp) and the injection's Inject() did for the injected build, by calling
// our code directly - read the launch options, point D: at the disc, load FFmpeg - then opens the render window the
// D3D9 backend presents into, starts the game's thread through Xbox_Entry (engine/XboxStartup.cpp), and pumps the
// window's messages on this thread. Closing the window ends the process, as the loader's does.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <tlhelp32.h>

#include "../devtools/Demo.h"
#include "../engine/Fmv.h"
#include "../engine/LaunchOptions.h"
#include "../engine/XboxSettings.h"
#include "../engine/XboxStartup.h"
#include "../../common/console.h"
#include "../../common/renderWindow.h"
#include "../../common/xboxPath.h"
#include "../../res/resource.h"
#include "../../driving/platform/Ptr32.h"   // GameWindowOutOfRange

// Where a thread is: its registers, and the words on its stack that are addresses in this executable, as offsets for
// its map (action_standalone.map)
static void ReportContext(const CONTEXT *c) {
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
#if defined(_M_IX86) || defined(__i386__)
    uintptr_t pc = c->Eip, sp = c->Esp;
    printf("[standalone]   eip action_standalone+0x%x  eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx "
           "ebp %08lx esp %08lx\n", (unsigned)(pc - base), c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp,
           c->Esp);
#else
    uintptr_t pc = c->Rip, sp = c->Rsp;
    printf("[standalone]   rip action_standalone+0x%x  rax %016llx rbx %016llx rcx %016llx rdx %016llx rsi %016llx "
           "rdi %016llx rbp %016llx rsp %016llx\n", (unsigned)(pc - base), c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi,
           c->Rdi, c->Rbp, c->Rsp);
#endif
    MEMORY_BASIC_INFORMATION region;
    if (VirtualQuery((const void *)sp, &region, sizeof(region)) == 0)
        return;
    const uintptr_t *stack = (const uintptr_t *)sp;
    const uintptr_t *top = (const uintptr_t *)((const char *)region.BaseAddress + region.RegionSize);
    int shown = 0;
    for (const uintptr_t *word = stack; word < top && shown < 40; word++) {
        if (*word >= base && *word < base + 0x1000000) {
            printf("[standalone]   stack +0x%04x: action_standalone+0x%x\n",
                   (unsigned)((word - stack) * sizeof(*word)), (unsigned)(*word - base));
            shown++;
        }
    }
}

#if UINTPTR_MAX != 0xffffffffu
// An address a 32-bit word of the game's data was to hold, outside the game window (driving/platform/Ptr32.h): the
// executable is linked low and the game's memory comes from below 4 GB, so this is a bug
void GameWindowOutOfRange(const void *pointer) {
    printf("[standalone] %p is outside the game window\n", pointer);
    fflush(stdout);
    ExitProcess(3);
}
#endif

// A fault nothing handles
static LONG WINAPI ReportUnhandled(EXCEPTION_POINTERS *info) {
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    printf("[standalone] exception 0x%08lx at 0x%08x, thread %lu\n", (unsigned long)record->ExceptionCode,
           (unsigned)(uintptr_t)record->ExceptionAddress, GetCurrentThreadId());
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
        printf("[standalone]   %s 0x%08x\n", record->ExceptionInformation[0] ? "writing" : "reading",
               (unsigned)record->ExceptionInformation[1]);
    ReportContext(info->ContextRecord);
    fflush(stdout);
    ExitProcess(3);
}

// NIGHTFIRE_REPORT_AFTER=<seconds>: where every other thread is then, for a run that stops getting anywhere
static DWORD WINAPI ThreadReport(void *seconds) {
    Sleep((DWORD)(uintptr_t)seconds * 1000);
    DWORD self = GetCurrentThreadId(), process = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry;
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self)
            continue;
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID);
        if (thread == NULL)
            continue;
        SuspendThread(thread);
        CONTEXT context;
        context.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(thread, &context)) {
            printf("[standalone] thread %lu\n", entry.th32ThreadID);
            ReportContext(&context);
        }
        ResumeThread(thread);
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    fflush(stdout);
    return 0;
}

static void StartThreadReport(void) {
    const char *after = getenv("NIGHTFIRE_REPORT_AFTER");
    if (after != NULL)
        CloseHandle(CreateThread(NULL, 0, ThreadReport, (void *)(uintptr_t)atoi(after), 0, NULL));
}

static LRESULT CALLBACK RenderWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE || message == WM_DESTROY) {
        // The game does not know the window has gone and would carry on drawing into nothing
        printf("[standalone] the render window was closed\n");
        fflush(stdout);
        ExitProcess(0);
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

// The client area is the back buffer's size; the backend resizes it to the device's (d3d9Backend.cpp)
static HWND CreateRenderWindow(int width, int height) {
    WNDCLASSEXA windowClass;
    memset(&windowClass, 0, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = RenderWindowProc;
    windowClass.hInstance = GetModuleHandleA(NULL);
    windowClass.hCursor = LoadCursorA(NULL, MAKEINTRESOURCEA(32512));   // IDC_ARROW (wide under UNICODE)
    windowClass.hIcon = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDI_ICON1));
    windowClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    windowClass.lpszClassName = NIGHTFIRE_RENDER_WINDOW_CLASS;
    if (RegisterClassExA(&windowClass) == 0) {
        printf("[standalone] could not register the render window class (error %lu)\n", GetLastError());
        return NULL;
    }
    RECT wanted = {0, 0, width, height};
    AdjustWindowRect(&wanted, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowExA(0, NIGHTFIRE_RENDER_WINDOW_CLASS, "007: Nightfire", WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, wanted.right - wanted.left, wanted.bottom - wanted.top,
                                  NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (window == NULL) {
        printf("[standalone] could not create the render window (error %lu)\n", GetLastError());
        return NULL;
    }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    return window;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    EnsureConsoleOutput();
    printf("[standalone] Nightfire (action engine), no XBE\n");
    SetUnhandledExceptionFilter(ReportUnhandled);
    StartThreadReport();

    // -level and -difficulty; then a time demo's mode (action_standalone_test): a playback moves into its sandbox
    // before settings.ini is read
    LaunchOptions_Read();
    Demo_Init();
    Xbox_SetDiscRoot(Settings_GetDiscPath());
    Fmv_Init();

    if (CreateRenderWindow(640, 480) == NULL)
        return 1;

    Xbox_Entry();

    MSG message;
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return 0;
}
