#include "CrashReport.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../platform/Host.h"
#include "../../common/version.h"

// A record is gathered here and appended to crash.log when it is complete, as the loader does: one append is
// milliseconds, a file opened per line was seconds a fault on a loaded machine. Lines outside a record (and those
// past a full buffer) go to the file one by one.

// Addresses, registers and stack words print at the host's width: 8 digits on a 32-bit host, 16 on a 64-bit one
#define ADDR "%0*" PRIxPTR

namespace {

const int kAddrDigits = int(sizeof(uintptr_t) * 2);

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
    CrashReport_Line("[standalone] %s loaded at 0x" ADDR "\n", CrashHost_Module(base, scratch, sizeof(scratch)),
                     kAddrDigits, base);
    CrashReport_Line("[standalone] version %s\n", NightfireVersion);
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
    const uintptr_t step = sizeof(uintptr_t);
    for (uintptr_t at = esp & ~(step - 1); at - esp < 256 * 1024 && found < limit; at += step) {
        if (!CrashHost_Readable(at, step))
            break;
        uintptr_t word = *(const uintptr_t *)at;
        if (word < 0x10000 || !CrashHost_IsCode(word) || !PrecededByCall(word))
            continue;
        CrashReport_Line("[standalone]     esp+%05x  0x" ADDR "  (%s)\n", (unsigned)(at - esp), kAddrDigits, word,
                         CrashHost_Module(word, scratch, sizeof(scratch)));
        found++;
    }
}

void CrashReport_Context(const CrashRegisters *r) {
    char scratch[HOST_MAX_PATH];
    const int w = kAddrDigits;
    CrashReport_Line("[standalone]   eax " ADDR " ebx " ADDR " ecx " ADDR " edx " ADDR "\n", w, r->eax, w, r->ebx, w,
                     r->ecx, w, r->edx);
    CrashReport_Line("[standalone]   esi " ADDR " edi " ADDR " ebp " ADDR " esp " ADDR "\n", w, r->esi, w, r->edi, w,
                     r->ebp, w, r->esp);
#if UINTPTR_MAX > 0xffffffffu
    for (int i = 0; i < 8; i += 4)
        CrashReport_Line("[standalone]   r%-2d " ADDR " r%-2d " ADDR " r%-2d " ADDR " r%-2d " ADDR "\n", 8 + i, w,
                         r->r[i], 9 + i, w, r->r[i + 1], 10 + i, w, r->r[i + 2], 11 + i, w, r->r[i + 3]);
#endif
    CrashReport_Line("[standalone]   eip " ADDR " eflags %08x\n", w, r->eip, (unsigned)r->eflags);

    if (CrashHost_Readable(r->eip, 16)) {
        const uint8_t *code = (const uint8_t *)r->eip;
        CrashReport_Line("[standalone]   code at eip: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
                         "%02x %02x %02x\n", code[0], code[1], code[2], code[3], code[4], code[5], code[6], code[7],
                         code[8], code[9], code[10], code[11], code[12], code[13], code[14], code[15]);
    }
    if (CrashHost_Readable(r->esp, 4 * sizeof(uintptr_t))) {
        const uintptr_t *stack = (const uintptr_t *)r->esp;
        CrashReport_Line("[standalone]   stack at esp: " ADDR " " ADDR " " ADDR " " ADDR "\n", w, stack[0], w, stack[1],
                         w, stack[2], w, stack[3]);
    }

    // The frame-pointer chain while each frame is sensibly above the last (x86-64 code seldom keeps one)
    CrashReport_Line("[standalone]   called from (frame chain):\n");
    uintptr_t frame = r->ebp;
    for (int depth = 0; depth < 16; depth++) {
        if (frame == 0 || (frame & (sizeof(uintptr_t) - 1)) != 0 || !CrashHost_Readable(frame, 2 * sizeof(uintptr_t)))
            break;
        uintptr_t returnAddress = ((const uintptr_t *)frame)[1];
        uintptr_t next = ((const uintptr_t *)frame)[0];
        if (returnAddress == 0)
            break;
        CrashReport_Line("[standalone]     0x" ADDR "  (%s)\n", w, returnAddress,
                         CrashHost_Module(returnAddress, scratch, sizeof(scratch)));
        if (next <= frame)
            break;
        frame = next;
    }

    CrashReport_Line("[standalone]   return addresses on the stack (stale ones included):\n");
    CrashReport_ScanStack(r->esp, 48);
}
