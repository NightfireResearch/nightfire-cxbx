// Shadow tests for parcel 16's package A: the menu framework - the skins (Component_*), finding controls, the
// dispatch, input, sprites, sounds and the pulse (ui/Menu.cpp), the keyframe scripts (ui/MenuScript.cpp) and
// LinkedList_GetIndex (ui/LinkedList.cpp). Each function, the original and ours, on the same made-up state: managers
// with pages of controls of every type (a cycle among them now and then, for the sends' step limit), skin sets and
// their pieces, sprites, scripts with keyframes and messages, pads' input, with zeros of both signs, NaNs and
// boundary values among the numbers. What the functions call that is not theirs - the control types' handlers and
// updates, the heap, the sprites, the fonts, the sounds, the processes, the input - goes to a logging fake.
// Run at start with MenuShadowTests=on (MenuProbe.cpp); NIGHTFIRE_SHADOW_ONLY=menuport runs it alone,
// NIGHTFIRE_MENUPORT_GROUP=<part of a name> one group.

#include "MenuPortShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../input.h"
#include "../ui/Component.h"
#include "../ui/LinkedList.h"
#include "../ui/Manager.h"
#include "../ui/Menu.h"
#include "../ui/MenuScript.h"
#include "../ui/TextBox.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

namespace {

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))
#ifndef COUNT
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#endif

const int CONTROLS = 24;
const int PAGES = 4;
const int SETS = 3;
const int PIECES = 8;
const int SPRITES = 48;
const int SCRIPTS = 12;
const int KEYS = 64;
const int MESSAGES = 40;
const int HEAP = 0x4000;

struct Ctrl {
    union {
        M_CONTROL c;
        M_WIDGET w;
        M_WINDOW win;
        uint8_t raw[0x120];
    };
};

struct World {
    Ctrl controls[CONTROLS];
    M_PAGE pages[PAGES];
    sprite cursor;
    sprite sprites[SPRITES];
    sprite *spriteTables[CONTROLS][PIECES];
    sprite spritePool[8];
    SpriteInfo infos[2];
    MENU_COMPONENTSET sets[SETS];
    MENU_COMPONENTPIECE pieces[SETS * 4][PIECES];
    M_SCRIPT scripts[SCRIPTS];
    M_KEYFRAME keys[KEYS];
    M_MESSAGE messages[MESSAGES];
    M_KEYFRAME keyArg;
    M_MESSAGE msgArg;
    LINKEDLIST list;
    void *linkArea[32];
    char text[4][24];
    char format[8];
    uint8_t heap[HEAP];
    uint32_t heapUsed;
    uint32_t spritePoolUsed;
    uint32_t status;
    ushort inputAnswer[4][40];
    int window;
    void *slot;
};
World world;

// The game's state the package reads and writes
#define GameManagers (GameSpan<M_MANAGER>{0x0025f1d0})
#define GameComponentList (*(LINKEDLIST *)XbeOurs(0x00223a90))
#define GameMenuFrame U32_AT(XbeOurs(0x00223aa0))
#define GameLastAction U32_AT(XbeOurs(0x00223aa4))
#define GameLastController U32_AT(XbeOurs(0x0017c540))
#define GameAlpha I16_AT(XbeOurs(0x00223b2c))
#define GameAlphaPhase FLOAT_AT(XbeOurs(0x00223b30))
#define GameSounds U32_AT(XbeOurs(0x00223b34))
#define GameTextScale FLOAT_AT(XbeOurs(0x00223b38))
#define GameTextPhase FLOAT_AT(XbeOurs(0x00223b3c))

int cases, differ, nanChoices;
uint32_t gen, caseSeed;

uint32_t R(uint32_t n) {
    gen = gen * 1664525u + 1013904223u;
    uint32_t v = gen ^ (gen >> 15);
    v *= 0x2c1b3c6du;
    v ^= v >> 12;
    v *= 0x297a2d39u;
    v ^= v >> 15;
    return n ? v % n : v;
}

float RF(float lo, float hi) {
    return lo + (hi - lo) * (float)R(0x10000) / 65535.0f;
}

// A number for a float: mostly in range, sometimes a signed zero, an end, a NaN or an infinity
float Num(float lo, float hi) {
    switch (R(24)) {
        case 0: return 0.0f;
        case 1: return -0.0f;
        case 2: { uint32_t u = 0x7fc00000u | R(0x400000); float f; memcpy(&f, &u, 4); return f; }
        case 3: return hi;
        case 4: return lo;
        case 5: return R(2) ? INFINITY : -INFINITY;
        default: return RF(lo, hi);
    }
}

// A short: mostly small, sometimes at an end or negative
short S(int range) {
    switch (R(16)) {
        case 0: return 0;
        case 1: return (short)0x7fff;
        case 2: return (short)0x8000;
        case 3: return (short)-(int)R(range);
        default: return (short)R(range);
    }
}

uint32_t W(const void *p) {
    if (p == NULL)
        return 0xffffffffu;
    uintptr_t at = (uintptr_t)p, base = (uintptr_t)&world;
    if (at >= base && at < base + sizeof(world))
        return (uint32_t)(at - base);
    return 0x80000000u | (uint32_t)at;
}

uint32_t P(const void *p) {
    return (uint32_t)(uintptr_t)p;
}

