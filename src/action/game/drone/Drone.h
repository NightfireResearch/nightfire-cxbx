#ifndef DRONE_H_
#define DRONE_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

typedef struct DCVars_tag {
    obj_tag* gameObj;
    Drone_tag* drone;
    cel_tag* cel;
    void* aiStateMachine;
} DCVars_tag;

typedef struct Drone_tag {
    char unknown[0xec];
    void* aiStateMachine;
    char unknown2[0x1000]; //?
} Drone_tag;

#pragma pack(pop)

bool Drone_DCVfromOBJ(obj_tag* obj, DCVars_tag *dcVars);


#endif // DRONE_H_