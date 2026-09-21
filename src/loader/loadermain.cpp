#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "xbe.h"
#include "kernel.h"
#include "../common/renderWindow.h"
#include "../res/resource.h"
#include "../common/console.h"

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
// KERNEL IMPORTS. The XBE imports 96 ordinals, of which 19 are implemented (src/loader/kernel.cpp) and
// the rest resolve to a stub that names itself and stops. That was how the list was arrived at: run, see
// which function the game reached, implement that one, run again. It is worth keeping the stubs rather than
// stubbing the whole table out silently, because "the screen is black" is not a bug report and
// "unimplemented kernel import: PsCreateSystemThreadEx, called from 0x000ee08e" is.
// ---------------------------------------------------------------------------------------------------------------

typedef void (*XbeEntryPoint)(void);

// ---------------------------------------------------------------------------------------------------------------
// Which engine this loader is for.
//
// Nothing below here is specific to the action engine any more: both XBEs are linked at 0x10000, both are
// spanned by the reservation array, and both are mapped and have their kernel imports resolved the same way.
// The only two engine-specific facts are the name of the XBE to look for and the name of the injection DLL
// that carries that engine's patches, so they are the only two things the build chooses between. See the
// "driving" target in CMakeLists.txt, and the naming note in docs/driving-engine-plan.md section 0.
//
// Both can still be overridden on the command line, which is what makes "run the other engine's XBE to see
// where it stops" a one-liner rather than a rebuild.
// ---------------------------------------------------------------------------------------------------------------

#ifdef IS_DRIVING
#define LOADER_ENGINE_NAME "driving"
#define LOADER_XBE_NAME    "Driving.xbe"
#define LOADER_INJECT_DLL  "drivinginject.dll"
#else
#define LOADER_ENGINE_NAME "action"
#define LOADER_XBE_NAME    "default.xbe"
#define LOADER_INJECT_DLL  "actioninject.dll"
#endif

// Where the XBE ended up, for ModuleContaining below. Set once the image is mapped; before that there is
// nothing at the base but this executable's own headers.
static uintptr_t g_imageStart = 0;
static uintptr_t g_imageEnd = 0;

// ---------------------------------------------------------------------------------------------------------------
// Fault forensics.
//
// Faults in this stage are mostly silent otherwise. An access violation inside a DLL's initialiser is caught by
// ntdll and turned into "LoadLibrary failed, error 1114", and a fault in game code lands on an address with no
// symbols behind it. Reporting the fault as it happens turns both into a line naming the address, the
// operation and which module the code was in - which is the difference between a fix and a guess.
//
// Everything reported goes to two places: stdout, which is usually a file that a crash would otherwise leave
// with its last few kilobytes still in the CRT's buffer, and crash.log in the working directory, opened and
// closed around every line so that nothing is lost however the process dies afterwards. The first hard fault
// also writes crash.dmp, a full-memory minidump that cdb or Visual Studio can open with the game's memory
// exactly as it was.
//
// This only reports; it never handles. EXCEPTION_CONTINUE_SEARCH leaves the real handling exactly as it was.
// ---------------------------------------------------------------------------------------------------------------

static const char *ModuleContaining(uintptr_t address, char *scratch, size_t scratchSize) {
    if (g_imageStart != 0 && address >= g_imageStart && address < g_imageEnd)
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

static void Report(const char *format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    fputs(line, stdout);
    fflush(stdout);

    FILE *log = fopen("crash.log", "a");
    if (log != NULL) {
        fputs(line, log);
        fclose(log);
    }
}

static void ReportStamp(const char *what) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    Report("[loader] ---- %s at %02u:%02u:%02u.%03u, thread %lu ----\n",
           what, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentThreadId());
}

// Whether [address, address + bytes) can be read without faulting. VirtualQuery rather than IsBadReadPtr,
// because IsBadReadPtr finds out by touching the memory and catching the fault - and a fault raised inside a
// vectored handler comes straight back into that handler.
static bool Readable(uintptr_t address, size_t bytes) {
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) != 0 || mbi.Protect == PAGE_NOACCESS)
        return false;
    return address + bytes <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