uint32_t F(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

// ---- calling originals with register arguments

struct Regs {
    uint32_t eax, ebx, ecx, edx, esi, edi;
};

__declspec(naked) uint32_t __cdecl CallRegs(unsigned at, const Regs *r, int argc, const uint32_t *args) {
    _asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ecx, [ebp + 16]
        mov edx, [ebp + 20]
    pushing:
        test ecx, ecx
        jz pushed
        push dword ptr [edx + ecx * 4 - 4]
        dec ecx
        jmp pushing
    pushed:
        mov eax, [ebp + 12]
        mov ebx, [eax + 4]
        mov ecx, [eax + 8]
        mov edx, [eax + 12]
        mov esi, [eax + 16]
        mov edi, [eax + 20]
        mov eax, [eax]
        call dword ptr [ebp + 8]
        mov ecx, [ebp + 16]
        lea esp, [esp + ecx * 4]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

typedef uint32_t (*Runner)(bool original);
struct Case {
    const char *name;
    Runner run;
};
const Case *current;

__declspec(noinline) void ZeroStack(void) {
    volatile uint32_t below[0x1800];
    for (int i = 0; i < 0x1800; i++)
        below[i] = 0;
}

uint32_t RunCase(bool original) {
    gen = caseSeed;
    world.heapUsed = 0;
    world.spritePoolUsed = 0;
    ZeroStack();
    return current->run(original);
}

uint32_t RegCall(bool original, unsigned at, const void *ours, const Regs &r, int argc, const uint32_t *args) {
    if (original) {
        XbeOriginalScope o(at);
        ZeroStack();
        return CallRegs(at, &r, argc, args);
    }
    ZeroStack();
    return CallRegs(P(ours), &r, argc, args);
}

#define CDECL_RUN(at, ours, type, ...)                                                                                \
    (original ? ([&]() { XbeOriginalScope o(at); ZeroStack(); return ORIGINAL(at, type)(__VA_ARGS__); }()) \
              : ([&]() { ZeroStack(); return ours(__VA_ARGS__); }()))

#define CDECL_VOID(at, ours, type, ...)                                                                               \
    do {                                                                                                              \
        ZeroStack();                                                                                                  \
        if (original) {                                                                                               \
            XbeOriginalScope o(at);                                                                                   \
            ORIGINAL(at, type)(__VA_ARGS__);                                                                          \
        } else {                                                                                                      \
            ours(__VA_ARGS__);                                                                                        \
        }                                                                                                             \
    } while (0)

// ---- the regions and the comparison (two NaNs the same: the x87 and SSE2 choose differently between two)

struct Area {
    uint8_t *at;
    size_t size;
};
Area areas[24];
int areaCount;
ShadowRegions regions;

void AddArea(void *at, size_t size) {
    regions.Add(at, size);
    areas[areaCount].at = (uint8_t *)at;
    areas[areaCount++].size = size;
}

bool IsNaNWord(const uint8_t *p) {
    uint32_t u;
    memcpy(&u, p, 4);
    return (u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) != 0;
}

bool CompareCase(const char *what, int trial) {
    static std::vector<uint8_t> theirState, ourState;
    regions.Save();
    ShadowLog_Clear();
    uint32_t theirResult = RunCase(true);
    regions.Capture(theirState);
    std::string theirLog = ShadowLog_Text();
    regions.Restore();
    ShadowLog_Clear();
    uint32_t ourResult = RunCase(false);
    regions.Capture(ourState);
    const std::string &ourLog = ShadowLog_Text();

    static std::map<std::string, int> reports;
    int &reported = reports[what];
    bool same = true;
    if (theirResult != ourResult) {
        same = false;
        if (reported++ < 3)
            printf("[menuport] %s case %d: returns 0x%x, the original 0x%x\n", what, trial, ourResult, theirResult);
    }
    size_t at = 0;
    bool nans = false, memorySame = true;
    bool identical = theirState.size() == ourState.size() &&
                     memcmp(theirState.data(), ourState.data(), theirState.size()) == 0;
    for (int a = 0; a < areaCount && memorySame && !identical; a++) {
        for (size_t j = 0; j < areas[a].size; j++) {
            if (theirState[at + j] == ourState[at + j])
                continue;
            size_t window = j >= 3 ? j - 3 : 0;
            for (; window <= j && window + 4 <= areas[a].size; window++) {
                if (IsNaNWord(&theirState[at + window]) && IsNaNWord(&ourState[at + window]))
                    break;
            }
            if (window <= j && window + 4 <= areas[a].size) {
                nans = true;
                j = window + 3;
                continue;
            }
            size_t word = j & ~(size_t)3;
            memorySame = false;
            if (reported++ < 3) {
                uint32_t theirs = 0, ours = 0;
                if (word + 4 <= areas[a].size) {
                    memcpy(&theirs, &theirState[at + word], 4);
                    memcpy(&ours, &ourState[at + word], 4);
                }
                printf("[menuport] %s case %d: memory differs in region %d at +0x%x: 0x%08x, the original 0x%08x\n",
                       what, trial, a, (unsigned)word, ours, theirs);
            }
            break;
        }
        at += areas[a].size;
    }
    same = same && memorySame;
    if (theirLog != ourLog) {
        same = false;
        if (reported++ < 3) {
            size_t d = 0;
            while (d < theirLog.size() && d < ourLog.size() && theirLog[d] == ourLog[d])
                d++;
            size_t from = d > 120 ? d - 120 : 0;
            printf("[menuport] %s case %d: calls differ at %d\n  ours:     %.400s\n  original: %.400s\n", what, trial,
                   (int)d, ourLog.c_str() + from, theirLog.c_str() + from);
        }
    }
    if (same && nans)
        nanChoices++;
    return same;
}

void RandomWorld(void);

void Test(const char *group, const Case *list, int count, int trials) {
    int groupCases = cases, groupDiffer = differ;
    for (int c = 0; c < count; c++) {
        current = &list[c];
        if (getenv("NIGHTFIRE_SHADOW_TRACE") != NULL) {
            printf("[menuport] %s\n", current->name);
            fflush(stdout);
        }
        for (int trial = 0; trial < trials; trial++) {
            gen = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
            RandomWorld();
            caseSeed = gen;
            cases++;
            if (!CompareCase(current->name, trial))
                differ++;
        }
    }
    printf("[menuport] %s: %d cases, %d differ\n", group, cases - groupCases, differ - groupDiffer);
    fflush(stdout);
}

#define C(name, run) {name, run}

// ---- the fakes

uint32_t Answer(void) {
    return R(4) == 0 ? 0 : R(0);
}

#define SEND_FAKE(name, tag)                                                                                          \
    int __cdecl name(M_CONTROL *c, uint message, int a1, int a2) {                                                    \
        ShadowLog(tag "(%x,%x,%x,%x)", W(c), message, (uint32_t)a1, (uint32_t)a2);                                    \
        return (int)Answer();                                                                                         \
    }
SEND_FAKE(FakeButtonSend, "button")
SEND_FAKE(FakeCheckboxSend, "checkbox")
SEND_FAKE(FakeComboSend, "combo")
SEND_FAKE(FakeLabelSend, "label")
SEND_FAKE(FakeListSend, "list")
SEND_FAKE(FakeManagerSend, "manager")
SEND_FAKE(FakeMemoSend, "memo")
SEND_FAKE(FakePageSend, "page")
SEND_FAKE(FakeRadioSend, "radio")
SEND_FAKE(FakeScrollSend, "scroll")
SEND_FAKE(FakeSpinSend, "spin")
SEND_FAKE(FakeTextSend, "text")
SEND_FAKE(FakeWindowSend, "window")

#define UPDATE_FAKE(name, tag)                                                                                        \
    void __cdecl name(M_MANAGER *m, M_PAGE *page, M_CONTROL *c, int a4) {                                             \
        ShadowLog(tag "(%x,%x,%x,%x)", (uint32_t)((uintptr_t)m - XbeOurs(0x0025f1d0)), W(page), W(c), (uint32_t)a4);                                                                                      \
    }
UPDATE_FAKE(FakeButtonUpdate, "buttonupd")
UPDATE_FAKE(FakeCheckboxUpdate, "checkboxupd")
UPDATE_FAKE(FakeComboUpdate, "comboupd")
UPDATE_FAKE(FakeListUpdate, "listupd")
UPDATE_FAKE(FakeMemoUpdate, "memoupd")
UPDATE_FAKE(FakeRadioUpdate, "radioupd")
UPDATE_FAKE(FakeScrollUpdate, "scrollupd")
UPDATE_FAKE(FakeSpinUpdate, "spinupd")
UPDATE_FAKE(FakeTextUpdate, "textupd")

void __cdecl FakeLabelUpdate(M_MANAGER *m, M_PAGE *page, M_CONTROL *c, int cursorOver) {
    ShadowLog("labelupd(%x,%x,%x,%x)", (uint32_t)((uintptr_t)m - XbeOurs(0x0025f1d0)), W(page), W(c), cursorOver & 0xff);
}

void __cdecl FakePageUpdate(M_MANAGER *m, M_PAGE *page, int takingInput) {
    ShadowLog("pageupd(%x,%x,%x)", (uint32_t)((uintptr_t)m - XbeOurs(0x0025f1d0)), W(page), (uint32_t)takingInput);
}

uint __cdecl FakeStatus(void) {
    ShadowLog("status");
    return world.status;
}

ushort __cdecl FakeInputAction(short player, GameActions_tag action, uchar flags) {
    ShadowLog("action(%x,%x,%x)", (uint32_t)(ushort)player, (uint32_t)action, (uint32_t)flags);
    return world.inputAnswer[(ushort)player & 3][(uint32_t)action % 40] & (flags ? 0xffff : 0);
}

void __cdecl FakeSetAction(short player, GameActions_tag action, uchar value) {
    ShadowLog("setaction(%x,%x,%x)", (uint32_t)(ushort)player, (uint32_t)action, (uint32_t)value);
}

void *__cdecl FakeMalloc(size_t size, MallocFlags flags, uint32_t align) {
    ShadowLog("malloc(%x,%x,%x)", (uint32_t)size, (uint16_t)flags, align);
    if (size > HEAP / 2)
        return world.heap;
    uint32_t at = (world.heapUsed + 3) & ~3u;
    if (at + size > HEAP)
        at = 0;
    world.heapUsed = at + (uint32_t)size;
    memset(&world.heap[at], 0xcd, size);
    return &world.heap[at];
}

void __cdecl FakeFree(void **p) {
    // A local's address (the slot the original frees through) is not compared
    uint32_t slot = W(p);
    ShadowLog("free(%x,%x)", slot & 0x80000000u ? 0x80000000u : slot, W(*p));
    *p = NULL;
}

void LogInfo(const SpriteInfo *info) {
    const uint32_t *w = (const uint32_t *)info;
    ShadowLog("info(%x:%x %x %x %x %x %x %x %x %x %x %x)", W(info), w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7],
              W((void *)(uintptr_t)w[8]), w[9], w[10] & 0xff);
}

sprite *__cdecl FakeSpriteCreate(void) {
    ShadowLog("spritecreate");
    return &world.spritePool[world.spritePoolUsed++ % 8];
}

sprite *__cdecl FakeSpriteCreate2(SpriteInfo *info) {
    ShadowLog("spritecreate2");
    LogInfo(info);
    return &world.spritePool[world.spritePoolUsed++ % 8];
}

void __cdecl FakeSpriteDelete(sprite *spr) {
    ShadowLog("spritedelete(%x)", W(spr));
}

bool __cdecl FakeSetSprite(sprite *spr, HASHCODE hc) {
    ShadowLog("setsprite(%x,%x)", W(spr), (uint32_t)hc);
    return R(2) != 0;
}

uint32_t __cdecl FakeSoundPlayExt(uint32_t sound, float volume, uint32_t pitch, uint32_t tag) {
    ShadowLog("sound(%x,%x,%x,%x)", sound, F(volume), pitch & 0xffff, tag);
    return 0;
}

uint __cdecl FakeCountExtent(const char *text, const char *format, const GPOINT *scale, float width) {
    ShadowLog("countextent(%x,%x,%x,%x)", W(text), W(format), W(scale), F(width));
    return R(4) == 0 ? R(0) : R(30);
}

bool __cdecl FakeTextExtent(const char *text, const char *format, GPOINT *extent, const GPOINT *scale, int count) {
    uint32_t at = W(extent);
    ShadowLog("textextent(%x,%x,%x,%x,%x)", W(text), W(format), at & 0x80000000u ? 0x80000000u : at, W(scale),
              (uint32_t)count);
    extent->x = Num(-10.0f, 700.0f);
    extent->y = Num(-10.0f, 70.0f);
    return true;
}

void __cdecl FakeProcessInit(M_CONTROL *c) {
    ShadowLog("processinit(%x)", W(c));
}

void __cdecl FakeProcessCreate(M_CONTROL *c, M_MESSAGE *msg, int frames, int unknown) {
    ShadowLog("processcreate(%x,%x,%x,%x)", W(c), W(msg), (uint32_t)frames, (uint32_t)unknown);
}

void __cdecl FakeProcessRunFrame(M_CONTROL *c) {
    ShadowLog("processrun(%x)", W(c));
}

void __cdecl FakeProcessDestroy(M_CONTROL *c) {
    ShadowLog("processdestroy(%x)", W(c));
}

M_CONTROL *__cdecl FakeWindowGet(M_CONTROL *page) {
    ShadowLog("windowget(%x)", W(page));
    return world.window < 0 ? NULL : &world.controls[world.window].c;
}

void Hook(unsigned at, const void *fake) {
    ShadowHook_Install(at, fake);
}

void InstallFakes(void) {
    Hook(0x00070e00, (const void *)&FakeButtonSend);
    Hook(0x00071380, (const void *)&FakeCheckboxSend);
    Hook(0x00071890, (const void *)&FakeComboSend);
    Hook(0x00090590, (const void *)&FakeLabelSend);
    Hook(0x00091b10, (const void *)&FakeListSend);
    Hook(0x00092c40, (const void *)&FakeManagerSend);
    Hook(0x00095330, (const void *)&FakeMemoSend);
    Hook(0x00095820, (const void *)&FakePageSend);
    Hook(0x00096380, (const void *)&FakeRadioSend);
    Hook(0x00097870, (const void *)&FakeScrollSend);
    Hook(0x000986f0, (const void *)&FakeSpinSend);
    Hook(0x00098be0, (const void *)&FakeTextSend);
    Hook(0x00099c60, (const void *)&FakeWindowSend);
    Hook(0x00070fe0, (const void *)&FakeButtonUpdate);
    Hook(0x00071530, (const void *)&FakeCheckboxUpdate);
    Hook(0x000719b0, (const void *)&FakeComboUpdate);
    Hook(0x0008fff0, (const void *)&FakeLabelUpdate);
    Hook(0x00090fc0, (const void *)&FakeListUpdate);
    Hook(0x00094cf0, (const void *)&FakeMemoUpdate);
    Hook(0x00095a80, (const void *)&FakePageUpdate);
    Hook(0x00096210, (const void *)&FakeRadioUpdate);
    Hook(0x00097be0, (const void *)&FakeScrollUpdate);
    Hook(0x000989c0, (const void *)&FakeSpinUpdate);
    Hook(0x00098de0, (const void *)&FakeTextUpdate);
    Hook(0x00092b80, (const void *)&FakeStatus);
    Hook(0x0006c9a0, (const void *)&FakeInputAction);
    Hook(0x0006cb10, (const void *)&FakeSetAction);
    Hook(0x00070ae0, (const void *)&FakeMalloc);
    Hook(0x000705f0, (const void *)&FakeFree);
    Hook(0x000cec90, (const void *)&FakeSpriteCreate);
    Hook(0x000cedf0, (const void *)&FakeSpriteCreate2);
    Hook(0x000cecf0, (const void *)&FakeSpriteDelete);
    Hook(0x0006b670, (const void *)&FakeSetSprite);
    Hook(0x000cb940, (const void *)&FakeSoundPlayExt);
    Hook(0x0006a670, (const void *)&FakeCountExtent);
    Hook(0x0006a5d0, (const void *)&FakeTextExtent);
    Hook(0x00095dc0, (const void *)&FakeProcessInit);
    Hook(0x00095de0, (const void *)&FakeProcessCreate);
    Hook(0x00095e60, (const void *)&FakeProcessRunFrame);
    Hook(0x00096090, (const void *)&FakeProcessDestroy);
    Hook(0x00099d50, (const void *)&FakeWindowGet);
}

// ---- the made-up world

const uchar kTypes[] = {ControlType_Button, ControlType_Checkbox, ControlType_Combo, 4, ControlType_Label,
                        ControlType_List, ControlType_Manager, ControlType_Page, ControlType_Radio, ControlType_Scroll,
                        ControlType_Spin, ControlType_Text, ControlType_Window, (uchar)ControlType_Memo,
                        ControlType_72, 0, 0xff, 14};

HASHCODE Hash(void) {
    return (HASHCODE)(R(6) == 0 ? R(0) : 0x10000001u + R(5));
}

// A list of elements through their link at offset 0: the head and tail point at the elements
void LinkList(LINKEDLIST *list, void **elements, int n) {
    list->head = n ? elements[0] : NULL;
    list->tail = n ? elements[n - 1] : NULL;
    list->count = n;
    list->offset = 0;
    for (int i = 0; i < n; i++) {
        void **link = (void **)elements[i];
        link[0] = i + 1 < n ? elements[i + 1] : NULL;
        link[1] = i > 0 ? elements[i - 1] : NULL;
    }
    if (R(16) == 0)
        list->count = R(4);
}

void RandomSprite(sprite *s) {
    uint8_t *b = (uint8_t *)s;
    for (size_t i = 0; i < sizeof(sprite); i++)
        b[i] = (uint8_t)R(0x100);
    s->text = R(3) ? world.text[R(4)] : NULL;
    s->maybeClippedString = R(2) ? world.format : NULL;
    s->scale.x = Num(0.0f, 2.0f);
    s->scale.y = Num(0.0f, 2.0f);
    s->positionX = S(700);
    s->positionY = S(500);
}

void RandomControl(Ctrl *k, M_PAGE *page) {
    uint8_t *b = k->raw;
    for (size_t i = 0; i < sizeof(k->raw); i++)
        b[i] = (uint8_t)R(0x100);
    M_CONTROL *c = &k->c;
    c->type = R(5) ? kTypes[R(COUNT(kTypes))] : (R(2) ? ControlType_Label : ControlType_Button);
    c->state = R(3) ? 0 : (uchar)R(4);
    c->hashcode = Hash();
    c->id = R(3) ? (int)R(0x100) : (int)R(0);
    c->x = R(8) ? (short)R(600) : S(1000);
    c->y = R(8) ? (short)R(440) : S(1000);
    c->width = R(8) ? (short)R(200) : S(1000);
    c->height = R(8) ? (short)R(120) : S(1000);
    c->depth = (ushort)(R(4) ? R(16) : R(0x10000));
    // Ties: controls of one size, at one depth
    if (R(3) == 0) {
        c->width = 120;
        c->height = 60;
        c->depth = (ushort)R(3);
    }
    int t = (int)(k - world.controls);
    // A control's owner: its page, none, or a control before it (so the chain ends)
    c->parent = R(10) ? &page->control : (R(2) || t == 0 ? NULL : &world.controls[R(t)].c);
    c->skin = (R(SETS + 1) + 1) << 16 | (R(10) ? R(4) : R(14));
    c->drawState = R(0);
    c->numSprites = R(PIECES + 1);
    c->sprites = R(4) ? world.spriteTables[t] : NULL;
    c->key = NULL;
    c->script = NULL;
    c->loopKey = NULL;
    c->scripts.head = c->scripts.tail = NULL;
    c->scripts.count = 0;
    c->scripts.offset = 0;
    for (int s = 0; s < 6; s++)
        k->w.defaultScripts[s] = NULL;
    k->win.page = R(4) ? &world.pages[R(PAGES)] : NULL;
}

void RandomPiece(MENU_COMPONENTPIECE *p) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < sizeof(*p); i++)
        b[i] = (uint8_t)R(0x100);
    p->info.posX = S(60);
    p->info.posY = S(60);
    p->info.onscreenWidth = S(80);
    p->info.onscreenHeight = S(80);
    p->info.maybeClippedText = NULL;
    p->stretchWidth = R(2) ? 0.0f : Num(-0.5f, 1.5f);
    p->stretchHeight = R(2) ? 0.0f : Num(-0.5f, 1.5f);
    p->flags = R(0x4000) | (R(2) ? 0x10 : 0) | (R(2) ? 0x20 : 0);
}

