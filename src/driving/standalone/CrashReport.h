#ifndef DRIVING_STANDALONE_CRASHREPORT_H_
#define DRIVING_STANDALONE_CRASHREPORT_H_

// The standalone executable's crash reporter: what the loader (src/loader/loadermain.cpp) gives the injected build.
// Every hard fault is reported to stdout and appended to crash.log in the working directory - registers, the bytes
// at eip and esp, the frame chain and every return address on the stack, each named by the module it lies in - and
// each report starts with where the executable is loaded, so that tools/symbolise.py --map names our functions from
// the linker's map. On Windows the fault that ends the process also writes crash.dmp (a full-memory minidump), and
// closing the window reports where every other thread is (CrashReport_Threads), the record of a freeze. It only
// reports: the fault's handling is left as it was.
//
// CrashReport.cpp is the report; CrashReportWin32.cpp (a vectored handler and the unhandled-exception filter) and
// CrashReportPosix.cpp (signal handlers, the text report only) catch the faults and answer the questions below.

#include <stddef.h>
#include <stdint.h>

void CrashReport_Install(void);
void CrashReport_Threads(void);

// ---- the report, shared by both hosts
struct CrashRegisters {   // on x86-64 the 64-bit registers (rax...)
    uintptr_t eax, ebx, ecx, edx, esi, edi;
    uintptr_t eip, esp, ebp, eflags;
};

void CrashReport_Begin(const char *what);   // a record, under a time-stamped heading and the executable's base
void CrashReport_Line(const char *format, ...);
void CrashReport_End(void);                 // the record appended to crash.log in one write
void CrashReport_Context(const CrashRegisters *registers);
void CrashReport_ScanStack(uintptr_t esp, int limit);

// ---- what each host answers
bool CrashHost_Readable(uintptr_t address, size_t bytes);
bool CrashHost_IsCode(uintptr_t address);
const char *CrashHost_Module(uintptr_t address, char *scratch, size_t size);   // its file name, without the path
uintptr_t CrashHost_ExecutableBase(void);

#endif // DRIVING_STANDALONE_CRASHREPORT_H_