static bool IsCode(uintptr_t address) {
    if (g_imageStart != 0 && address >= g_imageStart && address < g_imageEnd)
        return true;
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    return mbi.State == MEM_COMMIT && mbi.Type == MEM_IMAGE &&
           (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// Whether the instruction ending at address is a CALL, in any of the x86 encodings a compiler emits. A stack
// word that is a code address with a CALL just before it is a return address, whatever else is on the stack.
static bool PrecededByCall(uintptr_t address) {
    if (!Readable(address - 7, 7))
        return false;
    const uint8_t *p = (const uint8_t *)address;
    if (p[-5] == 0xE8)                                          // call rel32
        return true;
    if (p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0)                // call reg
        return true;
    if (p[-3] == 0xFF && (p[-2] & 0xF8) == 0x50 && (p[-2] & 7) != 4)   // call [reg+disp8]
        return true;
    if (p[-4] == 0xFF && p[-3] == 0x54)                         // call [sib+disp8]
        return true;
    if (p[-6] == 0xFF && (p[-5] == 0x15 || ((p[-5] & 0xF8) == 0x90 && (p[-5] & 7) != 4)))   // call [abs] / [reg+disp32]
        return true;
    if (p[-7] == 0xFF && p[-6] == 0x94)                         // call [sib+disp32]
        return true;
    return false;
}

// Every return address on the stack from esp upwards, whether or not the frames between them kept a frame
// pointer. The EBP chain stops at the first function that was compiled without one - which in the XBE is
// most of the leaf and library code - so this is what actually names the callers. Anything that is a code
// address preceded by a CALL is printed; a few will be leftovers from earlier calls, which is why the
// offsets are printed too, so that the reader can tell the live frames from the stale ones.
static void ScanStack(uintptr_t esp, int limit) {
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    if (VirtualQuery((LPCVOID)esp, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT)
        return;

    uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    if (end - esp > 256 * 1024)
        end = esp + 256 * 1024;

    char scratch[MAX_PATH];
    int found = 0;
    for (uintptr_t at = esp & ~(uintptr_t)3; at + 4 <= end && found < limit; at += 4) {
        uintptr_t word = *(const uintptr_t *)at;
        if (word < 0x10000 || !IsCode(word) || !PrecededByCall(word))
            continue;
        Report("[loader]     esp+%05x  0x%08x  (%s)\n",
               (unsigned)(at - esp), (unsigned)word, ModuleContaining(word, scratch, sizeof(scratch)));
        found++;
    }
}

static void ReportContext(const CONTEXT *context) {
    char scratch[MAX_PATH];
    Report("[loader]   eax %08x ebx %08x ecx %08x edx %08x esi %08x edi %08x\n",
           (unsigned)context->Eax, (unsigned)context->Ebx, (unsigned)context->Ecx,
           (unsigned)context->Edx, (unsigned)context->Esi, (unsigned)context->Edi);
    Report("[loader]   eip %08x esp %08x ebp %08x eflags %08x\n",
           (unsigned)context->Eip, (unsigned)context->Esp, (unsigned)context->Ebp, (unsigned)context->EFlags);

    if (Readable(context->Eip, 16)) {
        const uint8_t *code = (const uint8_t *)context->Eip;
        Report("[loader]   code at eip: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
               code[0], code[1], code[2], code[3], code[4], code[5], code[6], code[7],
               code[8], code[9], code[10], code[11], code[12], code[13], code[14], code[15]);
    }
    if (Readable(context->Esp, 32)) {
        const uint32_t *stack = (const uint32_t *)context->Esp;
        Report("[loader]   stack at esp: %08x %08x %08x %08x %08x %08x %08x %08x\n",
               stack[0], stack[1], stack[2], stack[3], stack[4], stack[5], stack[6], stack[7]);
    }

    // The frame-pointer chain first: almost every XBE function starts "push ebp; mov ebp, esp", so this is
    // the clean answer when it works. A frame that is not sensibly above the last one ends the walk.
    Report("[loader]   called from (frame chain):\n");
    uintptr_t frame = context->Ebp;
    for (int depth = 0; depth < 16; depth++) {
        if (frame == 0 || (frame & 3) != 0 || !Readable(frame, 8))
            break;
        uintptr_t returnAddress = ((const uintptr_t *)frame)[1];
        uintptr_t nextFrame = ((const uintptr_t *)frame)[0];
        if (returnAddress == 0)
            break;
        Report("[loader]     0x%08x  (%s)\n",
               (unsigned)returnAddress, ModuleContaining(returnAddress, scratch, sizeof(scratch)));
        if (nextFrame <= frame)
            break;
        frame = nextFrame;
    }

    Report("[loader]   return addresses on the stack (stale ones included):\n");
    ScanStack(context->Esp, 48);
}

typedef BOOL (WINAPI *MiniDumpWriteDumpFn)(HANDLE process, DWORD processId, HANDLE file, MINIDUMP_TYPE type,
                                            PMINIDUMP_EXCEPTION_INFORMATION exception,
                                            PMINIDUMP_USER_STREAM_INFORMATION userStreams,
                                            PMINIDUMP_CALLBACK_INFORMATION callback);

static void WriteMinidump(const char *path, EXCEPTION_POINTERS *info) {
    HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
    MiniDumpWriteDumpFn write = dbghelp != NULL ? (MiniDumpWriteDumpFn)GetProcAddress(dbghelp, "MiniDumpWriteDump") : NULL;
    if (write == NULL) {
        Report("[loader]   no dbghelp.dll, so no minidump\n");
        return;
    }

    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        Report("[loader]   could not create %s (error %lu)\n", path, GetLastError());
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION exception;
    exception.ThreadId = GetCurrentThreadId();
    exception.ExceptionPointers = info;
    exception.ClientPointers = FALSE;

    MINIDUMP_TYPE type = (MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo |
                                         MiniDumpWithUnloadedModules);
    BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, type, info != NULL ? &exception : NULL, NULL, NULL);
    CloseHandle(file);
    Report(ok ? "[loader]   wrote %s\n" : "[loader]   MiniDumpWriteDump failed for %s (error %lu)\n", path, GetLastError());
}

static bool IsHardFault(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_STACK_CHECK:
    case EXCEPTION_INVALID_HANDLE:
        return true;
    default:
        return false;
    }
}

static LONG CALLBACK ReportException(EXCEPTION_POINTERS *info) {
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    DWORD code = record->ExceptionCode;

    // C++ exceptions, the debugger's name-setting exception and OutputDebugString's are normal traffic.
    if (code == 0xE06D7363u || code == 0x406D1388u || code == 0x40010006u || code == 0x4001000Au)
        return EXCEPTION_CONTINUE_SEARCH;

    // A fault raised while reporting a fault would otherwise report itself, without end.
    static volatile LONG reporting = 0;
    if (InterlockedIncrement(&reporting) != 1) {
        InterlockedDecrement(&reporting);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    char scratch[MAX_PATH];
    uintptr_t at = (uintptr_t)record->ExceptionAddress;
    bool hard = IsHardFault(code);

    if (hard)
        ReportStamp("fault");
    Report("[loader] exception 0x%08x at 0x%08x, in %s\n",
           (unsigned)code, (unsigned)at, ModuleContaining(at, scratch, sizeof(scratch)));

    if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        // Parameter 0 says what was attempted; 1 is the address it was attempted on. A write fault inside the
        // mapped XBE usually means a section's protection is stricter than the code patching it expects.
        static const char *operations[] = { "read", "write", "execute" };
        ULONG_PTR operation = record->ExceptionInformation[0];
        uintptr_t target = (uintptr_t)record->ExceptionInformation[1];

        Report("[loader]   tried to %s 0x%08x\n",
               operation <= 2 ? operations[operation] : "access", (unsigned)target);

        MEMORY_BASIC_INFORMATION mbi;
        memset(&mbi, 0, sizeof(mbi));
        if (VirtualQuery((LPCVOID)target, &mbi, sizeof(mbi)) == sizeof(mbi))
            Report("[loader]   0x%08x is state 0x%x, protection 0x%x\n",
                   (unsigned)target, (unsigned)mbi.State, (unsigned)mbi.Protect);
    }

    if (hard) {
        ReportContext(info->ContextRecord);

        // Once: the first hard fault is the one that matters, and a full-memory dump is not small.
        static bool dumped = false;
        if (!dumped) {
            dumped = true;
            WriteMinidump("crash.dmp", info);
        }
    }

    InterlockedDecrement(&reporting);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Where every other thread is right now. This is the answer to "it froze": the render window's close box
// calls it, so that a game which has stopped drawing can be asked what it is doing before the process ends.
// The threads are suspended and left that way - the process is about to exit - and the loader's own thread,
// the one doing the asking, is skipped.
static void ReportThreads(void) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return;

    ReportStamp("thread snapshot");
    DWORD self = GetCurrentThreadId();
    DWORD process = GetCurrentProcessId();
    char scratch[MAX_PATH];

    THREADENTRY32 entry;
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self)
            continue;

        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                   FALSE, entry.th32ThreadID);
        if (thread == NULL)
            continue;
        if (SuspendThread(thread) == (DWORD)-1) {
            CloseHandle(thread);
            continue;
        }

        CONTEXT context;
        memset(&context, 0, sizeof(context));
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(thread, &context)) {
            Report("[loader] thread %lu at 0x%08x (%s)\n", entry.th32ThreadID, (unsigned)context.Eip,
                   ModuleContaining((uintptr_t)context.Eip, scratch, sizeof(scratch)));
            ScanStack(context.Esp, 12);
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
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
        // First, where every thread was: if the window was closed because the game froze, this is the only
        // record of what it froze in.
        printf("[loader] the render window was closed\n");
        fflush(stdout);
        ReportThreads();
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
    windowClass.hIcon = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDI_ICON1));
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
    char candidates[3][MAX_PATH];
    snprintf(candidates[0], sizeof(candidates[0]), "../disc/%s", LOADER_XBE_NAME);
    snprintf(candidates[1], sizeof(candidates[1]), "disc/%s", LOADER_XBE_NAME);
    snprintf(candidates[2], sizeof(candidates[2]), "%s", LOADER_XBE_NAME);

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (GetFileAttributesA(candidates[i]) != INVALID_FILE_ATTRIBUTES) {
            snprintf(out, outSize, "%s", candidates[i]);
            return true;
        }
    }
    char cwd[MAX_PATH];
    if (GetCurrentDirectoryA(sizeof(cwd), cwd) == 0)
        snprintf(cwd, sizeof(cwd), "(unknown)");
    printf("[loader] could not find %s. Looked for:\n", LOADER_XBE_NAME);
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
        printf("[loader]   %s\n", candidates[i]);
    printf("[loader] (relative to the working directory, which is %s)\n", cwd);
    return false;
}