void RandomScripts(void) {
    // Keyframes in frame order, two to six to a script; a message or two on some
    int key = 0, msg = 0;
    for (int s = 0; s < SCRIPTS; s++) {
        M_SCRIPT *script = &world.scripts[s];
        script->id = (int)R(8);
        script->flags = R(4) ? R(2) : R(0x10);
        int n = R(8) ? 1 + (int)R(5) : 0;
        if (key + n > KEYS)
            n = KEYS - key;
        void *elements[8];
        uint frame = R(4);
        for (int i = 0; i < n; i++) {
            M_KEYFRAME *k = &world.keys[key + i];
            frame += R(5) == 0 ? 0 : 1 + R(10);
            k->frame = R(20) ? frame : R(0);
            k->flags = R(3) ? 0 : R(0x40);
            k->x = S(600);
            k->y = S(400);
            k->width = S(300);
            k->height = S(300);
            int m = R(3) == 0 ? (int)R(3) : 0;
            if (msg + m > MESSAGES)
                m = 0;
            void *mel[3];
            for (int j = 0; j < m; j++) {
                M_MESSAGE *e = &world.messages[msg + j];
                static const uint32_t kTargets[] = {MESSAGE_TO_MANAGER, MESSAGE_TO_SELF, 0x10000001, 0x10000003, 0};
                e->target = (HASHCODE)kTargets[R(COUNT(kTargets))];
                e->type = R(4) ? R(0x80) : (R(2) ? 0xe1 : R(0));
                e->arg1 = R(3) ? (int)R(100) : -4;
                e->arg2 = (int)R(0);
                mel[j] = e;
            }
            LinkList(&k->messages, mel, m);
            k->messages.count = m;
            msg += m;
            elements[i] = k;
        }
        LinkList(&script->keys, elements, n);
        script->keys.count = n;
        key += n;
    }
    // Some controls get some of the scripts, playing one of them
    for (int c = 0; c < 8; c++) {
        Ctrl *k = &world.controls[c];
        int first = (int)R(SCRIPTS), n = (int)R(4);
        if (first + n > SCRIPTS)
            n = SCRIPTS - first;
        void *elements[4];
        for (int i = 0; i < n; i++)
            elements[i] = &world.scripts[first + i];
        LinkList(&k->c.scripts, elements, n);
        k->c.scripts.count = n;
        if (n > 0) {
            M_SCRIPT *s = &world.scripts[first + R(n)];
            k->c.script = s;
            int which = (int)R(s->keys.count + 1);
            M_KEYFRAME *at = (M_KEYFRAME *)LinkedList_First(&s->keys);
            for (int i = 0; i < which && at != NULL && at->next != NULL; i++)
                at = at->next;
            k->c.key = R(6) ? at : NULL;
            k->c.loopKey = (M_KEYFRAME *)LinkedList_First(&s->keys);
            k->c.step = R(4) ? (int)R(5) : (int)R(0);
            k->c.loopStep = (int)R(5);
            k->c.frame = at != NULL && R(2) ? (int)at->frame - (int)R(3) : (int)R(60);
            k->c.lastScriptResult = (int)R(0);
            for (int sl = 0; sl < 6; sl++)
                k->w.defaultScripts[sl] = R(2) ? &world.scripts[first + R(n)] : NULL;
        }
    }
}

