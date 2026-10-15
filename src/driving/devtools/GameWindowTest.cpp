// The game window and Ptr32 without the game (docs/driving/64-bit.md 7.1): "gamewindow_test [operations]". Checks that
// file structures keep their 32-bit layout, that a Ptr32 converts both ways, and that Host.h's heaps (GameWindow.cpp's
// on a 64-bit host) give blocks that hold their contents through a random mix of allocations, frees and reallocations,
// answer their sizes, honour HEAP_ZERO_MEMORY, HEAP_REALLOC_IN_PLACE_ONLY and a heap's maximum, and lie in the game
// window (the 4 GB from NIGHTFIRE_WINDOW_BASE) with the executable's statics, the pages, operator new's blocks and a
// thread's stack. One summary line; the exit code is the number of failed checks (capped at 100).

#include "../platform/Host.h"
#include "../platform/Ptr32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace {

int g_checks = 0;
int g_failed = 0;

void Check(bool ok, const char *what, long long detail = 0) {
    g_checks++;
    if (!ok) {
        if (g_failed < 20)
            printf("[window] FAILED: %s (%lld)\n", what, detail);
        g_failed++;
    }
}

#if UINTPTR_MAX > 0xffffffffu
const uintptr_t kBase = kGameWindowBase;
#else
const uintptr_t kBase = 0;
#endif

bool InWindow(const void *pointer) {
    return uintptr_t(pointer) - kBase <= 0xffffffffu;
}

struct Node;

struct FileRecord {               // as a structure read from a file
    uint32_t tag;
    Ptr32<Node> node;
    Ptr32<const char> name;
    Ptr32<FileRecord> next;
    Ptr32<void *> table;
};
static_assert(sizeof(FileRecord) == 20, "a file record keeps its layout");

struct Node {
    int value;
};

struct DerivedNode : Node {
    int extra;
};

Node g_staticNode = { 7 };
uint32_t g_random = 12345;

uint32_t Random() {
    g_random = g_random * 1103515245u + 12345u;
    return g_random >> 8;
}

void TestPtr32() {
    FileRecord record = {};
    Check(record.node == NULL && !record.node, "a zero word is null");
    record.node = &g_staticNode;
    Check(record.node->value == 7 && (*record.node).value == 7, "-> and * through a static");
    Node *back = record.node;
    Check(back == &g_staticNode, "back to the pointer");
    Check(PtrTo32(&g_staticNode) == uint32_t(uintptr_t(&g_staticNode) - kBase), "the word is the offset in the window");
    Check(PtrFrom32<Node>(PtrTo32(&g_staticNode)) == &g_staticNode, "PtrFrom32 undoes PtrTo32");
    Check(PtrTo32(NULL) == 0 && PtrFrom32<Node>(0) == NULL, "null is the word 0 both ways");
    record.node = NULL;
    Check(record.node == NULL && PtrTo32(record.node) == 0, "a null Ptr32 holds 0");
    record.name = "name";
    Check(strcmp(record.name, "name") == 0, "a string through a Ptr32");
    Node nodes[4] = { { 0 }, { 1 }, { 2 }, { 3 } };
    record.node = nodes;
    Check(record.node[2].value == 2 && (record.node + 3)->value == 3, "[] and +");
    record.node++;
    Check(record.node->value == 1, "++");
    record.node += 2;
    Check(record.node->value == 3 && record.node - nodes == 3, "+= and -");
    record.node--;
    Check(record.node == &nodes[2] && record.node != &nodes[1], "-- and comparisons");
    DerivedNode derived;
    derived.value = 5;
    derived.extra = 6;
    record.node = &derived;                   // to the base
    Check(static_cast<DerivedNode *>(record.node)->extra == 6, "a cast to the derived class");
    Ptr32<const Node> constant = record.node;
    Check(constant->value == 5, "to a pointer to const");
    record.next = &record;
    Check(record.next->next->tag == record.tag, "a chain");
}

struct Live {
    uint8_t *block;
    size_t size;
    uint8_t fill;
};

bool Holds(const Live &live) {
    for (size_t i = 0; i < live.size; i++)
        if (live.block[i] != uint8_t(live.fill + i))
            return false;
    return true;
}

void Fill(Live &live) {
    for (size_t i = 0; i < live.size; i++)
        live.block[i] = uint8_t(live.fill + i);
}

size_t RandomSize() {
    switch (Random() % 8) {
    case 0: return Random() % 16;
    case 1: case 2: case 3: return Random() % 256;
    case 4: case 5: return Random() % 4096;
    case 6: return Random() % 65536;
    default: return Random() % (1 << 20);
    }
}

