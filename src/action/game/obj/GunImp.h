#ifndef GUNIMP_H_
#define GUNIMP_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A gun emplacement's extra data (GunImp_Create allocates it). Fields named from GunImp_Create, GunImp_Update,
// GunImp_Activate and GunImp_Deactivate; the rest are not traced yet.
typedef struct {
    obj_tag *gun;                // 0x00 - the part that turns, a second object with the gun's model
    obj_tag *user;               // 0x04 - the player on it, or NULL
    _MATRIX placement;           // 0x08 - where it was placed; GunImp_Update turns the gun from here
    char unknown44[0x50 - 0x44];
    _VECTOR userOffset;          // 0x50 - where the player stands, in the gun's frame
    float pitchTarget;           // 0x5c - where the stick has put the aim
    float yawTarget;             // 0x60
    float unknown64;             // 0x64 - zeroed every frame by GunImp_Update
    float pitch;                 // 0x68 - the aim, eased towards the targets
    float yaw;                   // 0x6c
    char unknown70[4];
    float heat;                  // 0x74 - rises while firing; overheated past 200
    float refireTimer;           // 0x78
    DYNAMICSOUNDS *firingSound;  // 0x7c
    char overheated;             // 0x80
    uchar gunType;               // 0x81 - row of the gun table at 0x00163a30 (0x1c bytes each)
    char unknown82[2];
} GUNIMP;

static_assert(sizeof(GUNIMP) == 0x84, "Bad size for GUNIMP");
static_assert(offsetof(GUNIMP, userOffset) == 0x50, "Bad offset of userOffset");
static_assert(offsetof(GUNIMP, firingSound) == 0x7c, "Bad offset of firingSound");
static_assert(offsetof(GUNIMP, gunType) == 0x81, "Bad offset of gunType");

#pragma pack(pop)

void GunImp_Activate(obj_tag *gunObj, obj_tag *player);
void GunImp_Deactivate(obj_tag *gunObj);
obj_tag * GunImp_Create(_VECTOR *pos, quaternion_tag *quat, celglist_tag *celgl);

#endif // GUNIMP_H_
