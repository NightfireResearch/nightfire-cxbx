// Shadow test for Pickup_Update (game/obj/Pickup.cpp), GT_Track (game/obj/GT.cpp) and HUD_RadarUpdate
// (ui/HUD.cpp), on live play: with MPObjectsShadow=on, every call runs the original and ours from the same state
// and compares everything either can write. The game carries on with ours. A summary per function goes to the
// log every 500 calls, and the first 20 differences of each in full.
//
// - Pickup_Update: the pickup object, its PICKUPINFO and the RNG words. Its three callees that reach further
//   (Pickup_Handler gives the pickup away; MP_RegisterPickup and MP_UnregisterPickup change MPpickups and the AI
//   network's emitters) are caught by a jump over their entries for the length of both runs: each call is
//   recorded, not made, the two records are compared, and ours are then made for real, in order. They run after
//   the rest of our Pickup_Update rather than at their place in it, which nothing in it reads back.
// - GT_Track: the GUNTURRET, the RNG words and the returned target. Its sorted target list is scratch (the
//   original's at 0x25fc20, ours a static) and is not compared.
// - HUD_RadarUpdate: the radar sprite, every one of the pane's extra sprites (blips and name tags), the two
//   assassination markers and the RNG words. It is reached through the pane's update pointer, which points at
//   ours, so the HUD_Update entry is redirected to a wrapper that points the radar pane at the shadow first.

#include "MPObjectsShadow.h"

#include "../../common/xbeOriginal.h"
#include "../game/obj/Pickup.h"
#include "../game/obj/GT.h"
#include "../game/mp/multiplayer.h"
#include "../ui/HUD.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace {

const unsigned kPickupUpdate = 0x000a84d0;
const unsigned kGTTrack = 0x00099da0;
const unsigned kRadarUpdate = 0x000b54d0;
const unsigned kHUDUpdate = 0x000b6190;

const unsigned kPickupHandler = 0x000a7710;
const unsigned kRegisterPickup = 0x000a1310;
const unsigned kUnregisterPickup = 0x0009de20;

const size_t kObjSize = 0xe4;
void *const kRandWords = (void *)0x0018cdf8;   // U32_AT(0x0018cdf8) and U32_AT(0x0018cdfc)

const int kMaxRegions = 40;
const size_t kMaxBytes = 0x1000;

struct Region {
    const char *name;
    int index;
    unsigned char *at;
    size_t size;
};

struct Regions {
    Region region[kMaxRegions];
    int count;
    size_t total;

    void Add(const char *name, int index, void *at, size_t size) {
        if (at == NULL || count == kMaxRegions || total + size > kMaxBytes)
            return;
        for (int i = 0; i < count; i++) {
            if (region[i].at == at)
                return;
        }
        region[count++] = { name, index, (unsigned char *)at, size };
        total += size;
    }
    void Save(unsigned char *to) const {
        for (int i = 0; i < count; i++) {
            memcpy(to, region[i].at, region[i].size);
            to += region[i].size;
        }
    }
    void Load(const unsigned char *from) const {
        for (int i = 0; i < count; i++) {
            memcpy(region[i].at, from, region[i].size);
            from += region[i].size;
        }
    }
};

struct Counter {
    const char *name;
    unsigned calls, differing, reported;
};

Counter s_pickup = { "Pickup_Update" }, s_track = { "GT_Track" }, s_radar = { "HUD_RadarUpdate" };

// Compares the regions as they are now (ours) with the original's bytes
bool CompareRegions(Counter &c, const Regions &r, const unsigned char *original) {
    bool differs = false;
    const unsigned char *a = original;
    for (int i = 0; i < r.count; i++) {
        const unsigned char *b = r.region[i].at;
        for (size_t at = 0; at < r.region[i].size; at++) {
            if (a[at] == b[at])
                continue;
            differs = true;
            if (c.reported < 20) {
                c.reported++;
                printf("[mpobjshadow] %s call %u: %s[%d] +0x%03x original %02x ours %02x\n", c.name, c.calls,
                       r.region[i].name, r.region[i].index, (unsigned)at, a[at], b[at]);
            }
        }
        a += r.region[i].size;
    }
    return differs;
}

void Finish(Counter &c, bool differs) {
    if (differs)
        c.differing++;
    if (++c.calls % 500 == 0)
        printf("[mpobjshadow] %s: %u calls, %u differ\n", c.name, c.calls, c.differing);
}

// Pickup_Update's far-reaching callees, recorded instead of made
enum CallKind { CALL_HANDLER = 1, CALL_REGISTER, CALL_UNREGISTER };

struct DeferredCall {
    int kind;
    void *a, *b, *c;
};