void RandomWorld(void) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 23; j++)
            world.text[i][j] = (char)('A' + R(26));
        world.text[i][23] = 0;
        world.text[i][R(23)] = 0;
    }
    memcpy(world.format, "\xff\x02\xfc\x64\xfb\x64", 7);
    RandomSprite(&world.cursor);
    world.cursor.positionX = R(4) ? (short)R(640) : S(1000);
    world.cursor.positionY = R(4) ? (short)R(480) : S(1000);
    for (int i = 0; i < SPRITES; i++)
        RandomSprite(&world.sprites[i]);
    for (int i = 0; i < 8; i++)
        RandomSprite(&world.spritePool[i]);
    for (int c = 0; c < CONTROLS; c++)
        for (int p = 0; p < PIECES; p++)
            world.spriteTables[c][p] = &world.sprites[R(SPRITES)];
    for (int i = 0; i < 2; i++) {
        uint8_t *b = (uint8_t *)&world.infos[i];
        for (size_t j = 0; j < sizeof(SpriteInfo); j++)
            b[j] = (uint8_t)R(0x100);
    }

    // Pages, and the controls shared out among them in order
    for (int p = 0; p < PAGES; p++) {
        M_PAGE *page = &world.pages[p];
        uint8_t *b = (uint8_t *)page;
        for (size_t i = 0; i < sizeof(*page); i++)
            b[i] = (uint8_t)R(0x100);
        page->control.type = ControlType_Page;
        page->control.x = S(100);
        page->control.y = S(100);
        page->control.depth = (ushort)R(8);
        page->control.parent = NULL;
    }
    int at = 0;
    for (int p = 0; p < PAGES; p++) {
        int n = p == PAGES - 1 ? CONTROLS - at : (int)R(CONTROLS - at + 1);
        if (R(6) == 0)
            n = 0;
        void *elements[CONTROLS];
        for (int i = 0; i < n; i++) {
            RandomControl(&world.controls[at + i], &world.pages[p]);
            elements[i] = &world.controls[at + i];
        }
        LinkList(&world.pages[p].controls, elements, n);
        world.pages[p].controls.count = n;
        world.pages[p].focused = R(3) ? (n ? &world.controls[at + R(n)].c : NULL) : (R(2) ? NULL : &world.controls[R(CONTROLS)].c);
        at += n;
    }
    for (; at < CONTROLS; at++)
        RandomControl(&world.controls[at], &world.pages[0]);
    world.window = R(4) ? (int)R(CONTROLS) : -1;

    // The managers: pages through their links, the cursor, the current and under page
    XbeOursFill(GameManagers.at, 0, sizeof(M_MANAGER) * 5);
    for (int m = 0; m < 5; m++) {
        M_MANAGER *mgr = &GameManagers[m];
        int first = (int)R(PAGES), n = (int)R(PAGES - first + 1);
        mgr->pages.count = n;
        mgr->pages.offset = offsetof(M_PAGE, pageLink) / 4;
        mgr->pages.head = n ? world.pages[first].pageLink : NULL;
        mgr->pages.tail = n ? world.pages[first + n - 1].pageLink : NULL;
        for (int i = 0; i < n; i++) {
            M_PAGE *page = &world.pages[first + i];
            page->pageLink[0] = i + 1 < n ? world.pages[first + i + 1].pageLink : NULL;
            page->pageLink[1] = i > 0 ? world.pages[first + i - 1].pageLink : NULL;
        }
        mgr->control.type = ControlType_Manager;
        mgr->index = (uchar)m;
        mgr->cursorSprite = &world.cursor;
        mgr->currentPage = R(8) ? &world.pages[R(PAGES)] : NULL;
        mgr->underPage = R(2) ? &world.pages[R(PAGES)] : NULL;
        mgr->whichPlayer = (short)(R(2) ? -1 : R(4));
    }

    // The skin sets of the component list
    void *sets[SETS];
    for (int s = 0; s < SETS; s++) {
        MENU_COMPONENTSET *set = &world.sets[s];
        set->id = R(8) ? s + 1 : R(5);
        set->managerNum = (uchar)R(3);
        for (int c = 0; c < COMPONENTS_PER_SET; c++) {
            MENU_COMPONENT *comp = &set->components[c];
            comp->texture = (HASHCODE)R(0);
            comp->count = (ushort)R(PIECES + 1);
            comp->left = S(20);
            comp->top = S(20);
            comp->right = S(20);
            comp->bottom = S(20);
            comp->pieces = c < 4 && R(5) ? world.pieces[s * 4 + c] : NULL;
            if (comp->pieces == NULL)
                comp->count = 0;
        }
        sets[s] = set;
    }
    int numSets = (int)R(SETS + 1);
    LinkList(&GameComponentList, sets, numSets);
    GameComponentList.count = numSets + (R(8) == 0 ? R(3) : 0);
    for (int s = 0; s < SETS * 4; s++)
        for (int p = 0; p < PIECES; p++)
            RandomPiece(&world.pieces[s][p]);

    RandomScripts();
    uint8_t *ka = (uint8_t *)&world.keyArg;
    for (size_t i = 0; i < sizeof(world.keyArg); i++)
        ka[i] = (uint8_t)R(0x100);
    world.keyArg.frame = R(3) ? world.keys[R(KEYS)].frame : R(60);
    uint8_t *ma = (uint8_t *)&world.msgArg;
    for (size_t i = 0; i < sizeof(world.msgArg); i++)
        ma[i] = (uint8_t)R(0x100);

    // The pads
    for (int p = 0; p < 4; p++) {
        PlayerInput *input = &PlayerInputs[p];
        for (int c = 0; c < 40; c++) {
            input->fChannels[c] = R(3) ? (float)(R(3)) * 0.5f : Num(-2.0f, 2.0f);
            input->actions[c] = (uchar)(R(3) ? R(2) * 5 : R(0x100));
            world.inputAnswer[p][c] = (ushort)(R(3) ? 0 : R(0x10000));
        }
    }
    world.status = R(7);
    GameMenuFrame = R(0);
    GameLastAction = R(0);
    GameLastController = R(6) - 1;
    GameAlpha = (short)R(0x10000);
    GameAlphaPhase = R(4) ? RF(0.0f, 6.2831855f) : Num(-1.0f, 7.0f);
    // A step from just short of the wrap, where the sum and the sum as stored fall either side of it
    if (R(4) == 0) {
        float phase = 6.0831851f;
        for (int i = (int)R(16); i > 0; i--)
            phase = nextafterf(phase, 7.0f);
        GameAlphaPhase = phase;
    }
    GameSounds = R(0x10);
    GameTextPhase = R(4) ? RF(0.0f, 6.2831855f) : Num(-1.0f, 7.0f);
    if (R(4) == 0) {
        float phase = 6.1831851f;
        for (int i = (int)R(16); i > 0; i--)
            phase = nextafterf(phase, 7.0f);
        GameTextPhase = phase;
    }
    GameTextScale = Num(0.0f, 2.0f);
}

