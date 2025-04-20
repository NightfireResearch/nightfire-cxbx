#ifndef CAR_H_
#define CAR_H_


#pragma pack(push, 1)

typedef struct {
    char _pad_1[0x8c];
    _VECTOR someVector_8c; // 0x8C
    _VECTOR someVector_98; // 0x98
    quaternion_tag orientation; // 0xA4
    _VECTOR someVector_b4; // 0xB4
    obj_tag *turretGeom; // 0xC0
    obj_tag *barrelGeom; // 0xC4
    obj_tag *bodyGeom;   // 0xC8 -- Possibly redundant? Or is this the main tank body? Or special destroyed variant?
    obj_tag* playerController; // 0xCC (see Car_PlayerHasDied)
    obj_tag* lastController; // 0xD0
    char _pad_2[4];
    unsigned int damageAmt; // 0xD8
    float _unknown_0xdc;
    float _unknown_0xe0;
    float tankMachinegunTemperature; // 0xE4
    uint soundHandle; // 0xE8
    char _pad_3[2];
    short tankNum; // 0xEE
    short mainAmmo; // 0xF0
    char isHeli; // 0xF2 - this could be Tank vs Helicopter? Would make sense as the behaviour changes depending on multiple options for "miniVehiclesEnabled" - could be None, Tanks, Helicopters, Both
    char machineGunOverheated;
    char _pad_5[0x4];
} CAR_INFO;

#pragma pack(pop)

// Way to see the size or offset of a struct at compile time (will error out, and reveal the size of kaboom - which is our size/offset)
//char (*__kaboom)[offsetof(CAR_INFO,tankMachinegunTemperature)] = 1;

// Test against Xbox compiled code; other platforms may differ
static_assert(sizeof(CAR_INFO) == 0xf8, "Size of CAR_INFO not correct");
static_assert(offsetof(CAR_INFO, turretGeom) == 0xc0, "Offset of turretGeom not correct");
static_assert(offsetof(CAR_INFO, tankMachinegunTemperature) == 0xe4, "Offset of tankMachinegunTemperature not correct");
static_assert(offsetof(CAR_INFO, tankNum) == 0xee, "Offset of tankNum not correct");
static_assert(offsetof(CAR_INFO, isHeli) == 0xf2, "Offset of isHeli not correct");


void Car_Init(void);
obj_tag * Car_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *level);
void Car_PlayerHasDied(obj_tag *player);
void Car_Activate(obj_tag* carObj, obj_tag* playerObj);
void Car_Deactivate(obj_tag *carObj);

#endif // CAR_H_