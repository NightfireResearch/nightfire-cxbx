#ifndef DRONE_H_
#define DRONE_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// This is plausibly a skin - had previously been IDd as "skinHashcode" in DIVars
// It's also the same on Xbox and PS2, according to the size
typedef struct {
    HASHCODE skinHashcode;
    char unknown1[4];
    int minDifficultyLevelForDrone; // Bit of a weird place to put it, but I'm pretty sure I've understood this correctly?
    char unknown2[0x21*4-12];
} maybeSAnimSkin;


typedef struct DCVars_tag {
    obj_tag* gameObj;
    Drone_tag* drone;
    cel_tag* cel;
    void* aiStateMachine;
} DCVars_tag;

typedef struct DIVars_tag {
    obj_tag* gameObj;
    _VECTOR position;
    _VECTOR rotation;
    int someOtherThing;
    maybeSAnimSkin skinInfo;
} DIVars_tag;

typedef struct Drone_tag {
    char unknown[0xec];
    uint aiStateMachine; // a struct, NOT a pointer. FIXME when size of AI State Machine known
    char pad[0x119-4-0xec];
    char associatedSwitchChannel; // At 0x119
    char unknown2[0x1000]; //?
} Drone_tag;

typedef struct MsgObject {
    uint msgType;
    uint param_a;
    uint param_b;
    uint param_c;
    uint createdFrame;
    uint handleOnFrame;
    void* extraData;
} MsgObject;

#pragma pack(pop)

static_assert(offsetof(Drone_tag, associatedSwitchChannel) == 0x119, "Wrong offset for associatedSwitchChannel");

static_assert(sizeof(maybeSAnimSkin) == (0x84), "Wrong size for sAnimSkin or similar");
static_assert(sizeof(DIVars_tag) == 0xa4, "DIVars is wrong size");
static_assert(sizeof(DCVars_tag) == 0x10, "DCVars is wrong size");

bool Drone_DCVfromOBJ(obj_tag* obj, DCVars_tag *dcVars);
obj_tag* Drone_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
void Drone_SM_RouteMsg(MsgObject *msg);
void Drone_EnableAll(char enable, HASHCODE hashcode);

#endif // DRONE_H_