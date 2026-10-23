#include "Copter.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../../game.h"
#include "../../memory.h"
#include "../../math/X87Ops.h"
#include "../../sound/music.h"
#include "../../sound/Sound.h"
#include "../Spline.h"
#include "../weapon_stats.h"
#include "../sp/PlayerStats.h"
#include "build.h"
#include "bullet.h"
#include "control.h"
#include "Explode.h"
#include "Gas.h"
#include "ScriptPlayer.h"
#include "../../math/math.h"

// XBE_GLOBAL(0x001fe6b4, 0xc)
LLISTINFO_tag CopterPaths;

// XBE_GLOBAL(0x0017c118, 0x4)
float DiffMod = 1.0f;

// XBE_GLOBAL(0x001fe6a8, 0xc)
LLISTINFO_tag CopterList;
// XBE_GLOBAL(0x001fe6c0, 0x4)
float CopterConeCos;
// XBE_GLOBAL(0x001fe6c4, 0x4)
float CopterConeSin;
// XBE_GLOBAL(0x001fe6c8, 0x2)
int16_t CopterAttackMode;

#pragma fp_contract(off)

// XBE_GLOBAL(0x0017c120, 0x48) readonly relocate
COPTERBIT CopterBits[9] = {
    {0x0000, 0x0000, 2.0f},
    {0x0000, 0x0000, 3.0f},
    {0x0000, 0x0000, 1.0f},
    {0x0000, 0x0000, 2.0f},
    {0x0010, 0x0000, 100.0f},
    {0x0000, 0x0000, 1.0f},
    {0x0000, 0x0000, 0.0f},
    {0x0000, 0x0000, 0.0f},
    {0x0000, 0x0000, 0.0f},
};

// Copter_Update's states (obj_tag::curState)
enum {
    COPTER_START = 0,           // counts its group's routes
    COPTER_PATROL = 1,
    COPTER_ATTACK = 2,
    COPTER_LEAVING = 4,         // to its route's end, then the way out
    COPTER_GOING = 5,           // the way out, then gone
    COPTER_DESTROYED = 6,
};

#define COPTER_GUN_SOUND ((Action_SFX)0x5df)
// The level whose helicopter fires other weapons and now and then a missile straight at the player, and the two
// whose helicopters are blown up rather than leave
#define COPTER_LEVEL_A HT_Level_CastleIndoors2
#define COPTER_EXPLODES_A HT_Level_Tower2B
#define COPTER_EXPLODES_B HT_Level_TowerC

// The cone's half angle, a double
static constexpr double CopterConeAngle = 0.78539818525314331;

// A channel turned on, unchecked against the table's 256 (as everywhere the original inlines it)
static void SetChannel(ushort channel) {
    if (channel == 0)
        return;
    switch_channels[channel] = 1;
    switch_channels_time[channel] = GameState.NumFramesUnpaused;
}

// AUTOINJECT
void Copter_Delete(obj_tag *obj) {
    COPTER *copter = (COPTER*)obj->extraObjectData;
    SP_Delete(copter->scriptPlayer);
    LList_Remove(&CopterList, (LLNODE_tag*)copter);
    memset(copter, 0, sizeof(COPTER));
}

