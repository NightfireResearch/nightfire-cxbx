#include "Effect.h"

#include <math.h>

#include "../../game.h"
#include "../../engine/viewer.h"
#include "../../sound/Sound.h"
#include "../mp/multiplayer.h"
#include "bullet.h"
#include "control.h"
#include "Emitter.h"
#include "Gas.h"

#pragma fp_contract(off)

// The marks of bullets and feet
// XBE_GLOBAL(0x002237c0, 0x18)
#define EffectList (*(DLISTINFO_tag *)0x002237c0)

#define hashtable_set_sub_object_to_entity_gfx ((void (__cdecl *)(ATTACH_tag *mark, HASHCODE hashcode))0x0006b6d0)

// The laser's sparks
static const HASHCODE kLaserSparks[5] = {(HASHCODE)0x020002d1, (HASHCODE)0x020002d2, (HASHCODE)0x020002d3,
                                         (HASHCODE)0x020002d4, (HASHCODE)0x020002d5};
// And its dust
#define EFFECT_LASER_DUST ((HASHCODE)0x02000039)

// AUTOINJECT
void Effect_InitLists(void) {
    DList_Init(&EffectList, sizeof(ATTACH_tag), 200);
}

// Whether a bullet glances off the material: a draw in its ricochet range, masked (0 without either). The material
// is held to the table here, as nowhere else.
// AUTOINJECT
uchar Effect_RicochetProb(uchar material, ushort mask) {
    int surface = (material & 0x3f) > 22 ? 22 : material & 0x3f;
    ushort range = EffectInfo[surface].ricochet;
    if (range == 0 || mask == 0)
        return 0;
    return Rand_Rand(range) & mask;
}

// What a hit leaves, by its material: in the hit, a direction along the surface (glass's normal turned round for
// it); with EFFECT_SPARKS, sparks and debris; in single player, the bullet's, step's, laser's or body's effects
// AUTOINJECT
void Effect_Create(HITDATA_tag *hit, obj_tag *obj, uint flags, float volume) {

    plane_equ_tag plane = hit->surfaceNormal;
    if (obj == NULL || (hit->materialType & 0x3f) == 0)
        return;

    cel_tag *cel = hit->hitCel;
    if (cel == NULL)
        cel = hit->hitObj != NULL ? hit->hitObj->inCel : NULL;

    if ((hit->materialType & 0x3f) == 0x10) {
        hit->surfaceNormal.a = -hit->surfaceNormal.a;
        hit->surfaceNormal.b = -hit->surfaceNormal.b;
        hit->surfaceNormal.c = -hit->surfaceNormal.c;
    }
    _VECTOR u, v;
    Vec_MakeOrthonormalBasis(&u, &v, &hit->surfaceNormal.normal);
    Vec_Add2(&u, &v, &hit->unknown1);

    if (flags & EFFECT_SPARKS) {
        Emitter_Create(EffectInfo[hit->materialType & 0x3f].emitter, &hit->hitPosition, &hit->surfaceNormal.normal,
                       0.0f, 0, 0, 0, 0.0f);
        if (EffectInfo[hit->materialType & 0x3f].debrisOn) {
            ushort count = Rand_Rand(EffectInfo[hit->materialType & 0x3f].debrisMax) + 1;
            Debris_Create(hit, &EffectInfo[hit->materialType & 0x3f], cel, count);
        }
    }

    if (MPSettings.isMultiplayer)
        return;

    hit->surfaceNormal = plane;
    if (flags & EFFECT_BULLET)
        _Effect_Bullet(obj, hit, cel, flags);
    else if (flags & EFFECT_STEP)
        _Effect_Player(hit, obj, cel, flags, volume);
    else if (flags & EFFECT_LASER)
        _Effect_Laser(hit, cel, obj, flags);
    else if (flags & EFFECT_BODY)
        _Effect_Body(hit, obj, cel, flags);
}

