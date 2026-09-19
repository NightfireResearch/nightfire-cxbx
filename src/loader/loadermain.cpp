#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "xbe.h"
#include "kernel.h"
#include "../common/renderWindow.h"

// ---------------------------------------------------------------------------------------------------------------
// The standalone loader - stage B steps 4.2 and 4.3 of docs/cxbx-removal-plan.md, replacing cxbxr-ldr.exe.
//
// What it does, in order: map the XBE's sections at the base it was linked for, point the kernel import thunks
// at our own implementations, load actioninject.dll so the existing patches go in exactly as they do today,
// and call the entry point.
//
// It can reuse actioninject.dll unchanged because Inject() only writes to fixed addresses - it never assumed
// anything about CXBX being the host. Once the image is mapped where the XBE asks, the whole existing body of
// work (the D3D9 backend, the XAudio2 backend, the Win32 file layer, the input layer) comes along for free.
//
// GETTING THE ADDRESS RANGE. The XBE is linked to base 0x00010000 and is full of absolute addresses, so it has
// to land exactly there; there is nothing to relocate. That address is available in principle - the system's
// minimum is 0x00010000 - but it is never free in practice, and no amount of running earlier helps: reserving
// it from outside a process created suspended fails the same way it does from inside. The answer is to be the
// image. This executable is linked at that base with an array big enough to span the XBE, so the kernel maps
// it there before the process exists and the XBE is copied over the top. See src/loader/reserve.cpp.
//
// KERNEL IMPORTS. The XBE imports 96 of them, of which a dozen are implemented (src/loader/kernel.cpp) and
// the rest resolve to a stub that names itself and stops. That was how the list was arrived at: run, see
// which function the game reached, implement that one, run again. It is worth keeping the stubs rather than
// stubbing the whole table out silently, because "the screen is black" is not a bug report and
// "unimplemented kernel import: PsCreateSystemThreadEx, called from 0x000ee08e" is.
// ---------------------------------------------------------------------------------------------------------------

typedef void (*XbeEntryPoint)(void);

// ---------------------------------------------------------------------------------------------------------------
// First-chance exception reporting.
//
// Faults in this stage are mostly silent otherwise. An access violation inside a DLL's initialiser is caught by
// ntdll and turned into "LoadLibrary failed, error 1114", and a fault in game code lands on an address with no
// symbols behind it. Reporting the fault as it happens turns both into a line naming the address, the
// operation and which module the code was in - which is the difference between a fix and a guess.
//
// This only reports; it never handles. EXCEPTION_CONTINUE_SEARCH leaves the real handling exactly as it was.
// ---------------------------------------------------------------------------------------------------------------

static const char *ModuleContaining(uintptr_t address, char *scratch, size_t scratchSize) {
    if (address >= 0x00010000 && address < 0x0030b660)
        return "the mapped XBE";

    HMODULE module = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)address, &module) &&
        GetModuleFileNameA(module, scratch, (DWORD)scratchSize) != 0) {
        const char *slash = strrchr(scratch, '\\');
        return slash != NULL ? slash + 1 : scratch;
    }
    return "an unknown module";
}

static LONG CALLBACK ReportException(EXCEPTION_POINTERS *info) {
    const EXCEPTION_RECORD *record = info->ExceptionRecord;

    // C++ exceptions and the debugger's own name-setting exception are normal traffic, not faults.
    if (record->ExceptionCode == 0xE06D7363u || record->ExceptionCode == 0x406D1388u)
        return EXCEPTION_CONTINUE_SEARCH;

    char scratch[MAX_PATH];
    uintptr_t at = (uintptr_t)record->ExceptionAddress;
    printf("[loader] exception 0x%08x at 0x%08x, in %s\n",
           (unsigned)record->ExceptionCode, (unsigned)at, ModuleContaining(at, scratch, sizeof(scratch)));

    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        // Parameter 0 says what was attempted; 1 is the address it was attempted on. A write fault inside the
        // mapped XBE usually means a section's protection is stricter than the code patching it expects.
        static const char *operations[] = { "read", "write", "execute" };
        ULONG_PTR operation = record->ExceptionInformation[0];
        uintptr_t target = (uintptr_t)record->ExceptionInformation[1];

        printf("[loader]   tried to %s 0x%08x\n",
               operation <= 2 ? operations[operation] : "access", (unsigned)target);

        MEMORY_BASIC_INFORMATION mbi;
        memset(&mbi, 0, sizeof(mbi));
        if (VirtualQuery((LPCVOID)target, &mbi, sizeof(mbi)) == sizeof(mbi))
            printf("[loader]   0x%08x is state 0x%x, protection 0x%x\n",
                   (unsigned)target, (unsigned)mbi.State, (unsigned)mbi.Protect);
    }
    fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---------------------------------------------------------------------------------------------------------------