struct CallLog {
    int count;
    DeferredCall call[8];
};

CallLog *s_log;

struct CodeHook {
    unsigned at;
    unsigned char saved[5];
};

void Hook(CodeHook &h, const void *to) {
    DWORD old;
    VirtualProtect((void *)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy(h.saved, (void *)h.at, 5);
    unsigned char jmp[5] = { 0xe9 };
    unsigned relative = (unsigned)to - (h.at + 5);
    memcpy(jmp + 1, &relative, 4);
    memcpy((void *)h.at, jmp, 5);
    VirtualProtect((void *)h.at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)h.at, 5);
}

void Unhook(CodeHook &h) {
    DWORD old;
    VirtualProtect((void *)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)h.at, h.saved, 5);
    VirtualProtect((void *)h.at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)h.at, 5);
}

CodeHook s_handlerHook = { kPickupHandler }, s_registerHook = { kRegisterPickup },
         s_unregisterHook = { kUnregisterPickup };

} // namespace

// Outside the namespace: the naked stubs below call these by name.
void __cdecl MPObjectsShadow_Record(int kind, void *a, void *b, void *c) {
    if (s_log == NULL || s_log->count == 8)
        return;
    s_log->call[s_log->count++] = { kind, a, b, c };
}

void __cdecl MPObjectsShadow_RecordHandler(obj_tag *obj, PICKUPINFO *pickup, obj_tag *picker) {
    MPObjectsShadow_Record(CALL_HANDLER, obj, pickup, picker);
}

// Stands in for Pickup_Handler: the PICKUPINFO in EDI, the object and the picker on the stack (caller pops)
__declspec(naked) void MPObjectsShadow_HandlerStub(void) {
    __asm {
        push dword ptr [esp + 8]    // picker
        push edi                    // pickup
        push dword ptr [esp + 12]   // obj
        call MPObjectsShadow_RecordHandler
        add esp, 12
        ret
    }
}

// Pickup_Handler for real: the PICKUPINFO in EDI
__declspec(naked) void __cdecl MPObjectsShadow_CallHandler(obj_tag *obj, PICKUPINFO *pickup, obj_tag *picker) {
    __asm {
        push edi
        mov edi, [esp + 12]         // pickup
        push dword ptr [esp + 16]   // picker
        push dword ptr [esp + 12]   // obj
        mov eax, 0x000a7710
        call eax
        add esp, 8
        pop edi
        ret
    }
}

