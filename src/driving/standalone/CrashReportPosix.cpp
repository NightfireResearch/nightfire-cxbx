#include "CrashReport.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

// Signal handlers for the faults, each writing the text report and then letting the signal take its default course
// (a core dump, if the system keeps them): there is no minidump here. Readable memory and code are found from
// /proc/self/maps, read once a report.

namespace {

struct Mapping {
    uintptr_t start, end;
    bool readable, executable;
};
Mapping g_mappings[1024];
int g_mappingCount = 0;

uintptr_t Hex(const char **p) {
    uintptr_t value = 0;
    for (;; (*p)++) {
        char c = **p;
        if (c >= '0' && c <= '9')
            value = value * 16 + (c - '0');
        else if (c >= 'a' && c <= 'f')
            value = value * 16 + (c - 'a' + 10);
        else
            return value;
    }
}

// Each line: "start-end perms offset dev inode path"
void ReadMappings(void) {
    g_mappingCount = 0;
    int file = open("/proc/self/maps", O_RDONLY);
    if (file < 0)
        return;
    static char text[256 * 1024];
    size_t length = 0;
    for (ssize_t n; length < sizeof(text) - 1 && (n = read(file, text + length, sizeof(text) - 1 - length)) > 0;)
        length += size_t(n);
    close(file);
    text[length] = 0;
    for (const char *p = text; *p != 0 && g_mappingCount < 1024;) {
        Mapping &m = g_mappings[g_mappingCount++];
        m.start = Hex(&p);
        p++;
        m.end = Hex(&p);
        p++;
        m.readable = p[0] == 'r';
        m.executable = p[2] == 'x';
        while (*p != 0 && *p != '\n')
            p++;
        if (*p == '\n')
            p++;
    }
}

const Mapping *MappingOf(uintptr_t address) {
    for (int i = 0; i < g_mappingCount; i++) {
        if (address >= g_mappings[i].start && address < g_mappings[i].end)
            return &g_mappings[i];
    }
    return NULL;
}

const char *SignalName(int signal) {
    switch (signal) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGTRAP: return "SIGTRAP";
    case SIGABRT: return "SIGABRT";
    default: return "a signal";
    }
}

const int kSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGABRT };

volatile sig_atomic_t g_reporting = 0;

void ReportSignal(int signal, siginfo_t *info, void *context) {
    if (!g_reporting) {
        g_reporting = 1;
        ReadMappings();
        char scratch[256];
        const ucontext_t *uc = (const ucontext_t *)context;
#if defined(__i386__)
        const greg_t *g = uc->uc_mcontext.gregs;
        CrashRegisters r = { uint32_t(g[REG_EAX]), uint32_t(g[REG_EBX]), uint32_t(g[REG_ECX]), uint32_t(g[REG_EDX]),
                             uint32_t(g[REG_ESI]), uint32_t(g[REG_EDI]), uint32_t(g[REG_EIP]), uint32_t(g[REG_ESP]),
                             uint32_t(g[REG_EBP]), uint32_t(g[REG_EFL]) };
#else
        (void)uc;
        CrashRegisters r = {};
#endif
        CrashReport_Begin("fault");
        CrashReport_Line("[standalone] %s (code %d) at 0x%08x, in %s\n", SignalName(signal), info->si_code, r.eip,
                         CrashHost_Module(r.eip, scratch, sizeof(scratch)));
        if (signal == SIGSEGV || signal == SIGBUS)
            CrashReport_Line("[standalone]   address 0x%08x\n", (unsigned)(uintptr_t)info->si_addr);
        CrashReport_Context(&r);
        CrashReport_End();
    }
    // the default action, as if never caught
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    sigaction(signal, &action, NULL);
    raise(signal);
}

} // namespace

void CrashReport_Install(void) {
    static char alternateStack[64 * 1024];   // a stack overflow's report needs a stack of its own
    stack_t stack;
    memset(&stack, 0, sizeof(stack));
    stack.ss_sp = alternateStack;
    stack.ss_size = sizeof(alternateStack);
    sigaltstack(&stack, NULL);
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = ReportSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int signal : kSignals)
        sigaction(signal, &action, NULL);
}

// No other thread's registers can be read from here
void CrashReport_Threads(void) {
}

bool CrashHost_Readable(uintptr_t address, size_t bytes) {
    const Mapping *m = MappingOf(address);
    return m != NULL && m->readable && address + bytes <= m->end;
}

bool CrashHost_IsCode(uintptr_t address) {
    const Mapping *m = MappingOf(address);
    return m != NULL && m->readable && m->executable;
}

const char *CrashHost_Module(uintptr_t address, char *scratch, size_t size) {
    Dl_info info;
    if (dladdr((void *)address, &info) == 0 || info.dli_fname == NULL)
        return "an unknown module";
    const char *slash = strrchr(info.dli_fname, '/');
    strncpy(scratch, slash != NULL ? slash + 1 : info.dli_fname, size - 1);
    scratch[size - 1] = 0;
    return scratch;
}

uintptr_t CrashHost_ExecutableBase(void) {
    Dl_info info;
    if (dladdr((void *)&CrashReport_Install, &info) == 0)
        return 0;
    return (uintptr_t)info.dli_fbase;
}