// A path for every placement; the helicopter itself only for kind 0. Neither the path's allocation nor the script is
// checked.
// AUTOINJECT
obj_tag* Copter_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl) {
    CopterPlacement *keys = (CopterPlacement *)lvl;
    COPTERPATH *path = Copter_CreatePath(keys);
    if (keys->kind != 0)
        return NULL;

    obj_tag *obj = control_create_object(sizeof(COPTER), pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    COPTER *copter = (COPTER *)obj->extraObjectData;
    copter->path = path;
    copter->obj = obj;
    copter->pathFrame = 0.0f;
    copter->enableChannel = (ushort)keys->enableChannel;
    copter->doneChannel = (ushort)keys->doneChannel;
    copter->channel = (ushort)keys->channel;
    copter->delay = (ushort)keys->delay;
    copter->paths = 0;
    memset(copter->parts, 0, sizeof(copter->parts));
    obj->transformFlags |= TRANSFORM_MATRIX_PLACED | TRANSFORM_POSITION_FROM_MATRIX;
    obj->curState = COPTER_START;
    obj->objectType = OBJECTTYPE_COPTER;
    obj->objGraphics = NULL;
    build_LinkToRoom(obj, 0, glb_world);

    copter->scriptPlayer = SP_Create(pos, rot, keys->script0, keys->script1, 8, 0, NULL, Copter_Register, copter, 0);
    LList_Add(&CopterList, (LLNODE_tag *)copter);
    if (copter->enableChannel != 0 && !switch_channels[copter->enableChannel])
        SP_HideObj(copter->scriptPlayer, 1);

    switch (GameState.difficultyModifier) {
    case 2:
        DiffMod = 1.5f;
        break;
    case 3:
        DiffMod = 2.0f;
        break;
    case 4:
        DiffMod = 4.0f;
        break;
    default:
        DiffMod = 1.0f;
        break;
    }
    copter->damage = 0.0f;
    copter->maxDamage = DiffMod * 299.0f;
    return obj;
}

// AUTOINJECT
obj_tag* Copter_GetBody(COPTER* copter) {
    if (copter->enableChannel != 0 && !SwitchChannel_IsActive(copter->enableChannel))
        return NULL;
    return copter->body;
}

// AUTOINJECT
void Copter_Init(void) {
    LList_Init(&CopterList, sizeof(COPTER), 0);
    LList_Init(&CopterPaths, sizeof(COPTERPATH), 0);
}

// The script's event 15 names its parts: shots pass through all but the body
// FUNC_AT(0x0006b900)
void Copter_Register(obj_tag *obj, ushort part, void *context) {
    if (context == NULL || part >= 9)
        return;
    ((COPTER *)context)->parts[part] = obj;
    if (part != 2)
        obj->effectFlags |= EFFECT_NO_COLLISION;
}

// The static initialisers of the cone
// AUTOINJECT
void Copter_InitConeCos(void) {
    CopterConeCos = (float)FCos(CopterConeAngle);
}

// AUTOINJECT
void Copter_InitConeSin(void) {
    CopterConeSin = (float)FSin(CopterConeAngle);
}

// A path's speed puts its frames into its length (key 10, or the placement's own), at 60 Hz
COPTERPATH* Copter_CreatePath(CopterPlacement *keys) {
    COPTERPATH *path = (COPTERPATH *)Mem_Malloc(sizeof(COPTERPATH), 0x3304, 0);
    memset(path, 0, sizeof(COPTERPATH));
    uint length = keys->length2 != 0 ? keys->length2 : keys->header.splineLength;
    path->kind = (ushort)keys->kind;
    path->group = (ushort)keys->group;
    path->spline = keys->header.spline;
    path->frames = keys->header.splinePoints != 0 ? keys->header.splinePoints - 1 : 0;
    path->start = keys->start < path->frames ? (ushort)keys->start : path->frames;
    path->index = (ushort)keys->index;
    double frames = (double)path->frames * REC_FRAME_RATE;
    path->speed = (float)frames;
    path->speed = (float)((double)_FRAME_RATE / (double)length * frames);
    LList_Add(&CopterPaths, (LLNODE_tag *)path);
    return path;
}

// Another route of the group at the end of one: the more it is hurt the further down the list, and past half of
// what makes it leave, any at random
COPTERPATH* Copter_NextPath(COPTER *copter) {
    double hurt = copter->damage / ((double)copter->maxDamage - (double)copter->maxDamage * 0.1f);
    float pick = (float)(copter->paths * hurt);
    uint next = !(hurt >= 0.5f) ? Ftol((double)pick + 0.5f) : Rand_Rand(copter->paths);
    if (next > copter->paths)
        next = copter->paths;

    for (COPTERPATH *path = (COPTERPATH *)CopterPaths.head; path != NULL; path = (COPTERPATH *)path->node.next) {
        if (path->group == copter->path->group && path->index == next && path->kind != 2) {
            copter->path = path;
            break;
        }
    }
    copter->pathFrame = copter->path->start;
    return copter->path;
}

// Along its path (only the position with positionOnly), the script's models with it; at the path's end another
// route, and true. True with no path.
bool Copter_Move(COPTER *copter, obj_tag *obj, char positionOnly) {
    COPTERPATH *path = copter->path;
    bool ended = false;
    if (path == NULL)
        return true;
    if (path->frames == 0)
        return false;

    _MATRIX mtx;
    Spline_Interp(path->spline, 0, path->frames, copter->pathFrame, &mtx, &copter->quat, 2);
    copter->pathFrame = path->speed + copter->pathFrame;
    if (positionOnly)
        Vec_Copy((_VECTOR *)&mtx.m[12], (_VECTOR *)&obj->transformMatrix.m[12]);
    else
        Mat_Copy(&mtx, &obj->transformMatrix);

    if ((float)path->frames < copter->pathFrame) {
        ended = true;
        if (copter->channel == 100)
            obj->flags |= 1;
        copter->path = Copter_NextPath(copter);
    }
    SP_SetPosRot(copter->scriptPlayer, &obj->transformMatrix);
    return ended;
}

// The gun and its flash turn to just above player 0; every sixth frame a shot. Neither the player nor the flash is
// checked before the turn.
bool Copter_FireGun(COPTER *copter, obj_tag *obj) {
    obj_tag *gun = copter->gun;
    obj_tag *flash = copter->flash;
    _VECTOR target;
    Vec_Copy(&glb_players[0]->position, &target);
    target.y = target.y + 0.5f;
    if (gun == NULL)
        return false;

    _VECTOR *gunPos = (_VECTOR *)&gun->transformMatrix.m[12];
    Mat_LookAt(&gun->transformMatrix, gunPos, &target, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
    Mat_LookAt(&flash->transformMatrix, (_VECTOR *)&flash->transformMatrix.m[12], &target, &CONST_UP_VECTOR,
               &MAYBE_CONST_FORWARD_VECTOR);
    short shots = gun->curState++;
    if (shots < 5)
        return false;

    _VECTOR dir, muzzle;
    Vec_Subtract(&target, gunPos, &dir);
    Vec_Normalise(&dir, &dir);
    auxVec_AddMulR32(&gun->position, &dir, gun->objGraphics->extentMax.z, &muzzle);
    Sound_Play3D(COPTER_GUN_SOUND, &muzzle, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    int weapon = GameState.CurrentLevelHashcode == COPTER_LEVEL_A ? 0x61 : 0x71;
    Bullet_init(-1, obj, gun, &weapon_data[weapon], &muzzle, &dir, 0xffff);
    gun->curState = 0;
    if (flash != NULL) {
        flash->curState = 2;
        flash->effectFlags &= ~EFFECT_HIDDEN;
    }
    return true;
}

// A missile from either launcher, once a second at difficulty 1 (DiffMod's seconds); on one level now and then
// straight at player 0
bool Copter_FireMissile(COPTER *copter, obj_tag *obj) {
    obj_tag *launcher = (Rand_Rand(0x86e6) & 1) ? copter->launchers[0] : copter->launchers[1];
    if (launcher == NULL)
        return false;
    if (GameState.NumFramesUnpaused % (uint)Ftol((double)FRAME_RATE_INT * DiffMod) != 0)
        return false;

    _VECTOR dir;
    if (GameState.CurrentLevelHashcode == COPTER_LEVEL_A && Rand_Rand(Ftol(8.0f / (double)DiffMod)) == 0) {
        Vec_Subtract(&glb_players[0]->position, (_VECTOR *)&launcher->transformMatrix.m[12], &dir);
        Vec_Normalise(&dir, &dir);
    } else {
        Mat_GetDir(&dir, &launcher->transformMatrix);
    }
    _VECTOR at;
    auxVec_AddMulR32(&launcher->centrePoint, &dir, launcher->objGraphics->extentMax.z * 1.5f, &at);
    int weapon = GameState.CurrentLevelHashcode == COPTER_LEVEL_A ? 0x62 : 0x6d;
    Bullet_init(-1, obj, launcher, &weapon_data[weapon], &at, &dir, 0xffff);
    return true;
}

// Within 1000 of the helicopter, in its cone, and in sight of a point 4 ahead of it
bool Copter_SeesPlayer(obj_tag *player, obj_tag *obj) {
    if (player == NULL)
        return false;
    _VECTOR *at = (_VECTOR *)&obj->transformMatrix.m[12];
    if (Vec_SqDist3D(&player->position, at) > 1000000.0f)
        return false;
    _VECTOR dir;
    Mat_GetDir(&dir, &obj->transformMatrix);
    if (!Intersect_ConeSphere(at, &dir, CopterConeCos, CopterConeSin, &player->centrePoint))
        return false;
    _VECTOR ahead;
    auxVec_AddMulR32(at, &dir, 4.0f, &ahead);
    cel_tag *cel = build_FindCel(&ahead, glb_world);
    return Collide_LineOfSight(&ahead, &player->centrePoint, cel, obj, player, 0x2e);
}

// Shown and on its way once its channel is on; patrols, attacks while it sees player 0, takes its parts' shots, smokes
// past half its endurance, and past all of it leaves (or is blown up on two levels)
// AUTOINJECT
void Copter_Update(obj_tag *obj) {
    COPTER *copter = (COPTER *)obj->extraObjectData;
    obj_tag *flash = copter->flash;
    obj_tag *player = glb_players[0];

    if (copter->enableChannel != 0) {
        if (!switch_channels[copter->enableChannel])
            return;
        SP_HideObj(copter->scriptPlayer, 0);
        copter->enableChannel = 0;
    }
    if (copter->delay != 0) {
        copter->delay--;
        if (copter->delay == 0)
            SetChannel(copter->channel);
    }
    if (flash != NULL) {
        if (flash->curState != 0)
            flash->curState--;
        if (flash->curState == 0)
            flash->effectFlags |= EFFECT_HIDDEN;
    }

    switch ((short)obj->curState) {
    case COPTER_START:
        for (COPTERPATH *path = (COPTERPATH *)CopterPaths.head; path != NULL; path = (COPTERPATH *)path->node.next) {
            if (path->group != copter->path->group)
                continue;
            if (path->kind == 2)
                copter->exitPath = path;
            else
                copter->paths++;
        }
        obj->curState = COPTER_PATROL;
        break;

    case COPTER_PATROL:
        Copter_Move(copter, obj, 0);
        if (copter->channel == 100)
            break;
        obj->subState++;
        if ((double)(short)FRAME_RATE_INT * 4 / DiffMod > (double)(short)obj->subState)
            break;
        if (Copter_SeesPlayer(player, obj)) {
            obj->curState = COPTER_ATTACK;
            obj->subState = 0;
            copter->attackDamage = 0.0f;
            CopterAttackMode = Rand_Rand(10);
        }
        break;

    case COPTER_ATTACK: {
        Music_Event(2, 1);
        Copter_Move(copter, obj, 0);
        obj->subState++;
        if (CopterAttackMode & 1)
            Copter_FireGun(copter, obj);
        else
            Copter_FireMissile(copter, obj);
        bool stay = false;
        if (player != NULL && !((double)((short)FRAME_RATE_INT * 5) * DiffMod < (double)(short)obj->subState) &&
            !(copter->attackDamage > 100.0f))
            stay = Copter_SeesPlayer(player, obj);
        if (!stay) {
            obj->subState = 0;
            obj->curState = COPTER_PATROL;
            copter->attackDamage = 0.0f;
        }
        break;
    }

    case COPTER_LEAVING:
        if (!Copter_Move(copter, obj, 0))
            break;
        copter->path = copter->exitPath;
        copter->pathFrame = 0.0f;
        SetChannel(copter->doneChannel);
        obj->curState = COPTER_GOING;
        break;

    case COPTER_GOING:
        if (!Copter_Move(copter, obj, 0))
            return;
        SetChannel(copter->doneChannel);
        obj->flags |= 1;
        return;

    case COPTER_DESTROYED:
        SetChannel(copter->doneChannel);
        Explode_Create(obj, &obj->position, &CONST_UP_VECTOR, 10.0f, 10.0f, HT_Script_ExplosionBlastRadius, 67.4f, 0xff,
                       0xff, 100,
                       NULL, 6);
        obj->flags |= 1;
        return;
    }

    for (int i = 0; i < 9; i++) {
        obj_tag *part = copter->parts[i];
        if (part == NULL)
            continue;
        if (CopterBits[i].ignore != 0)
            Collide_FilterBullets(&part->hitList, CopterBits[i].ignore);
        bool byPlayer = false;
        for (HITDATA_tag *hit = part->hitList; hit != NULL; hit = hit->next) {
            if (!(hit->dmgAmt > 0.0f))
                continue;
            obj_tag *bullet = hit->hitObj;
            if (bullet != NULL && bullet->objectType == OBJECTTYPE_BULLET) {
                obj_tag *shooter = ((BU_tag *)bullet->extraObjectData)->firedByObj;
                if (shooter != NULL && shooter->objectType == OBJECTTYPE_PLAYER)
                    byPlayer = true;
            }
            copter->damage = (float)((double)CopterBits[i].scale * hit->dmgAmt + copter->damage);
            copter->attackDamage = (float)((double)CopterBits[i].scale * hit->dmgAmt + copter->attackDamage);
        }
        if (byPlayer)
            PlrStat_LogShotHitEnemy(0);
    }

    if ((double)copter->maxDamage * 0.5f < copter->damage && copter->smoke != NULL && Rand_Rand(10) > 7) {
        uchar grey = (uchar)Ftol((1.0f - (double)copter->damage / copter->maxDamage) * 127.0f);
        obj_tag *smoke = copter->smoke;
        _VECTOR rot;
        rot.y = 0.0f;
        rot.x = 0.0f;
        rot.z = (float)Float_FRand(TWO_PI);
        _VECTOR vel;
        vel.x = 0.0f;
        vel.z = 0.0f;
        vel.y = 0.0f;
        float size = (float)(Float_FRand(2.0f) + 3.0f);
        uint colour = ((grey << 8 | grey) << 8 | grey) << 8;
        Gas_Init(smoke->inCel, &smoke->centrePoint, &rot, &vel, 2.0f, size, HT_Entity_smoke_3_blacksmoke, 3,
                 colour | (uchar)(0xbf - grey), colour, 0x1a4);
    }

    if (obj->curState != COPTER_GOING && obj->curState != COPTER_LEAVING && copter->damage > copter->maxDamage) {
        obj->curState = COPTER_LEAVING;
        if (GameState.CurrentLevelHashcode == COPTER_EXPLODES_A || GameState.CurrentLevelHashcode == COPTER_EXPLODES_B)
            obj->curState = COPTER_DESTROYED;
        SetChannel(copter->doneChannel);
    }
}