void TestHeap(int operations) {
    HostHandle heap = HostHeapCreate(0, 0x1000, 0);
    Check(heap != NULL, "a growable heap");
    std::vector<Live> live;
    int outside = 0, lost = 0, sizes = 0, zero = 0;
    for (int op = 0; op < operations; op++) {
        unsigned what = Random() % 10;
        if (live.empty() || (what < 4 && live.size() < 2000)) {
            Live l;
            l.size = RandomSize();
            l.fill = uint8_t(Random());
            bool zeroed = (Random() & 1) != 0;
            l.block = static_cast<uint8_t *>(HostHeapAlloc(heap, zeroed ? HOST_HEAP_ZERO_MEMORY : 0, l.size));
            Check(l.block != NULL, "an allocation", (long long)(l.size));
            if (l.block == NULL)
                continue;
            outside += !InWindow(l.block) || !InWindow(l.block + l.size);
            if ((uintptr_t(l.block) & (2 * sizeof(void *) - 1)) != 0)   // as HeapAlloc aligns
                Check(false, "alignment", (long long)(uintptr_t(l.block) & 15));
            if (zeroed)
                for (size_t i = 0; i < l.size; i++)
                    zero += l.block[i] != 0;
            Fill(l);
            live.push_back(l);
        } else if (what < 8) {
            size_t i = Random() % live.size();
            lost += !Holds(live[i]);
            sizes += HostHeapSize(heap, 0, live[i].block) != live[i].size;
            Check(HostHeapFree(heap, 0, live[i].block) != 0, "a free");
            live[i] = live.back();
            live.pop_back();
        } else {
            size_t i = Random() % live.size();
            Live &l = live[i];
            lost += !Holds(l);
            size_t size = RandomSize();
            bool zeroed = (Random() & 1) != 0;
            bool inPlace = (Random() % 4) == 0;
            HostDword flags = (zeroed ? HOST_HEAP_ZERO_MEMORY : 0) | (inPlace ? 0x10 : 0);
            uint8_t *moved = static_cast<uint8_t *>(HostHeapReAlloc(heap, flags, l.block, size));
            if (moved == NULL) {
                Check(inPlace, "a reallocation fails only in place", (long long)(size));
                continue;
            }
            Check(!inPlace || moved == l.block, "in place stays in place");
            outside += !InWindow(moved) || !InWindow(moved + size);
            size_t kept = l.size < size ? l.size : size;
            for (size_t k = 0; k < kept; k++)
                lost += moved[k] != uint8_t(l.fill + k);
            if (zeroed)
                for (size_t k = kept; k < size; k++)
                    zero += moved[k] != 0;
            l.block = moved;
            l.size = size;
            l.fill = uint8_t(Random());
            Fill(l);
        }
    }
    for (Live &l : live) {
        lost += !Holds(l);
        sizes += HostHeapSize(heap, 0, l.block) != l.size;
        HostHeapFree(heap, 0, l.block);
    }
    Check(outside == 0, "every block in the window", outside);
    Check(lost == 0, "every block keeps its contents", lost);
    Check(sizes == 0, "HostHeapSize answers the size asked for", sizes);
    Check(zero == 0, "HEAP_ZERO_MEMORY zeroes", zero);
    Check(HostHeapFree(heap, 0, NULL) != 0, "freeing null succeeds");
}

void TestLimits() {
    HostHandle fixed = HostHeapCreate(0, 0x1000, 0x10000);
    std::vector<void *> blocks;
    for (int i = 0; i < 64; i++) {
        void *block = HostHeapAlloc(fixed, 0, 0x1000);
        if (block == NULL)
            break;
        blocks.push_back(block);
    }
    Check(!blocks.empty() && blocks.size() < 64, "a heap with a maximum fills", (long long)(blocks.size()));
    for (void *block : blocks)
        HostHeapFree(fixed, 0, block);
    Check(HostHeapAlloc(fixed, 0, 0x1000) != NULL, "and has room again once emptied");

    void *pages = HostAllocatePages(0x30000, 0, 0x10000000);
    Check(pages != NULL && InWindow(pages) && uintptr_t(pages) - kBase >= 0x10000, "contiguous pages in the window");
    HostFreePages(pages);
    int *object = new int[100];
    Check(InWindow(object), "operator new's blocks in the window");
    delete[] object;
    Check(InWindow(&g_staticNode) && InWindow((const void *)&TestLimits), "the statics and code in the window");
}

HostDword __stdcall StackThread(void *parameter) {
    int local = 0;
    *static_cast<void **>(parameter) = &local;
    return 0;
}

void TestThreadStack() {
    void *local = NULL;
    HostHandle thread = HostCreateThread(0x10000, StackThread, &local, 0, NULL);
    Check(thread != NULL, "a thread");
    if (thread == NULL)
        return;
    HostWaitForObject(thread, HOST_INFINITE);
    HostCloseHandle(thread);
    Check(local != NULL && InWindow(local), "a thread's stack in the window", (long long)uintptr_t(local));
}

int g_operations;

// On a thread of the host's, as the game runs: its locals are in the window (the process's first thread's need not be)
HostDword __stdcall Tests(void *) {
    TestPtr32();
    TestHeap(g_operations);
    TestLimits();
    TestThreadStack();
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    int operations = argc > 1 ? atoi(argv[1]) : 200000;
    g_operations = operations;
    HostHandle thread = HostCreateThread(0x10000, Tests, NULL, 0, NULL);
    Check(thread != NULL, "the tests' thread");
    if (thread != NULL) {
        HostWaitForObject(thread, HOST_INFINITE);
        HostCloseHandle(thread);
    }
    printf("[window] gamewindow_test: %d operations, %d checks, %d differ (%d-bit, window at 0x%llx, image at %p)\n",
           operations, g_checks, g_failed, int(sizeof(void *) * 8), (unsigned long long)kBase, (void *)&g_staticNode);
    return g_failed < 100 ? g_failed : 100;
}