// A bullet's sound; its material's smoke (always for the player's bullets, half the time for others'); and its mark,
// a centimetre out from the surface, facing out
void _Effect_Bullet(obj_tag *obj, HITDATA_tag *hit, cel_tag *cel, uint flags) {

    BU_tag *bullet = (BU_tag *)obj->extraObjectData;
    _VECTOR *normal = &hit->surfaceNormal.normal;

    _VECTOR surfaceRot;
    Vec_NormToRot(normal, &surfaceRot);
    _VECTOR faceRot;
    double x = -normal->x, z = -normal->z;
    faceRot.x = -maybeAtan2(-normal->y, (float)sqrt(x * x + z * z));
    faceRot.y = maybeAtan2(-normal->x, -normal->z);
    faceRot.z = 0.0f;

    HEINFO_tag *info = &EffectInfo[hit->materialType & 0x3f];
    if (flags & EFFECT_SOUND)
        Sound_Play3D((Action_SFX)info->soundBullet, &hit->hitPosition, 100.0f, -1.0f, -1.0f, 0, 0, 0);

    obj_tag *shooter = bullet->firedByObj;
    if ((shooter != NULL && shooter->objectType == OBJECTTYPE_PLAYER) || Rand_Rand(2) == 0) {
        for (int i = 0; i < 3; i++) {
            HEGAS_tag *gas = &info->gas[i];
            if (gas->entity == 0)
                continue;
            _VECTOR at;
            if (gas->onSurface) {
                if (fabsf(normal->y) > 0.3f)
                    continue;
                auxVec_AddMulR32(&hit->hitPosition, normal, 0.05f, &at);
            } else {
                Vec_Copy(&hit->hitPosition, &at);
            }
            _VECTOR *rot = gas->onSurface ? &surfaceRot : gas->faceOut ? &faceRot : &CONST_ZERO_VECTOR;
            Gas_Init(cel, &at, rot, NULL, gas->scale0, gas->scale1, gas->entity, gas->kind, gas->colour0, gas->colour1,
                     gas->life);
        }
    }

    if (EffectInfo[hit->materialType & 0x3f].decal == 0)
        return;
    ATTACH_tag *mark = Effect_NewMark();
    if (mark == NULL)
        return;
    Mat_Align2Dir(&mark->matrix, normal, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
    auxVec_AddMulR32(&hit->hitPosition, normal, 0.01f, (_VECTOR *)&mark->matrix.m[12]);
    mark->parent = hit->hitObj;
    hashtable_set_sub_object_to_entity_gfx(mark, EffectInfo[hit->materialType & 0x3f].decal);
    if (hit->hitCel != NULL)
        _Effect_LinkToCel(mark, cel);
    else
        _Effect_LinkToObject(mark, hit->hitObj);
}

// The object in EAX, the hit in ECX
// AUTOLTCG
void __declspec(naked) Effect_Bullet(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ecx
        push eax
        call _Effect_Bullet
        add esp, 16
        ret
    }
}

// A step's sound, and with EFFECT_LEFT_FOOT or EFFECT_RIGHT_FOOT the footprint under that foot, facing where the
// player faces (the foot, for anything else), if the foot is within 20 cm of the floor
void _Effect_Player(HITDATA_tag *hit, obj_tag *obj, cel_tag *cel, uint flags, float volume) {

    if (flags & EFFECT_SOUND) {
        DYNAMICSOUNDS *sound = Sound_Play3D((Action_SFX)EffectInfo[hit->materialType & 0x3f].soundStep,
                                            &hit->hitPosition, volume, -1.0f, -1.0f, 0, 0, 0);
        Sound_SetAlertness(sound, 0.0f);
    }

    HASHCODE footprint = (flags & EFFECT_LEFT_FOOT) ? EffectInfo[hit->materialType & 0x3f].footprint4
                                                    : EffectInfo[hit->materialType & 0x3f].footprint8;
    if (!(flags & (EFFECT_LEFT_FOOT | EFFECT_RIGHT_FOOT)) || footprint == 0)
        return;

    _MATRIX *foot = &obj->transformMatrix;
    if (flags & EFFECT_LEFT_FOOT)
        foot = &obj->animState->animObj.boneMtxs[55];
    else if (flags & EFFECT_RIGHT_FOOT)
        foot = &obj->animState->animObj.boneMtxs[51];

    _VECTOR dir;
    if (ScriptCam == 0 && !MPSettings.isMultiplayer && obj->objectType == OBJECTTYPE_PLAYER)
        Mat_GetDir(&dir, &obj->transformMatrix);
    else
        Mat_GetDir(&dir, foot);

    ATTACH_tag *mark = Effect_NewMark();
    if (mark == NULL)
        return;
    Mat_Align2Up((float *)&mark->matrix, (float *)&hit->surfaceNormal, (float *)&dir);
    Vec_Copy((_VECTOR *)&foot->m[12], (_VECTOR *)&mark->matrix.m[12]);
    // Too far from the floor, the mark stays taken but is never shown
    if (fabs((double)mark->matrix.m[13] - hit->hitPosition.y) > 0.2f)
        return;
    mark->parent = obj;
    mark->matrix.m[13] = hit->hitPosition.y + 0.05f;
    hashtable_set_sub_object_to_entity_gfx(mark, footprint);
    if (hit->hitCel != NULL)
        _Effect_LinkToCel(mark, cel);
    else
        _Effect_LinkToObject(mark, hit->hitObj);
}

