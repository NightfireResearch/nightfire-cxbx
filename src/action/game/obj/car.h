#ifndef CAR_H_
#define CAR_H_

#include "../../engine/Collide.h"
#include "../../../common/xbeClass.h"

// obj_tag::curState of a car (OBJECTTYPE_CAR). Its subState is the number of the player driving it, and once it is
// destroyed the frames until it is back.
enum {
    CAR_IDLE = 0,
    CAR_DRIVEN = 1,
    CAR_STOPPED = 2,        // no update or collision; the car's own code never sets it
    CAR_TAKEN = 5,          // Car_Activate's: driven from the next update
    CAR_DESTROYED = 99,
};

// CAR_INFO::fireSide: the side the helicopter fires from next is the right while the bit is clear
enum {
    HELI_GUN_LEFT = 0x01,
    HELI_MISSILE_LEFT = 0x02,
};

#pragma pack(push, 1)

// A multiplayer remote-controlled vehicle's extra data: a tank, or a helicopter (isHeli)
typedef struct {
    HITTEST_tag hitTest;            // 0x00 - its sphere, for the collision update
    _VECTOR velocity;               // 0x8c - in its own space, a frame
    _VECTOR fall;                   // 0x98 - the tank's, in the world: gravity, at most 45 a second each way
    quaternion_tag orientation;     // 0xa4 - the tank's: level with the floor it last stood on; it turns towards it
    _VECTOR aim;                    // 0xb4 - the tank's turret: its pitch (x) and heading (y)
    obj_tag *turretGeom;            // 0xc0 - the tank's turret, the helicopter's blades
    obj_tag *barrelGeom;            // 0xc4 - the tank's barrel, the helicopter's propeller
    obj_tag *bodyGeom;              // 0xc8 - never set
    obj_tag *playerController;      // 0xcc - the player driving it (see Car_PlayerHasDied)
    obj_tag *lastController;        // 0xd0
    float turnSpeed;                // 0xd4 - the tank's, a frame
    float damageAmt;                // 0xd8 - explosions add their damage to it (Explode_Propagate)
    float gunWait;                  // 0xdc - frames before the gun fires again
    float mainWait;                 // 0xe0 - frames before the tank's shell or the helicopter's missile
    float tankMachinegunTemperature; // 0xe4 - 10 a shot, cooling a frame's worth a frame; past 100 it overheats
    DYNAMICSOUNDS *soundHandle;     // 0xe8 - its engine
    char onGround;                  // 0xec - the tank: its collision found a floor under it
    char _pad_ed;
    short tankNum;                  // 0xee - its slot in the spawns
    short mainAmmo;                 // 0xf0
    char isHeli;                    // 0xf2 - from MPSettings.MiniVehiclesEnabled: tanks, helicopters or either
    char machineGunOverheated;      // 0xf3
    uchar fireSide;                 // 0xf4 - the helicopter's: HELI_GUN_LEFT, HELI_MISSILE_LEFT
    char _pad_f5[3];
} CAR_INFO;

#pragma pack(pop)

LAYOUT_32(sizeof(CAR_INFO) == 0xf8, "Size of CAR_INFO not correct");
LAYOUT_32(offsetof(CAR_INFO, turretGeom) == 0xc0, "Offset of turretGeom not correct");
LAYOUT_32(offsetof(CAR_INFO, turnSpeed) == 0xd4, "Offset of turnSpeed not correct");
LAYOUT_32(offsetof(CAR_INFO, tankMachinegunTemperature) == 0xe4, "Offset of tankMachinegunTemperature not correct");
LAYOUT_32(offsetof(CAR_INFO, onGround) == 0xec, "Offset of onGround not correct");
LAYOUT_32(offsetof(CAR_INFO, tankNum) == 0xee, "Offset of tankNum not correct");
LAYOUT_32(offsetof(CAR_INFO, isHeli) == 0xf2, "Offset of isHeli not correct");
LAYOUT_32(offsetof(CAR_INFO, fireSide) == 0xf4, "Offset of fireSide not correct");

void Car_Init(void);
void Car_Reset(void);
obj_tag * Car_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *level);
void Car_PlayerHasDied(obj_tag *player);
void Car_Activate(obj_tag* carObj, obj_tag* playerObj);
void Car_Deactivate(obj_tag *carObj);
void Car_Update(obj_tag *obj);
void Car_CollisionHandler(obj_tag *obj);
void Car_Delete(obj_tag *obj);
void Car_MaybeHandleDamage(obj_tag *obj);
// The register-argument originals' adaptors (AUTOLTCG), and the cores behind them
void _Car_SetBits(CAR_INFO *car, obj_tag *obj);
void Car_SetBits(void);             // ESI the car's data; the car
bool _Car_InheritVelocity(obj_tag *car, obj_tag *by);
bool Car_InheritVelocity(void);     // ECX what it touched; the car

#endif // CAR_H_