Ctrl *AnyCtrl(void) {
    return &world.controls[R(CONTROLS)];
}

M_CONTROL *AnyControl(void) {
    return R(20) ? &world.controls[R(CONTROLS)].c : NULL;
}

M_MANAGER *AnyManager(void) {
    return &GameManagers[R(5)];
}

// ---- skins

uint32_t RunInitComponents(bool original) {
    CDECL_VOID(0x00071c50, Component_InitComponents, void (__cdecl *)(void));
    return 0;
}

uint32_t RunAddSet(bool original) {
    uchar m = (uchar)R(5);
    ushort id = R(3) ? 999 : (ushort)R(0x10000);
    return W(CDECL_RUN(0x00071c60, Component_AddSet, MENU_COMPONENTSET *(__cdecl *)(uchar, ushort), m, id));
}

uint32_t RunDestroyAll(bool original) {
    uchar m = (uchar)R(4);
    CDECL_VOID(0x00072330, Component_DestroyAll, void (__cdecl *)(uchar), m);
    return 0;
}

uint32_t RunAddComponent(bool original) {
    MENU_COMPONENT *comp = &world.sets[R(SETS)].components[R(COMPONENTS_PER_SET)];
    short l = S(40), t = S(40), r = S(40), b = S(40), count = R(3) ? (short)R(9) : S(100);
    HASHCODE tex = (HASHCODE)R(0);
    return W(CDECL_RUN(0x00071cc0, Component_AddComponent,
                       MENU_COMPONENTPIECE *(__cdecl *)(MENU_COMPONENT *, short, short, short, short, HASHCODE, short),
                       comp, l, t, r, b, tex, count));
}

uint32_t RunInitInstance(bool original) {
    MENU_COMPONENTPIECE *p = &world.pieces[R(SETS * 4)][R(PIECES)];
    short a[8];
    for (int i = 0; i < 8; i++)
        a[i] = S(300);
    uint flags = R(0), colour = R(0);
    float sw = Num(-1.0f, 2.0f), sh = Num(-1.0f, 2.0f);
    CDECL_VOID(0x00071d20, Component_InitInstance,
               void (__cdecl *)(MENU_COMPONENTPIECE *, short, short, short, short, short, short, short, short, uint, uint,
                                float, float),
               p, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], flags, colour, sw, sh);
    return 0;
}

uint32_t RunGetHandle(bool original) {
    M_CONTROL *c = &AnyCtrl()->c;
    return W(CDECL_RUN(0x00071db0, Component_GetHandle, MENU_COMPONENT *(__cdecl *)(M_CONTROL *), c));
}

uint32_t RunSetupInstance(bool original) {
    M_CONTROL *c = &AnyCtrl()->c;
    static const uint kStates[] = {0x10, 0x20, 0x40, 0x80, 0x30, 0xc0, 0xa0, 0};
    uint state = R(4) ? kStates[R(COUNT(kStates))] : R(0);
    int x = R(2) ? -1 : S(300), y = R(2) ? -1 : S(300), w = R(2) ? -1 : S(300), h = R(2) ? -1 : S(300);
    CDECL_VOID(0x00071e00, Component_SetupInstance, void (__cdecl *)(M_CONTROL *, uint, int, int, int, int), c, state,
               x, y, w, h);
    return 0;
}

uint32_t RunComponentFree(bool original) {
    M_CONTROL *c = &AnyCtrl()->c;
    CDECL_VOID(0x000722e0, Component_Free, void (__cdecl *)(M_CONTROL *), c);
    return 0;
}

const Case kSkins[] = {
    C("Component_InitComponents", RunInitComponents), C("Component_AddSet", RunAddSet),
    C("Component_DestroyAll", RunDestroyAll),         C("Component_AddComponent", RunAddComponent),
    C("Component_InitInstance", RunInitInstance),     C("Component_GetHandle", RunGetHandle),
    C("Component_SetupInstance", RunSetupInstance),   C("Component_Free", RunComponentFree),
};

// ---- finding controls

// The cursor over one of the page's controls (an edge, a corner or inside), or anywhere
void AimCursor(M_MANAGER *m) {
    if (m->currentPage == NULL || R(4) == 0)
        return;
    M_CONTROL *c = (M_CONTROL *)LinkedList_First(&m->currentPage->controls);
    for (int n = (int)R(8); c != NULL && c->next != NULL && n > 0; n--)
        c = c->next;
    if (c == NULL)
        return;
    int px = m->currentPage->control.x, py = m->currentPage->control.y;
    switch (R(4)) {
        case 0: world.cursor.positionX = (short)(px + c->x); world.cursor.positionY = (short)(py + c->y); break;
        case 1: world.cursor.positionX = (short)(px + c->x + c->width); world.cursor.positionY = (short)(py + c->y + c->height); break;
        default:
            world.cursor.positionX = (short)(px + c->x + (c->width ? (int)R(abs(c->width)) : 0));
            world.cursor.positionY = (short)(py + c->y + (c->height ? (int)R(abs(c->height)) : 0));
            break;
    }
}

