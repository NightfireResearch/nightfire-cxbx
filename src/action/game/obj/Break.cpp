#include "Break.h"

#include "../../game.h"
#include "../../memory.h"
#include "../../util/Random.h"
#include "../../sound/Sound.h"
#include "../../engine/psiDraw.h"
#include "../drone/Drone.h"
#include "../view.h"

#include "build.h"
#include "control.h"
#include "Effect.h"
#include "Gas.h"

#include <bit>
#include <math.h>
#include <string.h>

#pragma fp_contract(off)

// The shards of every window broken
// XBE_GLOBAL(0x001dc564, 0xc)
#define ShardList (*(LLISTINFO_tag *)0x001dc564)

#define Vec_Avg3 ((void (__cdecl *)(_VECTOR *a, _VECTOR *b, _VECTOR *c, _VECTOR *out))0x000d4dd0)
// The PS2's psiFinishedObjectShatter; on the Xbox a thunk to __WBINVD
#define psiFinishedObjectShatter ((void (__cdecl *)(void))0x000dfb20)

// The vertices and planes of a leaf box's triangles: shorts, the normals times 2^-14
constexpr float kCollScale = 6.103888e-05f;
static_assert(std::bit_cast<uint32_t>(kCollScale) == 0x38800200, "the original's constant at 0x0015d4fc");

// AUTOINJECT
void InitShards(void) {
    LList_Init(&ShardList, sizeof(SHARD_tag), 0);
}

// The hits of the frame: bullets take their damage from the health, and in HendersonA a drone walking through a window
// takes 20 times the square of how far it moved. Below 0 health, it breaks with the last of those hits.
// AUTOINJECT
void Break_Update(obj_tag *obj) {

    if (obj == NULL || (obj->flags & 1))
        return;

    ObjData_Break *breakData = (ObjData_Break *)obj->extraObjectData;
    HITDATA_tag *last = NULL;

    Collide_FilterBullets(&obj->hitList, breakData->mask | 0x106);

    for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {

        obj_tag *hitter = hit->hitObj;
        if (hitter == NULL)
            continue;

        switch (hitter->objectType) {
        case OBJECTTYPE_DRONE:
            if (GameState.CurrentLevelHashcode == HT_Level_HendersonA && breakData->breakType == BreakType_Window) {
                last = hit;
                breakData->health =
                    (float)(breakData->health - (double)Vec_SqDist3D(&hitter->position, &hitter->lastPosition) * 20.0f);
            }
            break;
        case OBJECTTYPE_BULLET:
            last = hit;
            breakData->health -= hit->dmgAmt;
            break;
        }
    }

    if (breakData->health < 0.0f)
        _Break_Kill(obj, last);
}

// An explosion's damage (Explode_Propagate)
// AUTOINJECT
void Break_ApplyDamage(obj_tag *obj, HITDATA_tag *hit) {
    ObjData_Break *breakData = (ObjData_Break *)obj->extraObjectData;
    breakData->health -= hit->dmgAmt;
    if (breakData->health < 0.0f)
        _Break_Kill(obj, hit);
}

// Breaks the first breakable whose sphere holds pos (a script's), hit at its centre from in front
// AUTOINJECT
void Break_DoBreak(_VECTOR *pos) {

    for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {

        if (obj->objectType != OBJECTTYPE_BREAK)
            continue;

        float radius = obj->radius;
        if (Vec_SqDist3D(pos, &obj->centrePoint) < (double)radius * radius) {

            // The original leaves the rest of the hit as stack (the material and object Break_Kill reads)
            HITDATA_tag hit = {};
            _MATRIX mtx;
            RotMatrix(&obj->rotation, &mtx);
            Mat_GetDir(&hit.surfaceNormal.normal, &mtx);
            Vec_Copy(&obj->centrePoint, &hit.hitPosition);
            hit.maybehitDirection.x = 0.0f;
            hit.maybehitDirection.y = 0.0f;
            hit.maybehitDirection.z = -0.5f;
            _Break_Kill(obj, &hit);
            return;
        }
    }
}

// Marks the object deleted and, if its model has collision, smashes it: a window into shards, anything else into
// debris of the hit's material (10 for none)
void _Break_Kill(obj_tag *obj, HITDATA_tag *hit) {

    COLLDATA_tag *coll = obj->objGraphics->colldata;
    obj->flags |= 1;
    if (coll == NULL)
        return;

    ObjData_Break *breakData = (ObjData_Break *)obj->extraObjectData;

    Action_SFX sfx;
    if (breakData->breakType == BreakType_Window)
        sfx = SFX_ENV_WINDOW_SMASH_01;
    else if (Rand_Rand(100) > 40)
        sfx = SFX_ENV_BREAK_CERAMIC_LARGE;
    else
        sfx = SFX_ENV_BREAK_CERAMIC_SMALL;
    Sound_Play3D(sfx, &obj->centrePoint, 100.0f, -1.0f, -1.0f, 0, 0, 0);

    if (breakData->breakType == BreakType_Window) {
        Break_ShatterWindow(obj, hit);
        return;
    }

    Drone_BrokenObject(obj, hit);
    if ((hit->materialType & 0x3f) == 0)
        hit->materialType = 0x10;
    Vec_Zero(&hit->unknown1);
    Debris_Create_Loop(coll, hit, obj);
}