int main(int argc, char **argv) {
    // Usually a no-op: a console executable started from a terminal or from Explorer already has one. It
    // matters when this is started detached, where stdout is a handle onto a console nobody can see.
    EnsureConsoleOutput();

    printf("[loader] Nightfire standalone loader (%s engine)\n", LOADER_ENGINE_NAME);
    AddVectoredExceptionHandler(1, ReportException);

    // Both arguments are optional and positional: the XBE to map, then the DLL carrying its patches. They
    // exist so that either engine's XBE can be run under either loader without a rebuild.
    char xbePath[MAX_PATH];
    if (argc > 1)
        snprintf(xbePath, sizeof(xbePath), "%s", argv[1]);
    else if (!FindXbe(xbePath, sizeof(xbePath)))
        return 1;

    char injectDll[MAX_PATH];
    snprintf(injectDll, sizeof(injectDll), "%s", argc > 2 ? argv[2] : LOADER_INJECT_DLL);

    XbeImage image;
    if (!Xbe_Load(xbePath, &image))
        return 1;

    if (!Xbe_Map(&image))
        return 1;
    g_imageStart = image.baseAddress;
    g_imageEnd = (uintptr_t)image.baseAddress + image.sizeOfImage;
    if (!Kernel_Init())
        return 1;
    if (!Xbe_ResolveKernelImports(&image, Kernel_Resolve))
        return 1;

    // The existing injection DLL, unchanged. Its DllMain calls Inject(), which patches the game code at the
    // addresses it has always used - now that the image is mapped, those addresses mean what they always did.
    if (LoadLibraryA(injectDll) == NULL) {
        printf("[loader] could not load %s (error %lu)\n", injectDll, GetLastError());
        return 1;
    }
    printf("[loader] %s loaded and patches applied\n", injectDll);

    // Before the entry point, so that the window is already there when the game's graphics init looks for it.
    // That is too early to know what resolution the game will ask for, so this is only a starting size: the
    // D3D9 backend resizes the window to match the back buffer when it creates the device. The window stays
    // resizable after that, and D3D9 presents scaled into whatever size the user drags it to.
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