uint32_t RunCursorOverMe(bool original) {
    M_MANAGER *m = AnyManager();
    AimCursor(m);
    M_CONTROL *c = AnyControl();
    return CDECL_RUN(0x000723c0, Menu_CursorOverMe, int (__cdecl *)(M_MANAGER *, M_CONTROL *), m, c) & 0xff;
}

uint32_t RunGetControl(bool original) {
    M_MANAGER *m = AnyManager();
    if (m->currentPage == NULL)
        m->currentPage = &world.pages[0];
    AimCursor(m);
    return W(CDECL_RUN(0x00073370, Menu_GetControl, M_CONTROL *(__cdecl *)(M_MANAGER *), m));
}

uint32_t RunFindControl(bool original) {
    M_MANAGER *m = AnyManager();
    if (m->currentPage == NULL)
        m->currentPage = &world.pages[R(PAGES)];
    AimCursor(m);
    static const uchar kDirs[] = {1, 2, 4, 8, 1, 2, 4, 8, 0, 3, 16, 0xff};
    uchar dir = kDirs[R(COUNT(kDirs))];
    return W(CDECL_RUN(0x000734d0, Menu_FindControl, M_CONTROL *(__cdecl *)(M_MANAGER *, uchar), m, dir));
}

uint32_t RunGetFirstControl(bool original) {
    M_MANAGER *m = AnyManager();
    if (m->currentPage == NULL)
        m->currentPage = &world.pages[R(PAGES)];
    if (m->currentPage->focused != NULL && R(3) == 0)
        m->currentPage->focused->hashcode = (HASHCODE)0x1fffffff;
    return W(CDECL_RUN(0x000724b0, Menu_GetFirstControl, M_CONTROL *(__cdecl *)(M_MANAGER *), m));
}

uint32_t RunGetPage(bool original) {
    M_CONTROL *c = AnyControl();
    return W(CDECL_RUN(0x00073a10, Menu_GetPage, M_PAGE *(__cdecl *)(M_CONTROL *), c));
}

const Case kFinding[] = {
    C("Menu_CursorOverMe", RunCursorOverMe), C("Menu_GetControl", RunGetControl),
    C("Menu_FindControl", RunFindControl),   C("Menu_GetFirstControl", RunGetFirstControl),
    C("Menu_GetPage", RunGetPage),
};

// ---- dispatch

// Now and then a page's controls run round in a circle, for the sends' limit
void MaybeCycle(void) {
    if (R(8) != 0)
        return;
    M_PAGE *page = &world.pages[R(PAGES)];
    M_CONTROL *last = (M_CONTROL *)LinkedList_Last(&page->controls);
    if (last != NULL)
        last->next = (M_CONTROL *)LinkedList_First(&page->controls);
}

uint32_t RunSendMessage(bool original) {
    M_CONTROL *c = R(10) ? &AnyCtrl()->c : (R(2) ? NULL : &GameManagers[R(5)].control);
    uint msg = R(0x80);
    int a1 = R(8) == 0 ? (int)(uintptr_t)c : (int)R(0), a2 = (int)R(0);
    return CDECL_RUN(0x00072630, __Menu_SendMessage, int (__cdecl *)(M_CONTROL *, uint, int, int), c, msg, a1, a2);
}

uint32_t RunUpdate(bool original) {
    M_MANAGER *m = AnyManager();
    M_PAGE *page = &world.pages[R(PAGES)];
    M_CONTROL *c = &AnyCtrl()->c;
    CDECL_VOID(0x00072a90, Menu_Update, void (__cdecl *)(M_MANAGER *, M_PAGE *, M_CONTROL *), m, page, c);
    return 0;
}

HASHCODE SendHash(void) {
    return R(5) == 0 ? (HASHCODE)0xffffffff : Hash();
}

uint32_t RunSend(bool original) {
    MaybeCycle();
    uchar m = (uchar)R(5);
    HASHCODE h = SendHash();
    uint msg = R(0x80);
    int a1 = (int)R(0), a2 = (int)R(0);
    return CDECL_RUN(0x00072820, __Menu_Send, uint (__cdecl *)(uchar, HASHCODE, uint, int, int), m, h, msg, a1, a2);
}

uint32_t RunSendEx(bool original) {
    MaybeCycle();
    byte m = (byte)R(5);
    HASHCODE h = SendHash();
    uint id = R(3) ? world.controls[R(CONTROLS)].c.id : R(0x100);
    uint msg = R(0x80);
    int a1 = (int)R(0), a2 = (int)R(0);
    return CDECL_RUN(0x000728f0, __Menu_SendEx, int (__cdecl *)(byte, HASHCODE, uint, uint, int, int), m, h, id, msg,
                     a1, a2);
}

uint32_t RunSendEx2(bool original) {
    MaybeCycle();
    byte m = (byte)R(5);
    HASHCODE h = SendHash();
    uint bit = R(4) ? 1u << R(8) : R(0x200);
    uint msg = R(0x80);
    int a1 = (int)R(0), a2 = (int)R(0);
    return CDECL_RUN(0x000729e0, __Menu_SendEx2, int (__cdecl *)(byte, HASHCODE, uint, uint, int, int), m, h, bit, msg,
                     a1, a2);
}

const Case kDispatch[] = {
    C("__Menu_SendMessage", RunSendMessage), C("Menu_Update", RunUpdate), C("__Menu_Send", RunSend),
    C("__Menu_SendEx", RunSendEx),           C("__Menu_SendEx2", RunSendEx2),
};

// ---- input

short Player(void) {
    static const short kPlayers[] = {-1, -1, 0, 1, 2, 3, 4, 7, -2};
    return kPlayers[R(COUNT(kPlayers))];
}

int Action(void) {
    static const int kActions[] = {0x1e, 0x1f, 0x22, 0x1a, 0x1b, 0x20, 0x21};
    return R(3) ? kActions[R(COUNT(kActions))] : (int)R(40);
}

uint32_t RunGetAction(bool original) {
    uint32_t player = (uint32_t)(ushort)Player() | (R(2) ? R(0) << 16 : 0);
    uint32_t args[2] = {(uint32_t)Action(), R(4) ? (1u << R(4)) : R(0)};
    Regs r = {player, R(0), R(0), R(0), R(0), R(0)};
    return RegCall(original, 0x00072d70, (const void *)&Menu_GetAction, r, 2, args) & 0xffff;
}

uint32_t RunInputAction(bool original) {
    short player = Player();
    int action = Action();
    int flags = R(4) ? (int)(1u << R(4)) : (int)R(0);
    return CDECL_RUN(0x00072e20, Menu_InputAction, ushort (__cdecl *)(short, int, int), player, action, flags) & 0xffff;
}

uint32_t RunSetAction(bool original) {
    short player = Player();
    int action = Action();
    uchar value = (uchar)R(0x100);
    CDECL_VOID(0x00072ee0, Menu_SetAction, void (__cdecl *)(short, int, uchar), player, action, value);
    return 0;
}

uint32_t RunLastController(bool original) {
    return CDECL_RUN(0x00072f30, Menu_GetLastController, uint (__cdecl *)(void));
}

uint32_t RunNoInputCount(bool original) {
    return CDECL_RUN(0x00072f40, Menu_GetNoInputCount, uint (__cdecl *)(void));
}

uint32_t RunResetNoInput(bool original) {
    CDECL_VOID(0x00072f50, Menu_ResetNoInputCount, void (__cdecl *)(void));
    return 0;
}

const Case kInput[] = {
    C("Menu_GetAction", RunGetAction),           C("Menu_InputAction", RunInputAction),
    C("Menu_SetAction", RunSetAction),           C("Menu_GetLastController", RunLastController),
    C("Menu_GetNoInputCount", RunNoInputCount),  C("Menu_ResetNoInputCount", RunResetNoInput),
};

// ---- sprites, memory, the pulse and the sounds

