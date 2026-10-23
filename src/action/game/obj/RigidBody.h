#ifndef RIGIDBODY_H_
#define RIGIDBODY_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"
#include "../../engine/Collide.h"

#pragma pack(push, 1)

// RIGIDBODY::kind: 0 falls, rolls and collides as a body; the others hang from their position and swing (1 about x
// and z, 2 about x, 3 about y, 4 about z)
enum {
    RB_FREE = 0,
    RB_SWING_XZ = 1,
    RB_SWING_X = 2,
    RB_SWING_Y = 3,
    RB_SWING_Z = 4,
};

// A rigid body object's data (0x12c bytes, obj_tag::extraObjectData of type OBJECTTYPE_RIGIDBODY)
typedef struct RIGIDBODY {
    _VECTOR vel;                // 0x00
    _VECTOR omega;              // 0x0c - angular velocity
    _VECTOR accel;              // 0x18
    _VECTOR angAccel;           // 0x24
    _VECTOR impulse;            // 0x30 - the tick's force, applied and cleared by RB_EulerEvolve
    _VECTOR angImpulse;         // 0x3c - and torque
    _VECTOR *gravity;           // 0x48
    quaternion_tag quat;        // 0x4c - the orientation
    _MATRIX *matrix;            // 0x5c - the object's
    float mass;                 // 0x60
    float invMass;              // 0x64
    float inertia[3];           // 0x68
    float invInertia[3];        // 0x74 - of x, z and y, in that order (RB_Build), as RB_ApplyImpulses uses them
    float restitution;          // 0x80
    float halfHeight;           // 0x84 - |the model's box minimum y|
    float linDamp;              // 0x88 - time constant of the linear drag; 0 for none
    float angDamp;              // 0x8c - and of the angular drag
    ushort kind;                // 0x90 - RB_
    ushort testType;            // 0x92 - the HITTEST_ type of test while the body is active
    HITTEST_tag test;           // 0x94
    light_tag *light;           // 0x120
    uchar carriesLight;         // 0x124
    char _unknown125[3];
    uint sound;                 // 0x128 - Action_SFX of a bullet's hit; 0xffff for none
} RIGIDBODY;

LAYOUT_32(sizeof(RIGIDBODY) == 0x12c, "RIGIDBODY is 0x12c bytes");
LAYOUT_32(offsetof(RIGIDBODY, quat) == 0x4c, "Bad offset of RIGIDBODY.quat");
LAYOUT_32(offsetof(RIGIDBODY, kind) == 0x90, "Bad offset of RIGIDBODY.kind");
LAYOUT_32(offsetof(RIGIDBODY, test) == 0x94, "Bad offset of RIGIDBODY.test");
LAYOUT_32(offsetof(RIGIDBODY, light) == 0x120, "Bad offset of RIGIDBODY.light");

// The placement's keys for a rigid body (level_tag)
typedef struct RigidBodyPlacement {
    PlacementHeader header;     // 0x00
    uint mass;                  // 0x2c - 0 for 3
    char _unknown30[0x34 - 0x30];
    ushort kind;                // 0x34
    char _unknown36[0x38 - 0x36];
    uchar carriesLight;         // 0x38
    char _unknown39[0x3c - 0x39];
    uint sound;                 // 0x3c - 0 for none
} RigidBodyPlacement;

static_assert(offsetof(RigidBodyPlacement, sound) == 0x3c, "Bad offset of RigidBodyPlacement.sound");

#pragma pack(pop)

obj_tag* RB_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *lvl);
void RB_Update(obj_tag *obj);
void RB_CollisionHandler(obj_tag *obj);
void RB_Init(RIGIDBODY *rb, ushort kind);
bool RB_Build(RIGIDBODY *rb, _VECTOR *gravity, float *inertia, float mass);
void RB_AddImpulsiveForceAtPoint(RIGIDBODY *rb, _VECTOR *force, _VECTOR *point);
void RB_Drag(RIGIDBODY *rb, _VECTOR *accel, _VECTOR *angAccel, float dt);
void RB_Move(RIGIDBODY *rb, float dt);
void RB_ApplyImpulses(RIGIDBODY *rb);
void RB_RestingContactRot(obj_tag *obj, RIGIDBODY *rb, HITDATA_tag *hit);
void RB_CalcTorque(RIGIDBODY *rb, HITDATA_tag *hit);
void RB_EulerEvolve(RIGIDBODY *rb, float dt, int skipImpulses);
void RB_ApplyWind(RIGIDBODY *rb, obj_tag *obj);
void RB_RestingContact(obj_tag *obj, RIGIDBODY *rb, HITDATA_tag *hit);
void RB_CuboidMomentsOfInertia(float *out, float mass, float sizeX, float sizeY, float sizeZ);
void RB_ToWorld(_VECTOR *out, _VECTOR *v, _MATRIX *mtx);
void RB_ToBody(_VECTOR *out, _VECTOR *v, _MATRIX *mtx);
void RB_apply_omega(quaternion_tag *q, _VECTOR *omega, float dt);
void RB_VecSpecial(_VECTOR *out, float k, _VECTOR *a, _VECTOR *b, float s);

#endif // RIGIDBODY_H_
