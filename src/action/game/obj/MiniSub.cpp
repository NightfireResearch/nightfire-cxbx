#include "MiniSub.h"

#include "bullet.h"
#include "control.h"
#include "../drone/NDrone2.h"
#include "../sp/SwitchChannels.h"
#include "../../game.h"
#include "../../engine/Anim.h"
#include "../../sound/music.h"
#include "../../util/Random.h"
#include "../../math/X87Ops.h"

#pragma fp_contract(off)

// The mini sub (OBJECTTYPE_MINISUB). No level places one.

#define MINISUB_SKIN        ((HASHCODE)0x05000074)
#define MINISUB_ANIM        ((HASHCODE)0x06000741)
#define WEAPON_SUB_TORPEDO  0x30

#define EIGHTH_TURN 0.7853982f   // pi/4, the most it banks

// obj_tag::subState: heading along the route, or turning on the spot
#define MINISUB_TURN    0
#define MINISUB_FORWARD 1

// obj_tag::curState
#define MINISUB_START   0
#define MINISUB_ATTACK  3


// AUTOINJECT
obj_tag* MiniSub_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *lvl) {
    obj_tag *obj = Control_CreateObjEx(sizeof(MiniSubInfo), pos, rot, NULL, NULL, NULL, 1, 0, 1.0f, 0, 0xff, 0xff,
                                       0xff);
    if (obj == NULL)
        return NULL;
    obj->objectType = OBJECTTYPE_MINISUB;
    obj->curState = MINISUB_START;
    obj->subState = MINISUB_TURN;
    AnimObjectNew(obj, MINISUB_SKIN);
    AnimScriptAdd(obj, MINISUB_ANIM)->flags |= ANIMSCRIPT_LOOP;
    sAnimRawSeq_tag *seq = AnimScriptPeekSeq(MINISUB_ANIM);
    MiniSubInfo *sub = (MiniSubInfo *)obj->extraObjectData;
    sub->gameObj = obj;
    sub->channel = (int)lvl->keys[0];
    sub->health = 100.0f;
    sub->height = seq->boundSphere[3];
    sub->unknown18[1] = 0.0f;
    sub->unknown18[0] = 0.0f;
    sub->unknown24[2] = 0.0f;
    sub->unknown24[1] = 0.0f;
    sub->unknown24[0] = 0.0f;
    sub->unknown10 = 0.03f;
    sub->speed = 0.03f;
    sub->turnRate = (float)(0.005f / (double)FRAME_RATE_DIV);
    return obj;
}

// Heads for at, facing it. A place with no cel is given the one it is in.
void _MiniSub_SetTarget(MiniSubInfo *sub, CelPos_tag *at) {
    Vec_Copy(&at->pos, &sub->moveTarget.pos);
    if (at->cel == NULL)
        at->cel = NDrone2_FindCel(&at->pos);
    sub->moveTarget.cel = at->cel;
    sub->moveReach = 0.3f;
    sub->moveDistance = Vec_Dist3D(&sub->gameObj->position, &sub->moveTarget.pos);
    _VECTOR dir;
    Vec_Subtract(&sub->moveTarget.pos, &sub->gameObj->position, &dir);
    Vec_DirToRot(&dir, &sub->facing);
}

// EAX the sub's data, EBX where. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) MiniSub_SetTarget(void) {
    _asm {
        push ebx
        push eax
        call _MiniSub_SetTarget
        add esp, 8
        ret
    }
}

// Turns turnRate of the way to its facing, and eases its roll the same way towards a bank against the turn, at most
// pi/4
void _MiniSubMove_Turn(MiniSubInfo *sub) {
    double exact = Vec_AngleDifference(sub->gameObj->rotation.y, sub->facing.y);
    float gap = (float)exact;
    float bank;
    if (exact < 0.0) {
        if (EIGHTH_TURN < AbsFchs((double)gap))
            bank = EIGHTH_TURN;
        else
            bank = (float)Negate(gap);
    }
    else {
        if (EIGHTH_TURN >= gap || gap != gap)
            bank = (float)Negate(gap);
        else
            bank = -EIGHTH_TURN;
    }
    obj_tag *obj = sub->gameObj;
    obj->rotation.z = (float)(Vec_AngleDifference(obj->rotation.z, bank) * sub->turnRate + obj->rotation.z);
    sub->gameObj->rotation.y = (float)((double)gap * sub->turnRate + sub->gameObj->rotation.y);
}