namespace {

int __cdecl RegisterStub(obj_tag *obj) {
    MPObjectsShadow_Record(CALL_REGISTER, obj, NULL, NULL);
    return -1;  // Pickup_Update does not read it
}

void __cdecl UnregisterStub(obj_tag *obj) {
    MPObjectsShadow_Record(CALL_UNREGISTER, obj, NULL, NULL);
}

bool CompareCalls(Counter &c, const CallLog &original, const CallLog &ours) {
    bool same = original.count == ours.count;
    for (int i = 0; same && i < ours.count; i++) {
        const DeferredCall &a = original.call[i], &b = ours.call[i];
        same = a.kind == b.kind && a.a == b.a && a.b == b.b && a.c == b.c;
    }
    if (!same && c.reported < 20) {
        c.reported++;
        printf("[mpobjshadow] %s call %u: callees differ:", c.name, c.calls);
        for (int i = 0; i < original.count; i++)
            printf(" original %d(%p %p %p)", original.call[i].kind, original.call[i].a, original.call[i].b,
                   original.call[i].c);
        for (int i = 0; i < ours.count; i++)
            printf(" ours %d(%p %p %p)", ours.call[i].kind, ours.call[i].a, ours.call[i].b, ours.call[i].c);
        printf("\n");
    }
    return !same;
}

void MakeCalls(const CallLog &log) {
    for (int i = 0; i < log.count; i++) {
        const DeferredCall &call = log.call[i];
        switch (call.kind) {
        case CALL_HANDLER:
            MPObjectsShadow_CallHandler((obj_tag *)call.a, (PICKUPINFO *)call.b, (obj_tag *)call.c);
            break;
        case CALL_REGISTER:
            reinterpret_cast<int (__cdecl *)(obj_tag *)>(kRegisterPickup)((obj_tag *)call.a);
            break;
        case CALL_UNREGISTER:
            reinterpret_cast<void (__cdecl *)(obj_tag *)>(kUnregisterPickup)((obj_tag *)call.a);
            break;
        }
    }
}

unsigned char s_before[kMaxBytes], s_original[kMaxBytes];

void __cdecl ShadowPickupUpdate(obj_tag *obj) {
    if (obj == NULL) {
        Pickup_Update(obj);
        return;
    }
    Regions r = {};
    r.Add("obj", 0, obj, kObjSize);
    r.Add("pickup", 0, obj->extraObjectData, sizeof(PICKUPINFO));
    r.Add("rand", 0, kRandWords, 8);
    CallLog originalCalls = {}, ourCalls = {};

    r.Save(s_before);
    Hook(s_handlerHook, (const void *)&MPObjectsShadow_HandlerStub);
    Hook(s_registerHook, (const void *)&RegisterStub);
    Hook(s_unregisterHook, (const void *)&UnregisterStub);
    s_log = &originalCalls;
    {
        XbeOriginalScope scope(kPickupUpdate);
        reinterpret_cast<void (__cdecl *)(obj_tag *)>(kPickupUpdate)(obj);
    }
    r.Save(s_original);
    r.Load(s_before);
    s_log = &ourCalls;
    Pickup_Update(obj);
    s_log = NULL;
    Unhook(s_unregisterHook);
    Unhook(s_registerHook);
    Unhook(s_handlerHook);

    bool differs = CompareRegions(s_pickup, r, s_original);
    differs |= CompareCalls(s_pickup, originalCalls, ourCalls);
    Finish(s_pickup, differs);
    MakeCalls(ourCalls);
}

obj_tag *__cdecl ShadowGTTrack(GUNTURRET *gun, obj_tag *obj) {
    Regions r = {};
    r.Add("gun", 0, gun, sizeof(GUNTURRET));
    r.Add("rand", 0, kRandWords, 8);

    r.Save(s_before);
    obj_tag *original;
    {
        XbeOriginalScope scope(kGTTrack);
        original = reinterpret_cast<obj_tag *(__cdecl *)(GUNTURRET *, obj_tag *)>(kGTTrack)(gun, obj);
    }
    r.Save(s_original);
    r.Load(s_before);
    obj_tag *ours = GT_Track(gun, obj);

    bool differs = CompareRegions(s_track, r, s_original);
    if (original != ours) {
        differs = true;
        if (s_track.reported < 20) {
            s_track.reported++;
            printf("[mpobjshadow] GT_Track call %u: returned original %p ours %p\n", s_track.calls, original, ours);
        }
    }
    Finish(s_track, differs);
    return ours;
}

void __cdecl ShadowRadarUpdate(BLData *blData, HUDPANE_tag *pane, obj_tag *obj) {
    Regions r = {};
    if (pane->spriteList != NULL)
        r.Add("radar", 0, pane->spriteList[0], sizeof(sprite));
    sprite **items = (sprite **)pane->extraItems;
    for (int i = 0; items != NULL && i < pane->base->numExtraItems; i++)
        r.Add("item", i, items[i], sizeof(sprite));
    r.Add("marker", 0, MPGame.radar_related[blData->playerNum * 2], sizeof(sprite));
    r.Add("marker", 1, MPGame.radar_related[blData->playerNum * 2 + 1], sizeof(sprite));
    r.Add("rand", 0, kRandWords, 8);

    r.Save(s_before);
    {
        XbeOriginalScope scope(kRadarUpdate);
        reinterpret_cast<void (__cdecl *)(BLData *, HUDPANE_tag *, obj_tag *)>(kRadarUpdate)(blData, pane, obj);
    }
    r.Save(s_original);
    r.Load(s_before);
    HUD_RadarUpdate(blData, pane, obj);

    Finish(s_radar, CompareRegions(s_radar, r, s_original));
}

void __cdecl ShadowHUDUpdate(BLData *blData, obj_tag *obj) {
    if (blData != NULL && blData->hudInfo != NULL) {
        for (int i = 0; i < NUM_PANES; i++) {
            if (blData->hudInfo->pane[i].updateFunction == &HUD_RadarUpdate)
                blData->hudInfo->pane[i].updateFunction = &ShadowRadarUpdate;
        }
    }
    HUD_Update(blData, obj);
}

} // namespace

void MPObjectsShadow_Install(void) {
    char v[16] = "";
    GetPrivateProfileStringA("Settings", "MPObjectsShadow", "", v, sizeof(v), ".\\settings.ini");
    if (_stricmp(v, "on") != 0 && strcmp(v, "1") != 0)
        return;
    bool ok = XbeOriginal_Redirect(kPickupUpdate, (const void *)&ShadowPickupUpdate);
    ok &= XbeOriginal_Redirect(kGTTrack, (const void *)&ShadowGTTrack);
    ok &= XbeOriginal_Redirect(kHUDUpdate, (const void *)&ShadowHUDUpdate);
    printf("[mpobjshadow] comparing Pickup_Update, GT_Track and HUD_RadarUpdate with the originals%s\n",
           ok ? "" : " (not every redirect took)");
}