// The object in ESI, the hit in EDI
// AUTOLTCG
void __declspec(naked) Break_Kill(void) {
    _asm {
        push edi
        push esi
        call _Break_Kill
        add esp, 8
        ret
    }
}

// Every triangle of the window's model, shattered with its own texture
// AUTOINJECT
void Break_ShatterWindow(obj_tag *obj, HITDATA_tag *hit) {

    TriList *list = psiGetTriList(obj);
    if (list == NULL)
        return;

    TriListTriangle *tri = list->triangles;
    TriListVertex *vertices = list->vertices;
    for (int i = 0; i < list->triangleCount; i++, tri++) {
        TriListVertex verts[3];
        verts[0] = vertices[tri->a];
        verts[1] = vertices[tri->b];
        verts[2] = vertices[tri->c];
        ShatterPolyRecurse(0, verts, obj, hit, tri->texture);
    }

    psiFinishedObjectShatter();
}

// One float of a vertex, a fraction t of the way back from b to a
static float ShatterLerp(float a, float b, double t) {
    return (float)(((double)a - b) * t + b);
}

// Cuts the triangle (a window's, in its own space) in two across its longest side until that side's square is no
// more than half the square of the triangle's distance from the hit (0.5 to 100), then makes it a shard: still for
// a time that grows with the distance, then flying off along the hit's direction, slower and spinning less the
// further it was
// AUTOINJECT
void ShatterPolyRecurse(int depth, TriListVertex *verts, obj_tag *obj, HITDATA_tag *hit, uint texture) {

    _VECTOR centre, world;
    Vec_Avg3(&verts[0].pos, &verts[1].pos, &verts[2].pos, &centre);
    Vec_Add2(&centre, &obj->position, &world);

    float distSq = Vec_SqDist3D(&hit->hitPosition, &world);
    float side01 = Vec_SqDist3D(&verts[0].pos, &verts[1].pos);
    float side12 = Vec_SqDist3D(&verts[1].pos, &verts[2].pos);
    double side20 = Vec_SqDist3D(&verts[2].pos, &verts[0].pos);     // kept unrounded

    double longer = side12 > side20 ? side12 : side20;
    double longest = side01 > longer ? side01 : longer;

    double limit = distSq * 0.5f;
    if (limit > 0.5f) {
        if (!(limit < 100.0f))
            limit = 100.0f;
    } else {
        limit = 0.5f;
    }

    if (!(longest > limit)) {

        for (int i = 0; i < 3; i++)
            Vec_Subtract(&verts[i].pos, &centre, &verts[i].pos);

        SHARD_tag *shard = (SHARD_tag *)Mem_Malloc(sizeof(SHARD_tag), 0x3204, 0);
        memset(shard, 0, sizeof(SHARD_tag));
        LList_Add(&ShardList, &shard->node);

        verts[0].texture = texture;
        shard->vertices = psiCreateShard(verts);
        Vec_Add2(&centre, &obj->position, &shard->pos);
        shard->rot.x = 0.0f;
        shard->rot.y = 0.0f;
        shard->rot.z = 0.0f;

        float dist = (float)sqrt((double)distSq);
        shard->delay = (float)((double)dist * REC_FRAME_RATE * 15.0f);

        float spinScale = distSq * 10.0f;
        shard->spin.x = (float)((Float_FRand(1.0f) - 0.5f) / spinScale);
        shard->spin.y = (float)((Float_FRand(1.0f) - 0.5f) / spinScale);
        shard->spin.z = (float)((Float_FRand(1.0f) - 0.5f) / spinScale);

        double speed = 0.125f / (double)dist;
        if (speed > 0.1f)
            speed = 0.1f;
        shard->vel.x = (float)(speed * hit->maybehitDirection.x);
        shard->vel.y = (float)(speed * hit->maybehitDirection.y);
        shard->vel.z = (float)(speed * hit->maybehitDirection.z);

        shard->texture = texture;
        shard->life = REC_FRAME_RATE * 25.0f;
        return;
    }

    // The longest side to 0-1
    TriListVertex swap;
    double longer0 = side01 > side20 ? side01 : side20;
    if (side12 >= longer0) {
        swap = verts[0];
        verts[0] = verts[1];
        verts[1] = verts[2];
        verts[2] = swap;
    } else {
        double longer1 = side12 > side01 ? side12 : side01;
        if (side20 >= longer1) {
            swap = verts[0];
            verts[0] = verts[2];
            verts[2] = verts[1];
            verts[1] = swap;
        }
    }

    // A new vertex a quarter to three quarters of the way along it
    double t = Float_FRand(1.0f) * 0.5f + 0.25f;
    TriListVertex mid;
    mid.texture = verts[0].texture;
    mid.pos.x = ShatterLerp(verts[0].pos.x, verts[1].pos.x, t);
    mid.pos.y = ShatterLerp(verts[0].pos.y, verts[1].pos.y, t);
    mid.pos.z = ShatterLerp(verts[0].pos.z, verts[1].pos.z, t);
    for (int i = 0; i < 2; i++)
        mid.uv[i] = ShatterLerp(verts[0].uv[i], verts[1].uv[i], t);
    for (int i = 0; i < 4; i++)
        mid.colour[i] = ShatterLerp(verts[0].colour[i], verts[1].colour[i], t);

    TriListVertex half[3];
    half[0] = verts[0];
    half[1] = mid;
    half[2] = verts[2];
    ShatterPolyRecurse(depth + 1, half, obj, hit, texture);

    half[0] = mid;
    half[1] = verts[1];
    half[2] = verts[2];
    ShatterPolyRecurse(depth + 1, half, obj, hit, texture);
}