// ESI the sub's data. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) MiniSubMove_Turn(void) {
    _asm {
        push esi
        call _MiniSubMove_Turn
        add esp, 4
        ret
    }
}

// Turning, and moving speed along the level towards moveTarget
void _MiniSubMove_Forward(MiniSubInfo *sub) {
    _MiniSubMove_Turn(sub);
    _VECTOR dir;
    Vec_Subtract(&sub->moveTarget.pos, &sub->gameObj->position, &dir);
    Vec_Normalise(&dir, &dir);
    dir.y = 0.0f;
    Vec_MulR32(&dir, &dir, sub->speed);
    obj_tag *obj = sub->gameObj;
    Vec_Add2(&obj->position, &dir, &obj->position);
}

// EAX the sub's data
// AUTOLTCG
void __declspec(naked) MiniSubMove_Forward(void) {
    _asm {
        push eax
        call _MiniSubMove_Forward
        add esp, 4
        ret
    }
}

// A step of its route to targetObj within radius, planned afresh (and its first waypoint taken as moveTarget)
// whenever it is no longer good: AINetwork_CalcRouteToObject's status, 3 once the route is done, 6 with no target
// and 8 when the target cannot be reached
uint _MiniSub_Route(MiniSubInfo *sub, obj_tag *targetObj, float radius) {
    if (targetObj == NULL)
        return 6;
    sub->activeRoute = &sub->route;
    sub->route.arriveRadius = radius;
    if (!AINetwork_VerifyRouteTarget(sub->gameObj, sub->target, &sub->aiTarget, &sub->point, radius, sub->activeRoute))
        return 8;
    uint status = 0;
    if (!AINetwork_RouteIsValid(sub->activeRoute)) {
        status = AINetwork_CalcRouteToObject(sub->gameObj, targetObj, &sub->route, &sub->aiTarget, &sub->point, radius,
                                             0);
        if (status > 1)
            return status;
        sub->activeRoute->current = -1;
        if (!LinkCreep_Calc(&sub->route, 0))
            return 8;
        LinkCreep_Dest(&sub->route, 0, &sub->moveTarget);
    }
    if (AINetwork_FollowRoute(sub->activeRoute, NULL) == 3)
        status = 3;
    AINetwork_GetRouteDistance3D(sub->activeRoute);
    return status;
}

// ESI the sub's data; the target and radius on the stack. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) MiniSub_Route(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push esi
        call _MiniSub_Route
        add esp, 0xc
        ret
    }
}

// Once its time has come: a torpedo from its nose, 0.8 ahead, a little off; half of them home on player 1
void _MiniSub_FireTorpedo(MiniSubInfo *sub) {
    if (sub->nextFireFrame > GameState.NumFramesUnpaused)
        return;
    sub->lastFireFrame = GameState.NumFramesUnpaused;
    obj_tag *obj = sub->gameObj;
    _VECTOR at, dir;
    _MATRIX nose, fromNose;
    AnimGetBoneWorldTrans(obj, 0, 0, &at, &nose);
    Mat_GetDir(&dir, &nose);
    Mat_Copy(&nose, &fromNose);
    Mat_InverseSelf(&fromNose);
    auxVec_AddMulR32(&at, &dir, 0.8f, &at);
    dir.x = (float)(Rand_FRandHalf(0.25f) + dir.x);
    dir.y = (float)(Rand_FRandHalf(0.25f) + dir.y);
    Vec_Normalise(&dir, &dir);
    obj_tag *torpedo = Bullet_init(-1, obj, obj, &weapon_data[WEAPON_SUB_TORPEDO], &at, &dir, 0xffff);
    if (Rand_Rand(0x5b7f) & 1)
        ((BU_tag *)torpedo->extraObjectData)->heatseekTarget = glb_players[0];
}