uint32_t RunInitializeSprite(bool original) {
    sprite *s = R(20) ? &world.sprites[R(SPRITES)] : NULL;
    uchar pri = (uchar)R(0x100);
    short x = S(700), y = S(500), w = S(300), h = S(300);
    char *text = R(2) ? world.text[R(4)] : NULL;
    HASHCODE tex = (HASHCODE)(R(2) ? 0 : R(0));
    uint colour = R(3) ? R(0) : 0, colour2 = R(0);
    ushort u = R(3) ? (ushort)R(0x10000) : 0xffff, v = (ushort)R(0x10000), uw = (ushort)R(0x10000),
           vh = (ushort)R(0x10000), flags = (ushort)R(0x10000);
    CDECL_VOID(0x00072550, Menu_InitializeSprite,
               void (__cdecl *)(sprite *, uchar, short, short, short, short, char *, HASHCODE, uint, uint, ushort, ushort,
                                ushort, ushort, ushort),
               s, pri, x, y, w, h, text, tex, colour, colour2, u, v, uw, vh, flags);
    return 0;
}

uint32_t RunResize(bool original) {
    M_CONTROL *c = &AnyCtrl()->c;
    short x = S(700), y = S(500), w = S(300), h = S(300);
    CDECL_VOID(0x00072c30, Menu_ResizeControl, void (__cdecl *)(M_CONTROL *, short, short, short, short), c, x, y, w, h);
    return 0;
}

void ResizeFrom(M_CONTROL *c, M_CONTROL *from) {
    Menu_ResizeControl(c, from);
}

uint32_t RunResizeFrom(bool original) {
    M_CONTROL *c = &AnyCtrl()->c, *from = &AnyCtrl()->c;
    CDECL_VOID(0x00072c60, ResizeFrom, void (__cdecl *)(M_CONTROL *, M_CONTROL *), c, from);
    return 0;
}

uint32_t RunClipString(bool original) {
    sprite *s = R(20) ? &world.sprites[R(SPRITES)] : NULL;
    int width = R(4) ? (int)R(640) : (int)R(0);
    char *format = R(2) ? world.format : NULL;
    return CDECL_RUN(0x00072c90, Menu_ClipString, int (__cdecl *)(sprite *, int, char *), s, width, format);
}

uint32_t RunCreateSprite(bool original) {
    SpriteInfo *info = R(4) ? &world.infos[R(2)] : NULL;
    return W(CDECL_RUN(0x00072d10, Menu_CreateSprite, sprite *(__cdecl *)(SpriteInfo *), info));
}

uint32_t RunMalloc(bool original) {
    int size = R(4) ? (int)R(0x200) : (int)R(0);
    return W(CDECL_RUN(0x00072d40, Menu_Malloc, void *(__cdecl *)(int), size));
}

uint32_t RunFree(bool original) {
    world.slot = R(4) ? &world.heap[R(HEAP)] : NULL;
    CDECL_VOID(0x00072d60, Menu_Free, void (__cdecl *)(void **, undefined4), &world.slot, R(8));
    return 0;
}

uint32_t RunAlphaUpdate(bool original) {
    CDECL_VOID(0x00072fa0, Menu_AlphaUpdate, void (__cdecl *)(void));
    return 0;
}

uint32_t RunAlphaReset(bool original) {
    CDECL_VOID(0x00072ff0, Menu_AlphaReset, void (__cdecl *)(void));
    return 0;
}

uint32_t RunAlphaGet(bool original) {
    return CDECL_RUN(0x00073000, Menu_AlphaGet, short (__cdecl *)(void)) & 0xffff;
}

uint32_t RunEnableSounds(bool original) {
    static const uint32_t kPages[] = {0x40000002, 0x40000009, 0x40000015, 0x40000022, 0x40000033, 0x40000036,
                                      0x40000038, 0x40000037, 0x4000003a, 0x4000003b, 0x4000003c, 0x4000003d,
                                      0x4000003e, 0x40000005, 0x4000001c, 0x40000025, 0x4000004b, 0x40000001,
                                      0x40000003, 0x4000004c, 0x4000004d, 0x40000000, 0x10000002};
    HASHCODE page = (HASHCODE)(R(5) ? kPages[R(COUNT(kPages))] : R(0));
    CDECL_VOID(0x00073010, Menu_EnableSounds, void (__cdecl *)(HASHCODE), page);
    return 0;
}

uint32_t RunPlaySound(bool original) {
    uint sound = R(5) ? R(10) : R(0);
    CDECL_VOID(0x000730f0, Menu_PlaySound, void (__cdecl *)(uint), sound);
    return 0;
}

uint32_t RunUpdateTextFmt(bool original) {
    CDECL_VOID(0x000731d0, Menu_UpdateTextFmt, void (__cdecl *)(void));
    return 0;
}

const Case kSprites[] = {
    C("Menu_InitializeSprite", RunInitializeSprite), C("Menu_ResizeControl", RunResize),
    C("Menu_ResizeControl (from)", RunResizeFrom),   C("Menu_ClipString", RunClipString),
    C("Menu_CreateSprite", RunCreateSprite),         C("Menu_Malloc", RunMalloc),
    C("Menu_Free", RunFree),                         C("Menu_AlphaUpdate", RunAlphaUpdate),
    C("Menu_AlphaReset", RunAlphaReset),             C("Menu_AlphaGet", RunAlphaGet),
    C("Menu_EnableSounds", RunEnableSounds),         C("Menu_PlaySound", RunPlaySound),
    C("Menu_UpdateTextFmt", RunUpdateTextFmt),
};

// ---- scripts

// One of the controls that have scripts, or any
Ctrl *ScriptCtrl(void) {
    return R(4) ? &world.controls[R(8)] : AnyCtrl();
}

uint32_t RunPrepare(bool original) {
    M_CONTROL *c = R(20) ? &ScriptCtrl()->c : NULL;
    CDECL_VOID(0x00096960, Script_PrepareScripting, void (__cdecl *)(M_CONTROL *), c);
    return 0;
}

uint32_t RunAddScript(bool original) {
    M_CONTROL *c = R(20) ? &ScriptCtrl()->c : NULL;
    int id = (int)R(10);
    return W(CDECL_RUN(0x000969a0, Script_AddScript, M_SCRIPT *(__cdecl *)(M_CONTROL *, int), c, id));
}

uint32_t RunAddKeyFrame(bool original) {
    M_SCRIPT *s = R(20) ? &world.scripts[R(SCRIPTS)] : NULL;
    M_KEYFRAME *k = R(20) ? &world.keyArg : NULL;
    return W(CDECL_RUN(0x00096a10, Script_AddKeyFrame, M_KEYFRAME *(__cdecl *)(M_SCRIPT *, M_KEYFRAME *), s, k));
}

uint32_t RunAddMessage(bool original) {
    M_KEYFRAME *k = R(20) ? &world.keys[R(KEYS)] : NULL;
    M_MESSAGE *m = R(20) ? &world.msgArg : NULL;
    return W(CDECL_RUN(0x00096b10, Script_AddMessage, M_MESSAGE *(__cdecl *)(M_KEYFRAME *, M_MESSAGE *), k, m));
}

uint32_t RunSelect(bool original) {
    M_CONTROL *c = &ScriptCtrl()->c;
    int id = (int)R(10);
    return W(CDECL_RUN(0x00096b60, Script_Select, M_SCRIPT *(__cdecl *)(M_CONTROL *, int), c, id));
}

uint32_t RunPlay(bool original) {
    M_CONTROL *c = &ScriptCtrl()->c;
    int id = (int)R(10);
    char atEnd = (char)(R(2) ? R(0x100) : 0);
    // At the end of a script with no keyframes both would read through NULL
    for (M_SCRIPT *s = (M_SCRIPT *)LinkedList_First(&c->scripts); s != NULL; s = s->next) {
        if (s->id == id) {
            if (s->keys.tail == NULL)
                atEnd = 0;
            break;
        }
    }
    return CDECL_RUN(0x00096ba0, Script_Play, bool (__cdecl *)(M_CONTROL *, int, char), c, id, atEnd);
}

uint32_t RunPlayDefault(bool original) {
    M_CONTROL *c = R(20) ? &ScriptCtrl()->c : NULL;
    static const uint kMessages[] = {0x49, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x51, 0x5b, 0x63, 0x4a, 0x50, 0x52, 0x64, 0};
    uint msg = R(5) ? kMessages[R(COUNT(kMessages))] : R(0);
    return CDECL_RUN(0x00096c70, Script_PlayDefault, bool (__cdecl *)(M_CONTROL *, uint), c, msg);
}

uint32_t RunExitLoop(bool original) {
    M_CONTROL *c = R(20) ? &ScriptCtrl()->c : NULL;
    return CDECL_RUN(0x00096d50, Script_ExitLoop, bool (__cdecl *)(M_CONTROL *), c);
}