// AUTOINJECT
void UpdateAllShards(void) {

    if (GS_IsPaused(-1))
        return;

    SHARD_tag *next;
    for (SHARD_tag *shard = (SHARD_tag *)ShardList.head; shard != NULL; shard = next) {

        next = (SHARD_tag *)shard->node.next;
        if (shard->vertices == NULL)
            continue;

        if (shard->life <= 0.0f) {
            void *node = LList_Remove(&ShardList, &shard->node);
            psiDeleteShard(&shard->vertices);
            Mem_Free(&node);
        } else if (shard->delay <= 0.0f) {
            Vec_Add2(&shard->rot, &shard->spin, &shard->rot);
            Vec_Add2(&shard->pos, &shard->vel, &shard->pos);
            auxVec_AddMulR32(&shard->vel, &GRAVITY_VECTOR,
                             (float)((double)REC_FRAME_RATE * REC_FRAME_RATE * (FRAME_RATE_MUL * 4.0f)), &shard->vel);
            shard->life -= REC_FRAME_RATE;
        } else {
            shard->delay -= REC_FRAME_RATE;
        }
    }
}

// The shards fade out over their last quarter second or so
// AUTOINJECT
void DrawAllShards(void) {

    for (SHARD_tag *shard = (SHARD_tag *)ShardList.head; shard != NULL; shard = (SHARD_tag *)shard->node.next) {

        if (shard->vertices == NULL || !(shard->life > 0.0f))
            continue;

        double alpha = (double)_FRAME_RATE * shard->life * 10.0f;
        if (alpha > 255.0f)
            alpha = 255.0f;
        psiSetTweakA((uint8_t)(int)alpha);

        _MATRIX mtx;
        View_RotTransMatrix(&shard->rot, &shard->pos, &mtx);
        psiDrawShard(shard->vertices, &mtx, shard->texture);
    }
}

// Debris of the hit's material from every third triangle of the model's collision leaves, each hit at the
// triangle's centre with its plane
// AUTOINJECT
void Debris_Create_Loop(COLLDATA_tag *coll, HITDATA_tag *hit, obj_tag *obj) {

    for (int i = 0; i < coll->numCollBoxes; i++) {

        COLLBOX_tag *box = &coll->collBoxes[i];
        if (box->childA >= 0)
            continue;

        uchar *block = coll->dataStartC[box->vertexBlock].raw;
        for (int t = box->childB; t < box->triEnd; t += 3) {

            collDataB *tri = &coll->dataStartB[t];
            _VECTOR verts[3];
            Intersect_UnpackTriangleVerticesToWorld(block, tri->v[0], tri->v[1], tri->v[2], verts, &box->quant);
            Vec_Avg3(&verts[0], &verts[1], &verts[2], &hit->hitPosition);
            Vec_Add2(&hit->hitPosition, &obj->position, &hit->hitPosition);

            short *plane = (short *)block + tri->normal;
            hit->surfaceNormal.a = (float)plane[0] * kCollScale;
            hit->surfaceNormal.b = (float)plane[1] * kCollScale;
            hit->surfaceNormal.c = (float)plane[2] * kCollScale;
            memcpy(&hit->surfaceNormal.d, &plane[3], sizeof(float));

            Debris_Create(hit, &EffectInfo[hit->materialType & 0x3f], obj->inCel, 1);
        }
    }
}

// AUTOINJECT
obj_tag* Break_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *lvl) {

    obj_tag *obj = control_create_object(sizeof(ObjData_Break), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    ObjData_Break *breakData = (ObjData_Break *)obj->extraObjectData;
    obj->objectType = OBJECTTYPE_BREAK;
    Control_SetGList(obj, celgl);
    build_LinkToRoom(obj, 0, glb_world);

    BreakPlacement *keys = (BreakPlacement *)lvl;
    breakData->breakType = (BreakType)keys->breakType;
    breakData->health = (float)keys->health;
    breakData->mask = keys->mask != 0 ? (ushort)~keys->mask : 0;
    return obj;
}