// The hit in EAX, the object in EBX
// AUTOLTCG
void __declspec(naked) Effect_Player(void) {
    _asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push ebx
        push eax
        call _Effect_Player
        add esp, 20
        ret
    }
}

// The laser's sound and its material's smoke, with a spark six times in ten; every other frame a little dust near
// the point
void _Effect_Laser(HITDATA_tag *hit, cel_tag *cel, obj_tag *obj, uint flags) {

    if (flags & EFFECT_SOUND)
        Sound_PlayExt((Action_SFX)EffectInfo[hit->materialType & 0x3f].soundStep, 100.0f, 0, 0);

    _VECTOR *normal = &hit->surfaceNormal.normal;
    if (EffectInfo[hit->materialType & 0x3f].laserGas != 0) {

        _VECTOR rot;
        double x = normal->x, z = normal->z;
        rot.x = -maybeAtan2(normal->y, (float)sqrt(x * x + z * z));
        rot.y = maybeAtan2(normal->x, normal->z);
        rot.z = 0.0f;
        _VECTOR at;
        auxVec_AddMulR32(&hit->hitPosition, normal, 0.005f, &at);
        Gas_Init(cel, &at, &rot, NULL, 2.0f, 3.0f, EffectInfo[hit->materialType & 0x3f].laserGas, 5, 0x7f7f64ff,
                 0x643c0018, 10);

        if (Rand_Rand(100) > 40) {
            _VECTOR out = *normal;
            obj_tag *spark = control_create_object(sizeof(Effect_tag), &hit->hitPosition, NULL, NULL);
            ((Effect_tag *)spark->extraObjectData)->life = 1;
            spark->objectType = OBJECTTYPE_EFFECT;
            spark->scale = (float)(Float_FRand(2.0f) + 2.0f);
            spark->rotation.z = (float)Float_FRand(6.2831855f);
            spark->position.x = (float)((double)out.x * 0.01f + spark->position.x);
            spark->position.y = (float)((double)out.y * 0.01f + spark->position.y);
            spark->position.z = (float)((double)out.z * 0.01f + spark->position.z);
            hashtable_set_object_to_entity_gfx(spark, kLaserSparks[Rand_Rand(5)]);
            spark->effectFlags |= 0x20;
            control_link_object_to_cel(spark, cel);
        }
    }

    if (GameState.NumFramesUnpaused & 1) {
        _VECTOR dust;
        Rand_FRandHalf_Vec(&dust, 0.1f);
        Vec_Mul(&dust, &hit->unknown1);
        Vec_Add2(&dust, &hit->hitPosition, &dust);
        Gas_Init(cel, &dust, &CONST_ZERO_VECTOR, NULL, 0.005f, 0.02f, EFFECT_LASER_DUST, 0, 0xc8c8c87f, 0x20202006,
                 0x15);
    }
}

// The hit in EAX, the cel in EBX
// AUTOLTCG
void __declspec(naked) Effect_Laser(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ebx
        push eax
        call _Effect_Laser
        add esp, 16
        ret
    }
}

