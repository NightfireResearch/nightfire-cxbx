#include "X87Count.h"

#if defined(_M_IX86) || defined(__i386__)   // the x87 functions and their callers' rel32 calls: 32-bit only
#include "CodeRetarget.h"

#include "../game/Simulation.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_X87COUNT=1 (docs/driving/64-bit.md 4.1, package 0) counts the calls of each of the x87 assembly
// functions, to measure how much of the game's arithmetic goes through them, and keeps the arguments of the first
// four calls and of the first four after the player's car appears. The counts go to stdout (so to drive.log) when
// the car appears and 1000, 4000 and 8900 ticks later, and with the samples to NIGHTFIRE_X87COUNT_FILE
// (x87count.txt) every 1000 ticks.
//
// The functions are static in their files, so they are found by name and object in the DLL's link map (the .map
// beside it; /MAP). Every call to one in our code, and every stored address of one, is moved to a stub of its own
// (CodeRetarget.cpp), which pushes the stub's number and goes to X87Common: that saves the flags, the integer
// registers, XMM0-7 and the x87 environment, reads ST0 (without popping it; the environment is put back, so the
// status word's flags are untouched), counts, and returns into the real function with the stack as the caller
// left it. The calls the x87 functions make to each other (CrtAcos to Crt::CIacos) are counted too.
// ---------------------------------------------------------------------------------------------------------------

namespace {

struct Function {
    const char *name;     // the source's name
    const char *object;   // the object file it is in
};

const Function kFunctions[] = {
    {"X87Sin", "IK.obj"}, {"X87Cos", "IK.obj"}, {"X87Atan2", "IK.obj"}, {"CrtAcos", "IK.obj"},
    {"CrtPow", "Weapon.obj"}, {"LogX87", "SoundManager.obj"}, {"AddSine", "Camera.obj"},
    {"CrtAcosTimes", "CameraSpline.obj"}, {"CrtAsinTimes", "CameraSpline.obj"},
    {"SinTurnsTimes", "PlayerCameraB.obj"}, {"CosTurnsTimes", "PlayerCameraB.obj"},
    {"TanTurnsTimes", "PlayerCameraB.obj"}, {"DivideByTanTurns", "PlayerCameraB.obj"},
    {"EAGL_BuildRotate", "Transform.obj"}, {"X87Sin", "Transform.obj"}, {"X87Cos", "Transform.obj"},
    {"X87Atan2", "Transform.obj"}, {"PerspectivePlanes", "View.obj"}, {"EulerToQuatX87", "AnimMisc.obj"},
    {"LogX87", "PhysicsUtil.obj"}, {"GlowWave", "EmpEffect.obj"}, {"CrtAcos", "EventsA.obj"},
    {"NegativeSine", "Helicopter.obj"}, {"X87Atan2", "HudCrossHairs.obj"}, {"CrtPow", "VehicleRender.obj"},
    {"VehicleLogX87", "VehicleSound.obj"}, {"ScaledSine", "WeaponCameras.obj"},
    {"ScaledCosine", "WeaponCameras.obj"}, {"X87NaN", "CRuntime.obj"}, {"CIacos", "CRuntime.obj"},
    {"CIasin", "CRuntime.obj"}, {"CIpow", "CRuntime.obj"}, {"_fpmath", "CRuntime.obj"},
    {"_clearfp", "CRuntime.obj"}, {"SinCosFloat", "D3DXMath.obj"}, {"sin_fractionalangle", "RealMath.obj"},
    {"cos_fractionalangle", "RealMath.obj"}, {"sincos_fractionalangle", "RealMath.obj"},
    {"tan_fractionalangle", "RealMath.obj"}, {"atan_turns", "RealMath.obj"}, {"SinTimes", "RealMath.obj"},
    {"REAL_sqrtf", "RealPrint.obj"}, {"Tangent", "Targeting.obj"},
};
const int kCount = sizeof(kFunctions) / sizeof(kFunctions[0]);
static_assert(kCount == 43, "the 43 x87 functions of docs/driving/64-bit.md 4.1");

const int kSamples = 4;

struct Sample {
    uint32_t stack[4];    // the first four argument words
    uint32_t st0[2];      // ST0 on entry (the C runtime's _CI functions take their arguments there)
    int tick;             // ticks since the car appeared, -1 before
};

struct Counter {
    volatile LONG calls;
    LONG callsAtCar;      // calls when the car appeared
    Sample first[kSamples];
    Sample afterCar[kSamples];
};

// The calls by call site: the return address and the function called (open addressing; a full table drops sites)
struct CallSite {
    volatile LONG returnAddress;
    int function;
    volatile LONG calls;
};
const int kSites = 4096;
CallSite g_sites[kSites];

// The link map's functions, for naming the call sites
struct MapSymbol {
    uintptr_t address;
    char name[60];
};
MapSymbol *g_symbols;
int g_symbolCount;

RetargetTarget g_targets[kCount];
Counter g_counters[kCount];
volatile int g_tick = -1;     // ticks since the player's car appeared
volatile bool g_carSeen;
bool g_installed;
char g_file[MAX_PATH] = "x87count.txt";

}  // namespace