// The render window.
//
// Under CXBX the launcher owned a window and CXBX drew into a "CxbxRender" child of it, which is what the D3D9
// backend goes looking for. Standalone, nobody provides one, so the loader does - and it is the right place
// for it, because a window belongs to the thread that created it and has to have its messages pumped. The
// loader's main thread has nothing else to do once the entry point has returned, whereas the game's thread
// never pumps anything.
//
// The class name is what the backend finds it by; see FindRenderWindow in d3d9Backend.cpp.
// ---------------------------------------------------------------------------------------------------------------

#define RENDER_WINDOW_CLASS NIGHTFIRE_RENDER_WINDOW_CLASS

static LRESULT CALLBACK RenderWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE || message == WM_DESTROY) {
        // The game has no idea the window has gone and would carry on drawing into nothing, so end it here.
        printf("[loader] the render window was closed\n");
        fflush(stdout);
        ExitProcess(0);
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

static HWND CreateRenderWindow(int width, int height) {
    WNDCLASSEXA windowClass;
    memset(&windowClass, 0, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = RenderWindowProc;
    windowClass.hInstance = GetModuleHandleA(NULL);
    windowClass.hCursor = LoadCursorA(NULL, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    windowClass.lpszClassName = RENDER_WINDOW_CLASS;
    if (RegisterClassExA(&windowClass) == 0) {
        printf("[loader] could not register the render window class (error %lu)\n", GetLastError());
        return NULL;
    }

    // Sized so that the client area is the back buffer's size, rather than the window including its border.
    RECT wanted = { 0, 0, width, height };
    AdjustWindowRect(&wanted, WS_OVERLAPPEDWINDOW, FALSE);

    HWND window = CreateWindowExA(0, RENDER_WINDOW_CLASS, "007: Nightfire", WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT,
                                  wanted.right - wanted.left, wanted.bottom - wanted.top,
                                  NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (window == NULL) {
        printf("[loader] could not create the render window (error %lu)\n", GetLastError());
        return NULL;
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    return window;
}

// Kept in one place because the useful failure message is "where did you look", not "not found".
static bool FindXbe(char *out, size_t outSize) {
    static const char *candidates[] = {
        "../disc/default.xbe",
        "disc/default.xbe",
        "default.xbe",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (GetFileAttributesA(candidates[i]) != INVALID_FILE_ATTRIBUTES) {
            snprintf(out, outSize, "%s", candidates[i]);
            return true;
        }
    }
    char cwd[MAX_PATH];
    if (GetCurrentDirectoryA(sizeof(cwd), cwd) == 0)
        snprintf(cwd, sizeof(cwd), "(unknown)");
    printf("[loader] could not find default.xbe. Looked for:\n");
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
        printf("[loader]   %s\n", candidates[i]);
    printf("[loader] (relative to the working directory, which is %s)\n", cwd);
    return false;
}

int main(int argc, char **argv) {
    printf("[loader] Nightfire standalone loader\n");
    AddVectoredExceptionHandler(1, ReportException);

    char xbePath[MAX_PATH];
    if (argc > 1)
        snprintf(xbePath, sizeof(xbePath), "%s", argv[1]);
    else if (!FindXbe(xbePath, sizeof(xbePath)))
        return 1;

    XbeImage image;
    if (!Xbe_Load(xbePath, &image))
        return 1;

    if (!Xbe_Map(&image))
        return 1;
    if (!Kernel_Init())
        return 1;
    if (!Xbe_ResolveKernelImports(&image, Kernel_Resolve))
        return 1;

    // The existing injection DLL, unchanged. Its DllMain calls Inject(), which patches the game code at the
    // addresses it has always used - now that the image is mapped, those addresses mean what they always did.
    if (LoadLibraryA("actioninject.dll") == NULL) {
        printf("[loader] could not load actioninject.dll (error %lu)\n", GetLastError());
        return 1;
    }
    printf("[loader] actioninject.dll loaded and patches applied\n");

    // Before the entry point, so that the window is already there when the game's graphics init looks for it.
    // 640x480 is the game's own back buffer size; the window is resizable and D3D9 presents scaled into it.
    if (CreateRenderWindow(640, 480) == NULL)
        return 1;

    printf("[loader] calling entry point at 0x%08x\n", image.entryPoint);
    fflush(stdout);

    XbeEntryPoint entry = (XbeEntryPoint)(uintptr_t)image.entryPoint;
    entry();

    // entry() creates a thread for mainXapiStartup and returns immediately, so reaching here is normal and
    // means the game is running on that thread. This thread becomes the window's message pump - without one
    // the window would never repaint, never report input, and Windows would eventually mark it unresponsive.
    printf("[loader] entry point returned; the game is running on its own thread\n");
    fflush(stdout);

    MSG message;
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return 0;
}