// EAX the sub's data
// AUTOLTCG
void __declspec(naked) MiniSub_FireTorpedo(void) {
    _asm {
        push eax
        call _MiniSub_FireTorpedo
        add esp, 4
        ret
    }
}

// Within 12 of its target it turns to face him and fires, again in 3 to 5 seconds; further off it follows its route
// to him and fires, again in 5 to 8 seconds, or, with no route, turns towards him
void _MiniSubState_Attack(MiniSubInfo *sub) {
    if (sub->target == NULL)
        return;
    CelPos_tag at;
    if (Vec_Dist3D(&sub->gameObj->position, &sub->target->position) < 12.0f) {
        at.pos = sub->target->position;
        at.cel = sub->target->inCel;
        _MiniSub_SetTarget(sub, &at);
        _MiniSubMove_Turn(sub);
        sub->gameObj->subState = MINISUB_TURN;
        uint wait = Rand_Rand(FRAME_RATE_INT * 2);
        sub->nextFireFrame = sub->lastFireFrame + wait + FRAME_RATE_INT * 3;
    }
    else {
        switch (_MiniSub_Route(sub, sub->target, 10.0f)) {
            case 0:
            case 1:
            case 2: {
                _MiniSub_SetTarget(sub, &sub->activeRoute->waypoint);
                sub->gameObj->subState = MINISUB_FORWARD;
                uint wait = Rand_Rand(FRAME_RATE_INT * 3);
                sub->nextFireFrame = sub->lastFireFrame + wait + FRAME_RATE_INT * 5;
                break;
            }
            default:
                at.pos = sub->target->position;
                at.cel = sub->target->inCel;
                _MiniSub_SetTarget(sub, &at);
                sub->gameObj->subState = MINISUB_TURN;
                return;
        }
    }
    _MiniSub_FireTorpedo(sub);
    Music_Event(2, 1);
}

// EAX the sub's data
// AUTOLTCG
void __declspec(naked) MiniSubState_Attack(void) {
    _asm {
        push eax
        call _MiniSubState_Attack
        add esp, 4
        ret
    }
}

// Player 1 as its target, a fresh route over the AI network from where it is, never straight there; then attacking
void _MiniSubState_Init(MiniSubInfo *sub) {
    sub->target = glb_players[0];
    sub->activeRoute = NULL;
    AINetwork_ResetRoute(&sub->route, sub->gameObj, NULL);
    obj_tag *obj = sub->gameObj;
    sub->route.path = AINetwork_NavPathForPosition(&obj->position, obj->inCel, 0);
    sub->route.flags |= AIROUTE_NO_DIRECT;
    sub->route.arriveRadius = 10.0f;
    sub->route.creepReach = 8.0f;
    sub->gameObj->curState = MINISUB_ATTACK;
}

// ESI the sub's data. Not named on PS2; the name is ours.
// AUTOLTCG
void __declspec(naked) MiniSubState_Init(void) {
    _asm {
        push esi
        call _MiniSubState_Init
        add esp, 4
        ret
    }
}

// While its switch channel is on
// AUTOINJECT
void MiniSub_Update(obj_tag *obj) {
    if (obj == NULL)
        return;
    MiniSubInfo *sub = (MiniSubInfo *)obj->extraObjectData;
    if (sub->channel != 0 && !switch_channels[sub->channel])
        return;
    NDrone2_NavNodeCache();
    switch ((short)obj->curState) {
        case MINISUB_START:
            _MiniSubState_Init(sub);
            break;
        case MINISUB_ATTACK:
            _MiniSubState_Attack(sub);
            break;
    }
    if ((short)obj->subState == MINISUB_FORWARD)
        _MiniSubMove_Forward(sub);
    else
        _MiniSubMove_Turn(sub);
    obj->transformFlags |= TRANSFORM_MOVED;
    AnimObjectUpdate(obj);
}