// The stubs' common part: frame[0] is the stub's number, frame[1] the caller's return address, frame[2...] the
// arguments; frame[0] becomes the real function's address, which X87Common returns into.
extern "C" void __cdecl X87Record(uint32_t *frame, const uint32_t *st0) {
    int n = (int)frame[0];
    Counter *c = &g_counters[n];
    LONG call = InterlockedIncrement(&c->calls);
    Sample *s = NULL;
    if (call <= kSamples)
        s = &c->first[call - 1];
    else if (g_carSeen && call - c->callsAtCar >= 1 && call - c->callsAtCar <= kSamples)
        s = &c->afterCar[call - c->callsAtCar - 1];
    if (s != NULL) {
        for (int i = 0; i < 4; i++)
            s->stack[i] = frame[2 + i];
        s->st0[0] = st0[0];
        s->st0[1] = st0[1];
        s->tick = g_tick;
    }
    LONG at = (LONG)frame[1];
    for (unsigned i = ((uint32_t)at >> 2) % kSites, probes = 0; probes < kSites; i = (i + 1) % kSites, probes++) {
        LONG seen = InterlockedCompareExchange(&g_sites[i].returnAddress, at, 0);
        if (seen == 0)
            g_sites[i].function = n;
        if (seen == 0 || seen == at) {
            InterlockedIncrement(&g_sites[i].calls);
            break;
        }
    }
    frame[0] = (uint32_t)g_targets[n].from;
}

extern "C" __declspec(naked) void X87Common(void) {
    __asm {
        pushfd
        pushad
        sub esp, 164                      // +0 the x87 environment, +28 ST0, +36 XMM0-7
        fnstenv [esp]
        fst qword ptr [esp + 28]
        fldenv [esp]
        movdqu [esp + 36], xmm0
        movdqu [esp + 52], xmm1
        movdqu [esp + 68], xmm2
        movdqu [esp + 84], xmm3
        movdqu [esp + 100], xmm4
        movdqu [esp + 116], xmm5
        movdqu [esp + 132], xmm6
        movdqu [esp + 148], xmm7
        lea eax, [esp + 28]
        push eax
        lea eax, [esp + 4 + 164 + 32 + 4]   // the stub's number, pushed before the flags
        push eax
        call X87Record
        add esp, 8
        movdqu xmm0, [esp + 36]
        movdqu xmm1, [esp + 52]
        movdqu xmm2, [esp + 68]
        movdqu xmm3, [esp + 84]
        movdqu xmm4, [esp + 100]
        movdqu xmm5, [esp + 116]
        movdqu xmm6, [esp + 132]
        movdqu xmm7, [esp + 148]
        add esp, 164
        popad
        popfd
        ret                               // into the real function, the caller's return address on top
    }
}

// One stub per function, numbered as kFunctions is (the number is the stub's push)
#define X87_STUB(n) extern "C" __declspec(naked) void X87Stub##n(void) { __asm push n __asm jmp X87Common }
X87_STUB(0) X87_STUB(1) X87_STUB(2) X87_STUB(3) X87_STUB(4) X87_STUB(5) X87_STUB(6) X87_STUB(7)
X87_STUB(8) X87_STUB(9) X87_STUB(10) X87_STUB(11) X87_STUB(12) X87_STUB(13) X87_STUB(14) X87_STUB(15)
X87_STUB(16) X87_STUB(17) X87_STUB(18) X87_STUB(19) X87_STUB(20) X87_STUB(21) X87_STUB(22) X87_STUB(23)
X87_STUB(24) X87_STUB(25) X87_STUB(26) X87_STUB(27) X87_STUB(28) X87_STUB(29) X87_STUB(30) X87_STUB(31)
X87_STUB(32) X87_STUB(33) X87_STUB(34) X87_STUB(35) X87_STUB(36) X87_STUB(37) X87_STUB(38) X87_STUB(39)
X87_STUB(40) X87_STUB(41) X87_STUB(42) X87_STUB(43) X87_STUB(44) X87_STUB(45) X87_STUB(46) X87_STUB(47)
#undef X87_STUB