// Only the sound, which makes the drones more watchful
void _Effect_Body(HITDATA_tag *hit, obj_tag *obj, cel_tag *cel, uint flags) {
    if (flags & EFFECT_SOUND) {
        DYNAMICSOUNDS *sound = Sound_Play3D((Action_SFX)EffectInfo[hit->materialType & 0x3f].soundBody,
                                            &hit->hitPosition, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        Sound_ModAlertness(sound, 0.5f);
    }
}

// The hit in EAX
// AUTOLTCG
void __declspec(naked) Effect_Body(void) {
    _asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push eax
        call _Effect_Body
        add esp, 16
        ret
    }
}

// Off its cel's or object's marks (it stays in use)
void _Effect_UnLink(ATTACH_tag *mark) {
    if (mark == NULL)
        return;
    ATTACH_tag *next = mark->next;
    ATTACH_tag *prev = mark->prev;
    if (next != NULL)
        next->prev = prev;
    if (prev != NULL)
        prev->next = next;
    if (mark->cel != NULL && mark->cel->attachments == mark)
        mark->cel->attachments = next;
    if (mark->parent != NULL && mark->parent->attachments == mark)
        mark->parent->attachments = next;
    mark->next = NULL;
    mark->prev = NULL;
}

// The mark in EAX
// AUTOLTCG
void __declspec(naked) Effect_UnLink(void) {
    _asm {
        push ecx
        push edx
        push eax
        call _Effect_UnLink
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// A mark from EffectList; when they are all in use, the oldest is taken back
// AUTOINJECT
ATTACH_tag* Effect_NewMark(void) {
    ATTACH_tag *mark = (ATTACH_tag *)DList_MoveFromFree2InUse(&EffectList);
    if (mark == NULL) {
        ATTACH_tag *oldest = (ATTACH_tag *)EffectList.activeList.head;
        _Effect_UnLink(oldest);
        DList_RemoveFromInUse2Free(&EffectList, &oldest->node);
        mark = (ATTACH_tag *)DList_MoveFromFree2InUse(&EffectList);
    }
    return mark;
}

void _Effect_LinkToCel(ATTACH_tag *mark, cel_tag *cel) {
    ATTACH_tag *first = cel->attachments;
    mark->next = first;
    mark->prev = NULL;
    if (first != NULL)
        first->prev = mark;
    cel->attachments = mark;
    mark->parent = NULL;
    mark->cel = cel;
}

// The mark in EAX, the cel in EDX
// AUTOLTCG
void __declspec(naked) Effect_LinkToCel(void) {
    _asm {
        push ecx
        push edx
        push edx
        push eax
        call _Effect_LinkToCel
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// On the object, in its space
void _Effect_LinkToObject(ATTACH_tag *mark, obj_tag *obj) {
    if (mark == NULL || obj == NULL)
        return;
    _MATRIX inverse;
    _VECTOR world;
    Mat_Copy(&obj->transformMatrix, &inverse);
    Mat_InverseSelf(&inverse);
    Vec_Copy((_VECTOR *)&mark->matrix.m[12], &world);
    Concat(&inverse, &mark->matrix);
    RotPreTransVec(&inverse, &world, (_VECTOR *)&mark->matrix.m[12]);
    ATTACH_tag *first = obj->attachments;
    mark->next = first;
    mark->prev = NULL;
    if (first != NULL)
        first->prev = mark;
    obj->attachments = mark;
    mark->parent = obj;
    mark->cel = NULL;
}

// The mark in ESI, the object in EDI
// AUTOLTCG
void __declspec(naked) Effect_LinkToObject(void) {
    _asm {
        push edi
        push esi
        call _Effect_LinkToObject
        add esp, 8
        ret
    }
}

// Every mark on the object back to EffectList (it is deleted or broken)
// AUTOINJECT
void Effect_RemoveSObjs(obj_tag *obj) {
    ATTACH_tag *mark = obj->attachments;
    obj->attachments = NULL;
    while (mark != NULL) {
        ATTACH_tag *next = mark->next;
        _Effect_UnLink(mark);
        DList_RemoveFromInUse2Free(&EffectList, &mark->node);
        mark = next;
    }
}
