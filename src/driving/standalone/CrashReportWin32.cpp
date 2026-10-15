#include "CrashReport.h"

#include <stdio.h>
#include <string.h>

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

// Hard faults are reported as they are raised (a vectored handler, first in line), as the loader reports them; the
// one no handler takes also writes crash.dmp, from the unhandled-exception filter, just before the process ends.

namespace {

#ifdef _WIN64
#define Eax Rax
#define Ebx Rbx
#define Ecx Rcx
#define Edx Rdx
#define Esi Rsi
#define Edi Rdi
#define Eip Rip
#define Esp Rsp
#define Ebp Rbp
#endif

CrashRegisters RegistersOf(const CONTEXT *context) {
    CrashRegisters r;
    r.eax = context->Eax;
    r.ebx = context->Ebx;
    r.ecx = context->Ecx;
    r.edx = context->Edx;
    r.esi = context->Esi;
    r.edi = context->Edi;
    r.eip = context->Eip;
    r.esp = context->Esp;
    r.ebp = context->Ebp;
    r.eflags = context->EFlags;
    return r;
}

bool IsHardFault(DWORD code) {
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
    case EXCEPTION_BREAKPOINT:   // with no debugger to take it, an int3 ends the process like any fault
        return true;
    default:
        return false;
    }
}

typedef BOOL (WINAPI *MiniDumpWriteDumpFn)(HANDLE process, DWORD processId, HANDLE file, MINIDUMP_TYPE type,
                                            PMINIDUMP_EXCEPTION_INFORMATION exception,
                                            PMINIDUMP_USER_STREAM_INFORMATION userStreams,
                                            PMINIDUMP_CALLBACK_INFORMATION callback);

void WriteMinidump(const char *path, EXCEPTION_POINTERS *info) {
    HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
    MiniDumpWriteDumpFn write = dbghelp != NULL ? (MiniDumpWriteDumpFn)GetProcAddress(dbghelp, "MiniDumpWriteDump") : NULL;
    if (write == NULL) {
        CrashReport_Line("[standalone]   no dbghelp.dll, so no minidump\n");
        return;
    }
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        CrashReport_Line("[standalone]   could not create %s (error %lu)\n", path, GetLastError());
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION exception;
    exception.ThreadId = GetCurrentThreadId();
    exception.ExceptionPointers = info;
    exception.ClientPointers = FALSE;
    MINIDUMP_TYPE type = (MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo |
                                         MiniDumpWithUnloadedModules);
    BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, type, &exception, NULL, NULL);
    DWORD error = GetLastError();
    CloseHandle(file);
    if (ok)
        CrashReport_Line("[standalone]   wrote %s\n", path);
    else
        CrashReport_Line("[standalone]   MiniDumpWriteDump failed for %s (error %lu)\n", path, error);
}

volatile LONG g_reporting = 0;   // a fault raised while reporting one is left alone

LONG CALLBACK ReportException(EXCEPTION_POINTERS *info) {
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    DWORD code = record->ExceptionCode;
    if (!IsHardFault(code))   // C++ throws, thread names, OutputDebugString: normal traffic
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_reporting) != 1) {
        InterlockedDecrement(&g_reporting);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    char scratch[MAX_PATH];
    uintptr_t at = (uintptr_t)record->ExceptionAddress;
    CrashReport_Begin("fault");
    CrashReport_Line("[standalone] exception 0x%08x at 0x%08x, in %s\n", (unsigned)code, (unsigned)at,
                     CrashHost_Module(at, scratch, sizeof(scratch)));
    if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        static const char *const operations[] = { "read", "write", "execute" };
        ULONG_PTR operation = record->ExceptionInformation[0];
        uintptr_t target = (uintptr_t)record->ExceptionInformation[1];
        CrashReport_Line("[standalone]   tried to %s 0x%08x\n", operation <= 2 ? operations[operation] : "access",
                         (unsigned)target);
        MEMORY_BASIC_INFORMATION mbi;
        memset(&mbi, 0, sizeof(mbi));
        if (VirtualQuery((LPCVOID)target, &mbi, sizeof(mbi)) == sizeof(mbi))
            CrashReport_Line("[standalone]   0x%08x is state 0x%x, protection 0x%x\n", (unsigned)target,
                             (unsigned)mbi.State, (unsigned)mbi.Protect);
    }
    CrashRegisters registers = RegistersOf(info->ContextRecord);
    CrashReport_Context(&registers);
    CrashReport_End();
    InterlockedDecrement(&g_reporting);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Reached only for the exception that ends the process: its report is already written
LONG WINAPI ReportUnhandled(EXCEPTION_POINTERS *info) {
    static volatile LONG dumped = 0;
    if (InterlockedIncrement(&dumped) == 1) {
        CrashReport_Begin("unhandled: the process ends");
        CrashReport_Line("[standalone] exception 0x%08x at 0x%08x\n", (unsigned)info->ExceptionRecord->ExceptionCode,
                         (unsigned)(uintptr_t)info->ExceptionRecord->ExceptionAddress);
        WriteMinidump("crash.dmp", info);
        CrashReport_End();
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void CrashReport_Install(void) {
    AddVectoredExceptionHandler(1, ReportException);
    SetUnhandledExceptionFilter(ReportUnhandled);
}

// Every other thread's eip and return addresses. They are suspended and left so: the process is about to end.
void CrashReport_Threads(void) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return;
    CrashReport_Begin("thread snapshot");
    DWORD self = GetCurrentThreadId();
    DWORD process = GetCurrentProcessId();
    char scratch[MAX_PATH];
    THREADENTRY32 entry;
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self)
            continue;
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                                   entry.th32ThreadID);
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
            CrashReport_Line("[standalone] thread %lu at 0x%08x (%s)\n", entry.th32ThreadID, (unsigned)context.Eip,
                             CrashHost_Module(context.Eip, scratch, sizeof(scratch)));
            CrashReport_ScanStack(context.Esp, 12);
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    CrashReport_End();
}

// VirtualQuery rather than IsBadReadPtr, which finds out by faulting - and a fault in a vectored handler comes back
// to it
bool CrashHost_Readable(uintptr_t address, size_t bytes) {
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) != 0 || mbi.Protect == PAGE_NOACCESS)
        return false;
    return address + bytes <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

bool CrashHost_IsCode(uintptr_t address) {
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    return mbi.State == MEM_COMMIT && mbi.Type == MEM_IMAGE &&
           (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

const char *CrashHost_Module(uintptr_t address, char *scratch, size_t size) {
    HMODULE module = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)address, &module) &&
        GetModuleFileNameA(module, scratch, (DWORD)size) != 0) {
        const char *slash = strrchr(scratch, '\\');
        return slash != NULL ? slash + 1 : scratch;
    }
    return "an unknown module";
}

uintptr_t CrashHost_ExecutableBase(void) {
    return (uintptr_t)GetModuleHandleA(NULL);
}