uint32_t RunDelKeyFrame(bool original) {
    int s = (int)R(SCRIPTS);
    M_SCRIPT *script = R(20) ? &world.scripts[s] : NULL;
    M_KEYFRAME *key = (M_KEYFRAME *)LinkedList_First(&world.scripts[s].keys);
    if (key == NULL || R(20) == 0)
        key = NULL;
    else if (R(2) && key->next != NULL)
        key = key->next;
    CDECL_VOID(0x00096e00, Script_DelKeyFrame, void (__cdecl *)(M_SCRIPT *, M_KEYFRAME *), script, key);
    return 0;
}

uint32_t RunDelScript(bool original) {
    Ctrl *k = ScriptCtrl();
    M_CONTROL *c = R(20) ? &k->c : NULL;
    M_SCRIPT *s = (M_SCRIPT *)LinkedList_First(&k->c.scripts);
    if (R(20) == 0)
        s = NULL;
    CDECL_VOID(0x00097310, Script_DelScript, void (__cdecl *)(M_CONTROL *, M_SCRIPT *), c, s);
    return 0;
}

uint32_t RunDelAllScripts(bool original) {
    M_CONTROL *c = R(20) ? &ScriptCtrl()->c : NULL;
    CDECL_VOID(0x00097560, Script_DelAllScripts, void (__cdecl *)(M_CONTROL *), c);
    return 0;
}

uint32_t RunRunFrame(bool original) {
    M_MANAGER *m = AnyManager();
    M_CONTROL *c = &world.controls[R(8)].c;
    if (c->key != NULL && c->script == NULL)
        c->key = NULL;
    CDECL_VOID(0x00097370, Script_RunFrame, void (__cdecl *)(M_MANAGER *, M_CONTROL *), m, c);
    return 0;
}

const Case kScripts[] = {
    C("Script_PrepareScripting", RunPrepare), C("Script_AddScript", RunAddScript),
    C("Script_AddKeyFrame", RunAddKeyFrame),  C("Script_AddMessage", RunAddMessage),
    C("Script_Select", RunSelect),            C("Script_Play", RunPlay),
    C("Script_PlayDefault", RunPlayDefault),  C("Script_ExitLoop", RunExitLoop),
    C("Script_DelKeyFrame", RunDelKeyFrame),  C("Script_DelScript", RunDelScript),
    C("Script_DelAllScripts", RunDelAllScripts), C("Script_RunFrame", RunRunFrame),
};

// ---- interpolation

uint32_t RunCalculateSpline(bool original) {
    int p[4];
    for (int i = 0; i < 4; i++)
        p[i] = R(5) ? S(700) : (int)R(0);
    float t = R(4) ? RF(0.0f, 1.0f) : Num(-3.0f, 3.0f);
    double r = CDECL_RUN(0x00096d80, Script_CalculateSpline, double (__cdecl *)(int, int, int, int, float), p[0], p[1],
                         p[2], p[3], t);
    if (r != r)
        return 0x7ff80000u;
    uint64_t bits;
    memcpy(&bits, &r, 8);
    return (uint32_t)bits ^ (uint32_t)(bits >> 32);
}

M_SCRIPT *KeyedScript(void) {
    for (int tries = 0; tries < 20; tries++) {
        M_SCRIPT *s = &world.scripts[R(SCRIPTS)];
        if (s->keys.count > 0 && LinkedList_First(&s->keys) != NULL)
            return s;
    }
    return &world.scripts[0];
}

float FrameT(M_SCRIPT *s) {
    return R(4) ? RF(-0.5f, (float)s->keys.count + 0.5f) : Num(-3.0f, 9.0f);
}

uint32_t RunInterpolateSpline(bool original) {
    M_CONTROL *c = &AnyCtrl()->c;
    M_SCRIPT *s = KeyedScript();
    float t = FrameT(s);
    CDECL_VOID(0x00096e80, Script_InterpolateSpline, void (__cdecl *)(M_CONTROL *, M_SCRIPT *, float), c, s, t);
    return 0;
}

uint32_t RunInterpolateLine(bool original) {
    M_CONTROL *c = &AnyCtrl()->c;
    M_SCRIPT *s = KeyedScript();
    float t = FrameT(s);
    CDECL_VOID(0x000970d0, Script_InterpolateLine, void (__cdecl *)(M_CONTROL *, M_SCRIPT *, float), c, s, t);
    return 0;
}

uint32_t RunGetIndex(bool original) {
    LINKEDLIST *list;
    if (R(3)) {
        list = &world.scripts[R(SCRIPTS)].keys;
    } else {
        // Links a word in, the list's offset saying so
        list = &world.list;
        int n = (int)R(6);
        for (int i = 0; i < 32; i++)
            world.linkArea[i] = NULL;
        list->offset = 1;
        list->count = R(4) ? n : R(8);
        list->head = n ? &world.linkArea[1] : NULL;
        for (int i = 0; i < n; i++)
            world.linkArea[2 + 2 * i] = i + 1 < n ? &world.linkArea[1 + 2 * (i + 1)] : NULL;
        list->tail = NULL;
    }
    if (R(20) == 0)
        list = NULL;
    uint index = R(4) ? R(7) : R(0);
    return W(CDECL_RUN(0x00090bc0, LinkedList_GetIndex, void *(__cdecl *)(LINKEDLIST *, uint), list, index));
}

const Case kInterpolation[] = {
    C("Script_CalculateSpline", RunCalculateSpline),     C("Script_InterpolateSpline", RunInterpolateSpline),
    C("Script_InterpolateLine", RunInterpolateLine),     C("LinkedList_GetIndex", RunGetIndex),
};

void Group(const char *name, const Case *list, int count, int trials) {
    const char *only = getenv("NIGHTFIRE_MENUPORT_GROUP");
    if (only != NULL && strstr(name, only) == NULL)
        return;
    ShadowHook_RemoveAll();
    InstallFakes();
    Test(name, list, count, trials);
}

} // namespace

void MenuPortShadow_Run(void) {
    struct Saved {
        void *at;
        size_t size;
        std::vector<uint8_t> bytes;
    };
    static const struct {
        uint32_t at;
        size_t size;
    } kGlobals[] = {
        {XbeOurs(0x0025f1d0), sizeof(M_MANAGER) * 5}, // manager
        {XbeOurs(0x00223a90), 0x10},                  // the component list
        {0x00223aa0, 8},                     // the frame and the last action
        {0x00223b2c, 0x14},                  // the pulse, the sounds, the text's throb
        {XbeOurs(0x00224558), 0x94},                  // Menu_GetFirstControl's stand-in
        {XbeOurs(0x0017c540), 4},                     // the last controller
        {0x001fe6d0, 0x158 * 4},             // PlayerInputs
    };
    std::vector<Saved> saved;
    for (const auto &g : kGlobals) {
        Saved s;
        s.at = (void *)(uintptr_t)g.at;
        s.size = g.size;
        s.bytes.resize(s.size);
        XbeOursRead(s.bytes.data(), (unsigned)(uintptr_t)s.at, s.size);
        saved.push_back(s);
    }

    regions.Clear();
    areaCount = 0;
    AddArea(&world, sizeof(world));
    for (const auto &g : kGlobals)
        AddArea((void *)(uintptr_t)g.at, g.size);
    NfWarnMuted++;
    cases = differ = nanChoices = 0;

    Group("skins", kSkins, COUNT(kSkins), 6000);
    Group("finding controls", kFinding, COUNT(kFinding), 12000);
    Group("dispatch", kDispatch, COUNT(kDispatch), 8000);
    Group("input", kInput, COUNT(kInput), 8000);
    Group("sprites, memory, pulse and sounds", kSprites, COUNT(kSprites), 5000);
    Group("scripts", kScripts, COUNT(kScripts), 8000);
    Group("interpolation", kInterpolation, COUNT(kInterpolation), 12000);
    ShadowHook_RemoveAll();
    if (nanChoices != 0)
        printf("[menuport] %d cases matched with NaNs the x87 and SSE2 chose differently\n", nanChoices);

    NfWarnMuted--;
    for (auto &s : saved)
        XbeOursWrite((unsigned)(uintptr_t)s.at, s.bytes.data(), s.size);
}
