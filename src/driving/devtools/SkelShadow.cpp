#include "SkelShadow.h"

#include "../eagl/anim/Skeleton.h"
#include "../eagl/Transform.h"
#include "../../common/xbeOriginal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// A live shadow test of the Skeleton functions the engine calls every frame a character is posed (ActPoser,
// ActSkeleton; eagl/anim/Skeleton.cpp), when NIGHTFIRE_SKELSHADOW is set. Each of the six entries is redirected
// to a wrapper here: it snapshots every buffer the call reads or writes, runs the port, keeps what it wrote,
// puts the snapshots back and runs the original (with the original matrix multiply hook) - whose output the game
// then keeps - and compares the two byte for byte. Real skeletons, real poses, real masks: whatever the level
// poses. A summary line every so often counts calls and differences per function; the first differences are
// printed in full.
//
// Main thread only, as the game's animation is (common/xbeOriginal.h).
// ---------------------------------------------------------------------------------------------------------------

namespace {

enum Fn { kSQTToLocal, kLocalToGlobal, kSQTToGlobal, kGlobalToSkin, kStillPose, kStillPoseBone, kFnCount };
const char *const kNames[kFnCount] = { "PoseSQTToLocal", "PoseLocalToGlobal", "PoseSQTToGlobal",
                                       "PoseGlobalToSkin", "GetStillPose", "GetStillPose(int)" };
const unsigned kAddress[kFnCount] = { 0x000f9df0, 0x000f9f10, 0x000fa130, 0x000fa290, 0x000fa340, 0x000fa560 };
const unsigned kMultiply = 0x001066f0;

long g_calls[kFnCount], g_diffs[kFnCount], g_total;
int g_reported;

// One buffer a call touches: its live address and a snapshot.
struct Buffer {
    void *live;
    size_t bytes;
    uint8_t *before;
    uint8_t *port;
};

struct Call {
    Buffer b[3];
    int n;
    void Add(void *p, size_t bytes) {
        if (p == NULL || bytes == 0)
            return;
        for (int i = 0; i < n; i++)
            if (b[i].live == p) {
                if (bytes > b[i].bytes)
                    b[i].bytes = bytes;
                return;
            }
        b[n].live = p;
        b[n].bytes = bytes;
        n++;
    }
    void Snapshot() {
        for (int i = 0; i < n; i++) {
            b[i].before = (uint8_t *)malloc(b[i].bytes);
            b[i].port = (uint8_t *)malloc(b[i].bytes);
            memcpy(b[i].before, b[i].live, b[i].bytes);
        }
    }
    void KeepPort() {
        for (int i = 0; i < n; i++) {
            memcpy(b[i].port, b[i].live, b[i].bytes);
            memcpy(b[i].live, b[i].before, b[i].bytes);
        }
    }
    void Compare(Fn fn) {
        bool same = true;
        for (int i = 0; i < n; i++) {
            if (memcmp(b[i].port, b[i].live, b[i].bytes) == 0)
                continue;
            same = false;
            if (g_reported < 12) {
                g_reported++;
                for (size_t w = 0; w < b[i].bytes / 4; w++) {
                    uint32_t p, o;
                    memcpy(&p, b[i].port + w * 4, 4);
                    memcpy(&o, (uint8_t *)b[i].live + w * 4, 4);
                    if (p != o) {
                        printf("[skelshadow] DIFF %s buffer %d word %u: port %08x original %08x\n", kNames[fn], i,
                               (unsigned)w, p, o);
                        break;
                    }
                }
            }
        }
        g_calls[fn]++;
        if (!same)
            g_diffs[fn]++;
        for (int i = 0; i < n; i++) {
            free(b[i].before);
            free(b[i].port);
        }
        if (++g_total % 20000 == 0 || g_total == 100)
            Summary();
    }
    static void Summary() {
        printf("[skelshadow] after %ld calls:", g_total);
        for (int f = 0; f < kFnCount; f++)
            printf(" %s %ld/%ld", kNames[f], g_diffs[f], g_calls[f]);
        printf(" (differing/calls)\n");
        fflush(stdout);
    }
};

struct Originals {
    explicit Originals(unsigned at) : fn(at), mul(kMultiply) {}
    XbeOriginalScope fn, mul;
};

size_t MaskBytes(const BoneMask *m) { return m != NULL ? sizeof(BoneMask) : 0; }

void __fastcall SQTToLocal(Skeleton *s, int, const float *pose, Transform *local, const BoneMask *mask) {
    Call c = {};
    c.Add((void *)pose, s->count * 48u);
    c.Add(local, s->count * 64u);
    c.Add((void *)mask, MaskBytes(mask));
    c.Snapshot();
    s->PoseSQTToLocal(pose, local, mask);
    c.KeepPort();
    {
        Originals o(kAddress[kSQTToLocal]);
        ((void (__fastcall *)(Skeleton *, int, const float *, Transform *, const BoneMask *))kAddress[kSQTToLocal])(
            s, 0, pose, local, mask);
    }
    c.Compare(kSQTToLocal);
}

void __fastcall LocalToGlobal(Skeleton *s, int, const Transform *local, Transform *global, const BoneMask *mask) {
    Call c = {};
    c.Add((void *)local, s->count * 64u);
    c.Add(global, s->count * 64u);
    c.Add((void *)mask, MaskBytes(mask));
    c.Snapshot();
    s->PoseLocalToGlobal(local, global, mask);
    c.KeepPort();
    {
        Originals o(kAddress[kLocalToGlobal]);
        ((void (__fastcall *)(Skeleton *, int, const Transform *, Transform *, const BoneMask *))
             kAddress[kLocalToGlobal])(s, 0, local, global, mask);
    }
    c.Compare(kLocalToGlobal);
}

void __fastcall SQTToGlobal(Skeleton *s, int, const float *pose, Transform *global, const BoneMask *mask) {
    Call c = {};
    c.Add((void *)pose, s->count * 48u);
    c.Add(global, s->count * 64u);
    c.Add((void *)mask, MaskBytes(mask));
    c.Snapshot();
    s->PoseSQTToGlobal(pose, global, mask);
    c.KeepPort();
    {
        Originals o(kAddress[kSQTToGlobal]);
        ((void (__fastcall *)(Skeleton *, int, const float *, Transform *, const BoneMask *))kAddress[kSQTToGlobal])(
            s, 0, pose, global, mask);
    }
    c.Compare(kSQTToGlobal);
}

void __fastcall GlobalToSkin(Skeleton *s, int, const Transform *global, Transform *skin, const BoneMask *mask) {
    Call c = {};
    c.Add((void *)global, s->count * 64u);
    c.Add(skin, s->count * 64u);
    c.Add((void *)mask, MaskBytes(mask));
    c.Snapshot();
    s->PoseGlobalToSkin(global, skin, mask);
    c.KeepPort();
    {
        Originals o(kAddress[kGlobalToSkin]);
        ((void (__fastcall *)(Skeleton *, int, const Transform *, Transform *, const BoneMask *))
             kAddress[kGlobalToSkin])(s, 0, global, skin, mask);
    }
    c.Compare(kGlobalToSkin);
}

void __fastcall StillPose(Skeleton *s, int, float *pose, const BoneMask *mask) {
    Call c = {};
    c.Add(pose, s->count * 48u);
    c.Add((void *)mask, MaskBytes(mask));
    c.Snapshot();
    s->GetStillPose(pose, mask);
    c.KeepPort();
    {
        Originals o(kAddress[kStillPose]);
        ((void (__fastcall *)(Skeleton *, int, float *, const BoneMask *))kAddress[kStillPose])(s, 0, pose, mask);
    }
    c.Compare(kStillPose);
}

void __fastcall StillPoseBone(Skeleton *s, int, int bone, float *pose) {
    Call c = {};
    c.Add(pose, 48);
    c.Snapshot();
    s->GetStillPoseBone(bone, pose);
    c.KeepPort();
    {
        Originals o(kAddress[kStillPoseBone]);
        ((void (__fastcall *)(Skeleton *, int, int, float *))kAddress[kStillPoseBone])(s, 0, bone, pose);
    }
    c.Compare(kStillPoseBone);
}

}  // namespace

void SkelShadow_Install(void) {
    const char *on = getenv("NIGHTFIRE_SKELSHADOW");
    if (on == NULL || on[0] == '\0' || on[0] == '0')
        return;
    const void *wrappers[kFnCount] = { (const void *)&SQTToLocal, (const void *)&LocalToGlobal,
                                       (const void *)&SQTToGlobal, (const void *)&GlobalToSkin,
                                       (const void *)&StillPose, (const void *)&StillPoseBone };
    int ok = 0;
    for (int f = 0; f < kFnCount; f++)
        ok += XbeOriginal_Redirect(kAddress[f], wrappers[f]);
    printf("[skelshadow] %d of %d Skeleton entries shadowed\n", ok, (int)kFnCount);
    fflush(stdout);
}