namespace {

void (*const kStubs[48])(void) = {
    X87Stub0, X87Stub1, X87Stub2, X87Stub3, X87Stub4, X87Stub5, X87Stub6, X87Stub7,
    X87Stub8, X87Stub9, X87Stub10, X87Stub11, X87Stub12, X87Stub13, X87Stub14, X87Stub15,
    X87Stub16, X87Stub17, X87Stub18, X87Stub19, X87Stub20, X87Stub21, X87Stub22, X87Stub23,
    X87Stub24, X87Stub25, X87Stub26, X87Stub27, X87Stub28, X87Stub29, X87Stub30, X87Stub31,
    X87Stub32, X87Stub33, X87Stub34, X87Stub35, X87Stub36, X87Stub37, X87Stub38, X87Stub39,
    X87Stub40, X87Stub41, X87Stub42, X87Stub43, X87Stub44, X87Stub45, X87Stub46, X87Stub47,
};
static_assert(kCount <= 48, "a stub per function");

bool PlayerCarExists() {
    PVehicle **cars = fgSimulation.cars.first;
    return cars != NULL && *cars != NULL;
}

// The functions' addresses from the link map: "<section:offset> <decorated name> <address> [f] [i] <object>",
// a function's decorated name starting "?<name>@" (a namespace or the anonymous one may follow).
int FindFunctions(HMODULE module) {
    char path[MAX_PATH];
    DWORD length = GetModuleFileNameA(module, path, sizeof(path));
    if (length < 4 || length >= sizeof(path))
        return 0;
    strcpy(path + length - 4, ".map");
    FILE *map = fopen(path, "r");
    if (map == NULL) {
        printf("[x87count] no link map at %s - not counting\n", path);
        return 0;
    }
    uintptr_t preferred = 0, base = (uintptr_t)module;
    int found = 0;
    char line[1024];
    while (fgets(line, sizeof(line), map) != NULL) {
        unsigned value;
        if (sscanf(line, " Preferred load address is %x", &value) == 1)
            preferred = value;
        char where[32], name[512], address[32];
        if (sscanf(line, " %31s %511s %31s", where, name, address) != 3 || strchr(where, ':') == NULL ||
            name[0] != '?')
            continue;
        char *object = strrchr(line, ' ');
        if (object == NULL)
            continue;
        object++;
        object[strcspn(object, "\r\n")] = 0;
        const char *colon = strrchr(object, ':');   // "lib:object"
        if (colon != NULL)
            object = (char *)colon + 1;
        if (strstr(line, " f ") != NULL) {
            if (g_symbolCount % 4096 == 0)
                g_symbols = (MapSymbol *)realloc(g_symbols, (g_symbolCount + 4096) * sizeof(MapSymbol));
            MapSymbol *symbol = &g_symbols[g_symbolCount++];
            symbol->address = (uintptr_t)strtoul(address, NULL, 16) - preferred + base;
            snprintf(symbol->name, sizeof(symbol->name), "%s", name);
        }
        for (int i = 0; i < kCount; i++) {
            size_t n = strlen(kFunctions[i].name);
            if (strncmp(name + 1, kFunctions[i].name, n) != 0 || name[1 + n] != '@' ||
                _stricmp(object, kFunctions[i].object) != 0 || g_targets[i].from != 0)
                continue;
            g_targets[i].from = (uintptr_t)strtoul(address, NULL, 16) - preferred + base;
            found++;
        }
    }
    fclose(map);
    return found;
}

void Report(FILE *out, bool samples) {
    LONG total = 0, totalCar = 0;
    for (int i = 0; i < kCount; i++) {
        total += g_counters[i].calls;
        totalCar += g_counters[i].calls - g_counters[i].callsAtCar;
    }
    int ticks = g_tick;
    fprintf(out, "[x87count] tick %d: %ld calls, %ld since the car appeared", ticks, total, totalCar);
    if (ticks > 0)
        fprintf(out, " (%.1f per tick)", (double)totalCar / ticks);
    fprintf(out, "\n");
    for (int i = 0; i < kCount; i++) {
        const Counter *c = &g_counters[i];
        if (g_targets[i].sites == 0)
            continue;
        LONG sinceCar = c->calls - c->callsAtCar;
        fprintf(out, "[x87count]   %-24s %-18s %9ld before the car %9ld after", kFunctions[i].name,
                kFunctions[i].object, c->callsAtCar, sinceCar);
        if (ticks > 0)
            fprintf(out, " (%.2f per tick)", (double)sinceCar / ticks);
        fprintf(out, "\n");
        if (!samples)
            continue;
        for (int set = 0; set < 2; set++) {
            const Sample *s = set == 0 ? c->first : c->afterCar;
            for (int k = 0; k < kSamples; k++) {
                if (s[k].stack[0] == 0 && s[k].stack[1] == 0 && s[k].st0[0] == 0 && s[k].st0[1] == 0 && s[k].tick == 0)
                    continue;
                float f[4];
                memcpy(f, s[k].stack, sizeof(f));
                double d0, st;
                memcpy(&d0, s[k].stack, 8);
                memcpy(&st, s[k].st0, 8);
                fprintf(out, "      %s tick %d: words %08x %08x %08x %08x (as floats %g %g %g %g, words 0-1 as a "
                        "double %.17g) ST0 %.17g\n", set == 0 ? "first" : "after", s[k].tick, s[k].stack[0],
                        s[k].stack[1], s[k].stack[2], s[k].stack[3], f[0], f[1], f[2], f[3], d0, st);
            }
        }
    }
}

// The function a return address is in: the last map symbol at or below it (the map lists them in address order)
const char *Caller(uintptr_t address) {
    int low = 0, high = g_symbolCount - 1, found = -1;
    while (low <= high) {
        int middle = (low + high) / 2;
        if (g_symbols[middle].address <= address) {
            found = middle;
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return found >= 0 ? g_symbols[found].name : "?";
}

// The call sites by calls, the busiest first: "<calls> <function called> from <caller>+<offset>"
void ReportSites(FILE *out, int limit) {
    static int order[kSites];
    int count = 0;
    for (int i = 0; i < kSites; i++)
        if (g_sites[i].returnAddress != 0)
            order[count++] = i;
    for (int i = 1; i < count; i++)
        for (int j = i; j > 0 && g_sites[order[j]].calls > g_sites[order[j - 1]].calls; j--) {
            int swap = order[j];
            order[j] = order[j - 1];
            order[j - 1] = swap;
        }
    fprintf(out, "[x87count] %d call sites, the busiest:\n", count);
    for (int i = 0; i < count && i < limit; i++) {
        const CallSite *site = &g_sites[order[i]];
        fprintf(out, "[x87count]   %10ld %-22s from %s\n", site->calls, kFunctions[site->function].name,
                Caller((uint32_t)site->returnAddress));
    }
}

void WriteFile() {
    FILE *out = fopen(g_file, "w");
    if (out == NULL)
        return;
    Report(out, true);
    ReportSites(out, kSites);
    fclose(out);
}

}  // namespace

void X87Count_Install(void) {
    char text[16];
    if (GetEnvironmentVariableA("NIGHTFIRE_X87COUNT", text, sizeof(text)) == 0 || text[0] == '0')
        return;
    GetEnvironmentVariableA("NIGHTFIRE_X87COUNT_FILE", g_file, sizeof(g_file));
    HMODULE module = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&X87Count_Install, &module))
        return;
    int found = FindFunctions(module);
    if (found == 0)
        return;
    // A function the map does not name was folded into an identical one by the linker (its calls count there)
    for (int i = 0; i < kCount; i++)
        g_targets[i].to = (uintptr_t)kStubs[i];
    RetargetTarget targets[kCount];
    int slot[kCount], used = 0;
    for (int i = 0; i < kCount; i++) {
        bool folded = g_targets[i].from == 0;
        for (int j = 0; j < i && !folded; j++)
            folded = g_targets[j].from == g_targets[i].from;
        if (folded) {
            printf("[x87count]   %s (%s) is one function with another in this build: its calls count there\n",
                   kFunctions[i].name, kFunctions[i].object);
            continue;
        }
        targets[used] = g_targets[i];
        slot[used++] = i;
    }
    int moved = Retarget(targets, used, NULL, 0);
    for (int k = 0; k < used; k++)
        g_targets[slot[k]].sites = targets[k].sites;
    g_installed = true;
    printf("[x87count] %d references to %d distinct x87 functions moved to counting stubs\n", moved, used);
}

void X87Count_Tick(void) {
    if (!g_installed)
        return;
    if (!g_carSeen) {
        if (!PlayerCarExists())
            return;
        for (int i = 0; i < kCount; i++)
            g_counters[i].callsAtCar = g_counters[i].calls;
        g_carSeen = true;
        g_tick = 0;
    } else {
        g_tick = g_tick + 1;
    }
    if (g_tick == 0 || g_tick == 1000 || g_tick == 4000 || g_tick == 8900) {
        Report(stdout, false);
        if (g_tick == 8900)
            ReportSites(stdout, 60);
        fflush(stdout);
    }
    if (g_tick % 1000 == 0)
        WriteFile();
}
#else   // a 64-bit build has no x87 functions to count

void X87Count_Install(void) {
}

void X87Count_Tick(void) {
}

#endif
