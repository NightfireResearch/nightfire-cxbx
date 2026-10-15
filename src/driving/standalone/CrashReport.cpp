#include "CrashReport.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../platform/Host.h"

// A record is gathered here and appended to crash.log when it is complete, as the loader does: one append is
// milliseconds, a file opened per line was seconds a fault on a loaded machine. Lines outside a record (and those
// past a full buffer) go to the file one by one.

namespace {

char g_record[16384];
size_t g_recordLength = 0;
bool g_recordOpen = false;

void Append(const char *text, size_t length) {
    FILE *log = fopen("crash.log", "a");
    if (log != NULL) {
        fwrite(text, 1, length, log);
        fclose(log);
    }
}

// Whether the instruction ending at address is a CALL, in any of the encodings a compiler emits: a code address on
// the stack with a CALL before it is a return address.
bool PrecededByCall(uintptr_t address) {
    if (!CrashHost_Readable(address - 7, 7))
        return false;
    const uint8_t *p = (const uint8_t *)address;
    if (p[-5] == 0xE8)                                                     // call rel32
        return true;
    if (p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0)                           // call reg
        return true;
    if (p[-3] == 0xFF && (p[-2] & 0xF8) == 0x50 && (p[-2] & 7) != 4)       // call [reg+disp8]
        return true;
    if (p[-4] == 0xFF && p[-3] == 0x54)                                    // call [sib+disp8]
        return true;
    if (p[-6] == 0xFF && (p[-5] == 0x15 || ((p[-5] & 0xF8) == 0x90 && (p[-5] & 7) != 4)))   // call [abs] / [reg+disp32]
        return true;
    if (p[-7] == 0xFF && p[-6] == 0x94)                                    // call [sib+disp32]
        return true;
    return false;
}

} // namespace

void CrashReport_Line(const char *format, ...) {
    char line[1024];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);

    fputs(line, stdout);
    fflush(stdout);

    size_t length = strlen(line);
    if (g_recordOpen && g_recordLength + length < sizeof(g_record)) {
        memcpy(g_record + g_recordLength, line, length);
        g_recordLength += length;
        return;
    }
    if (g_recordOpen)
        CrashReport_End();
    Append(line, length);
}

void CrashReport_Begin(const char *what) {
    g_recordOpen = true;
    HostSystemTime now;
    HostGetLocalTime(&now);
    CrashReport_Line("[standalone] ---- %s at %02u:%02u:%02u.%03u, thread %lu ----\n", what, now.hour, now.minute,
                     now.second, now.milliseconds, HostGetCurrentThreadId());
    char scratch[HOST_MAX_PATH];
    uintptr_t base = CrashHost_ExecutableBase();
    CrashReport_Line("[standalone] %s loaded at 0x%08x\n", CrashHost_Module(base, scratch, sizeof(scratch)),
                     (unsigned)base);
}

void CrashReport_End(void) {
    g_recordOpen = false;
    if (g_recordLength > 0)
        Append(g_record, g_recordLength);
    g_recordLength = 0;
}

// Every return address from esp up, whether or not the frames between kept a frame pointer; a few are stale ones
// from earlier calls, which the offsets help tell apart.
void CrashReport_ScanStack(uintptr_t esp, int limit) {
    char scratch[HOST_MAX_PATH];
    int found = 0;
    for (uintptr_t at = esp & ~(uintptr_t)3; at - esp < 256 * 1024 && found < limit; at += 4) {
        if (!CrashHost_Readable(at, 4))
            break;
        uintptr_t word = *(const uintptr_t *)at;
        if (word < 0x10000 || !CrashHost_IsCode(word) || !PrecededByCall(word))
            continue;
        CrashReport_Line("[standalone]     esp+%05x  0x%08x  (%s)\n", (unsigned)(at - esp), (unsigned)word,
                         CrashHost_Module(word, scratch, sizeof(scratch)));
        found++;
    }
}

void CrashReport_Context(const CrashRegisters *r) {
    char scratch[HOST_MAX_PATH];
    CrashReport_Line("[standalone]   eax %08x ebx %08x ecx %08x edx %08x esi %08x edi %08x\n", r->eax, r->ebx, r->ecx,
                     r->edx, r->esi, r->edi);
    CrashReport_Line("[standalone]   eip %08x esp %08x ebp %08x eflags %08x\n", r->eip, r->esp, r->ebp, r->eflags);

    if (CrashHost_Readable(r->eip, 16)) {
        const uint8_t *code = (const uint8_t *)(uintptr_t)r->eip;
        CrashReport_Line("[standalone]   code at eip: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
                         "%02x %02x %02x\n", code[0], code[1], code[2], code[3], code[4], code[5], code[6], code[7],
                         code[8], code[9], code[10], code[11], code[12], code[13], code[14], code[15]);
    }
    if (CrashHost_Readable(r->esp, 32)) {
        const uint32_t *stack = (const uint32_t *)(uintptr_t)r->esp;
        CrashReport_Line("[standalone]   stack at esp: %08x %08x %08x %08x %08x %08x %08x %08x\n", stack[0], stack[1],
                         stack[2], stack[3], stack[4], stack[5], stack[6], stack[7]);
    }

    // The frame-pointer chain while each frame is sensibly above the last
    CrashReport_Line("[standalone]   called from (frame chain):\n");
    uintptr_t frame = r->ebp;
    for (int depth = 0; depth < 16; depth++) {
        if (frame == 0 || (frame & 3) != 0 || !CrashHost_Readable(frame, 8))
            break;
        uintptr_t returnAddress = ((const uintptr_t *)frame)[1];
        uintptr_t next = ((const uintptr_t *)frame)[0];
        if (returnAddress == 0)
            break;
        CrashReport_Line("[standalone]     0x%08x  (%s)\n", (unsigned)returnAddress,
                         CrashHost_Module(returnAddress, scratch, sizeof(scratch)));
        if (next <= frame)
            break;
        frame = next;
    }

    CrashReport_Line("[standalone]   return addresses on the stack (stale ones included):\n");
    CrashReport_ScanStack(r->esp, 48);
}
