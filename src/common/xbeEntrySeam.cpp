#include "xbeEntrySeam.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// See xbeEntrySeam.h for what this is for.

// ---------------------------------------------------------------------------------------------------------------
// The stub.
//
// One trampoline per entry point, so that a single handler can still say which one it was reached through -
// the same arrangement as the loader's kernel import stubs (src/loader/kernel.cpp), and for the same reason.
// Each trampoline is "push slot; jmp StubEntry", reached by a jump patched over the entry point, so on entry
// the stack is [esp] = slot, [esp+4] = the caller's return address, then the arguments.
//
// Unlike the kernel stubs these return rather than stopping. A missing kernel import means the game is about
// to use something that does not exist; a missing library entry point usually means a piece of drawing or a
// sound does not happen, and carrying on names the next twenty as well as this one. Returning correctly is
// the whole trick: the handler hands back the argument size out of the table, and the epilogue pops exactly
// that much.
// ---------------------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
struct StubTrampoline {
    uint8_t  pushOpcode;
    uint32_t slot;
    uint8_t  jmpOpcode;
    int32_t  relativeTarget;
};
#pragma pack(pop)

// Slots are global rather than per-seam so that one handler serves every seam; a trampoline carries the index
// of its slot, and the slot says which entry of which seam it belongs to.
struct Slot {
    XbeEntry   *entry;
    const char *tag;
};

#define MAX_SLOTS 512
static Slot g_slots[MAX_SLOTS];
static unsigned g_slotCount = 0;
static StubTrampoline *g_trampolines = NULL;

static unsigned __cdecl StubReport(unsigned slot, uint32_t calledFrom) {
    if (slot >= g_slotCount)
        return 0;

    XbeEntry *entry = g_slots[slot].entry;
    entry->calls++;
    if (!entry->reported) {
        entry->reported = true;
        printf("[%s] not implemented: %s, first called from 0x%08x\n",
               g_slots[slot].tag, entry->name, calledFrom);
        fflush(stdout);
    }
    return entry->stackBytes == XBE_ENTRY_STACK_UNKNOWN ? 0 : entry->stackBytes;
}

// Entered by a jmp from a trampoline. Returns zero, in EAX, having popped the caller's arguments.
static void __declspec(naked) StubEntry(void) {
    __asm {
        push [esp + 4]          // the caller's return address, for the report
        push [esp + 4]          // the slot the trampoline pushed, now one slot further down
        call StubReport
        add  esp, 8             // __cdecl: ours to clean up. EAX is now the argument size
        add  esp, 4             // drop the slot
        pop  edx                // the caller's return address
        add  esp, eax           // and the arguments, exactly as the original would have
        xor  eax, eax           // an entry point that returns anything returns a status or a pointer;
        jmp  edx                //   zero is success, or null, and either is the safer answer
    }
}

static bool EnsureTrampolines(void) {
    if (g_trampolines != NULL)
        return true;
    g_trampolines = (StubTrampoline *)VirtualAlloc(NULL, MAX_SLOTS * sizeof(StubTrampoline),
                                                   MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (g_trampolines == NULL)
        printf("[seam] could not allocate the stub trampolines (error %lu)\n", GetLastError());
    return g_trampolines != NULL;
}

static void WriteJump(const char *tag, unsigned address, const void *target) {
    unsigned char *site = (unsigned char *)address;
    DWORD previous = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) {
        printf("[%s] could not make 0x%08x writable (error %lu)\n", tag, address, GetLastError());
        return;
    }
    site[0] = 0xE9;                                             // jmp rel32
    *(int *)(site + 1) = (int)((const unsigned char *)target - (site + 5));
}

int XbeSeam_Replace(XbeEntrySeam *seam, const char *name, void *replacement, unsigned stackBytes) {
    int found = 0;
    for (unsigned i = 0; i < seam->count; i++) {
        if (strcmp(seam->entries[i].name, name) != 0)
            continue;

        // The check described in the header. The table's own figure comes from the binary, so where they
        // disagree it is the replacement that is wrong.
        if (seam->entries[i].stackBytes != XBE_ENTRY_STACK_UNKNOWN &&
            seam->entries[i].stackBytes != stackBytes) {
            printf("[%s] %s pops %u bytes but its replacement pops %u - fix the replacement's signature\n",
                   seam->tag, name, seam->entries[i].stackBytes, stackBytes);
        }
        seam->entries[i].replacement = replacement;
        found++;
    }
    if (found == 0)
        printf("[%s] no entry point called %s - has the table been regenerated?\n", seam->tag, name);
    return found;
}

void XbeSeam_Install(XbeEntrySeam *seam) {
    if (!EnsureTrampolines())
        return;

    unsigned implemented = 0, stubbed = 0;
    for (unsigned i = 0; i < seam->count; i++) {
        XbeEntry *entry = &seam->entries[i];

        if (entry->replacement != NULL) {
            WriteJump(seam->tag, entry->address, entry->replacement);
            implemented++;
            continue;
        }
        if (g_slotCount >= MAX_SLOTS) {
            printf("[%s] more than %u entry points across all seams - %s left unpatched\n",
                   seam->tag, (unsigned)MAX_SLOTS, entry->name);
            continue;
        }

        unsigned slot = g_slotCount++;
        g_slots[slot].entry = entry;
        g_slots[slot].tag = seam->tag;

        StubTrampoline *t = &g_trampolines[slot];
        t->pushOpcode = 0x68;                 // push imm32
        t->slot = slot;
        t->jmpOpcode = 0xE9;                  // jmp rel32, measured from the end of this instruction
        t->relativeTarget = (int32_t)((const uint8_t *)StubEntry - ((uint8_t *)&t->relativeTarget + 4));
        WriteJump(seam->tag, entry->address, t);
        stubbed++;
    }

    FlushInstructionCache(GetCurrentProcess(), g_trampolines, MAX_SLOTS * sizeof(StubTrampoline));
    printf("[%s] %u entry points patched, %u of them implemented\n", seam->tag, implemented + stubbed,
           implemented);
    fflush(stdout);
}

void XbeSeam_ReportMissing(XbeEntrySeam *seam) {
    unsigned reached = 0;
    for (unsigned i = 0; i < seam->count; i++)
        reached += (seam->entries[i].calls != 0) ? 1 : 0;
    if (reached == 0)
        return;

    printf("[%s] %u unimplemented entry points reached so far:\n", seam->tag, reached);
    for (unsigned i = 0; i < seam->count; i++) {
        if (seam->entries[i].calls != 0)
            printf("[%s]   %-44s %u\n", seam->tag, seam->entries[i].name, seam->entries[i].calls);
    }
    fflush(stdout);
}
