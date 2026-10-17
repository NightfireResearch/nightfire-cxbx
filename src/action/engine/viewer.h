
#include "../actionhelpers.h"

#include "../math/math.h"
#include "../util/Stack.h"

#pragma pack(push, 1)
struct viewer_tag { /* Size confirmed in build_alloc_viewer */
    struct world_tag *world;
    cel_tag* someCel;
    undefined4 field2_0x8;
    undefined field3_0xc;
    undefined field4_0xd;
    undefined field5_0xe;
    undefined field6_0xf;
    undefined field7_0x10;
    undefined field8_0x11;
    undefined field9_0x12;
    undefined field10_0x13;
    ushort field11_0x14;
    ushort field12_0x16;
    ushort field13_0x18;
    short field14_0x1a;
    undefined field15_0x1c;
    undefined field16_0x1d;
    undefined field17_0x1e;
    undefined field18_0x1f;
    undefined field19_0x20;
    undefined field20_0x21;
    undefined field21_0x22;
    undefined field22_0x23;
    undefined4 camMode;       // 0x24 - the player's camera mode, copied every frame (Player_CollisionHandler)
    undefined1 idx; /* Created by retype action */
    undefined field25_0x29;
    undefined1 field26_0x2a;
    undefined1 cameraUpdated;
    STACKINFO stateStack;
    int someStackMem;
    undefined field30_0x38;
    undefined field31_0x39;
    undefined field32_0x3a;
    undefined field33_0x3b;
    undefined field34_0x3c;
    undefined field35_0x3d;
    undefined field36_0x3e;
    undefined field37_0x3f;
    undefined field38_0x40;
    undefined field39_0x41;
    undefined field40_0x42;
    undefined field41_0x43;
    undefined field42_0x44;
    undefined field43_0x45;
    undefined field44_0x46;
    undefined field45_0x47;
    undefined field46_0x48;
    undefined field47_0x49;
    undefined field48_0x4a;
    undefined field49_0x4b;
    undefined field50_0x4c;
    undefined field51_0x4d;
    undefined field52_0x4e;
    undefined field53_0x4f;
    undefined field54_0x50;
    undefined field55_0x51;
    undefined field56_0x52;
    undefined field57_0x53;
    undefined field58_0x54;
    undefined field59_0x55;
    undefined field60_0x56;
    undefined field61_0x57;
    undefined field62_0x58;
    undefined field63_0x59;
    undefined field64_0x5a;
    undefined field65_0x5b;
    undefined field66_0x5c;
    undefined field67_0x5d;
    undefined field68_0x5e;
    undefined field69_0x5f;
    undefined field70_0x60;
    undefined field71_0x61;
    undefined field72_0x62;
    undefined field73_0x63;
    obj_tag *cameraUpdateObj;
    cameraUpdateFunc cameraUpdateCallback;
    plane_equ_tag frustumPlanes[4];
    undefined field140_0xac;
    undefined field141_0xad;
    undefined field142_0xae;
    undefined field143_0xaf;
    undefined field144_0xb0;
    undefined field145_0xb1;
    undefined field146_0xb2;
    undefined field147_0xb3;
    undefined field148_0xb4;
    undefined field149_0xb5;
    undefined field150_0xb6;
    undefined field151_0xb7;
    _VECTOR pos; /* View? */
    _VECTOR field153_0xc4;
    _VECTOR someVec;
    float xMin;
    float xMax;
    float yMin;
    float yMax;
    float projectionScaleX;
    float projectionScaleY;
    float zoom;               // 0xf4 - the view is drawn at fovRadians / zoom; 1.0 unzoomed (Camera_CalcViewAngles)
    float width;
    float height;
    float AspectRatio;
    float fovRadians;
    _MATRIX viewMatrix;
    _MATRIX worldMatrix;
    _MATRIX mtx;
    float shearAngle;         // 0x1bc - Camera_Shear's wobble under water, kept within +-pi
    // camera_tracking (camera mode 0xb, on a wire or creep wall): the view's pitch and yaw about the player, the
    // distance it used this frame and the distance it should be (4 on a wire or wall, 0 off it), and the three
    // Player_CollCreepWall keeps from before
    float trackPitch;         // 0x1c0
    float trackYaw;           // 0x1c4
    float trackDistanceUsed;  // 0x1c8
    float trackDistance;      // 0x1cc
    float savedTrackPitch;    // 0x1d0
    float savedTrackYaw;      // 0x1d4
    float savedTrackDistance; // 0x1d8
    float apocalypseEffect;
    undefined4 field246_0x1e0;
    undefined4 field247_0x1e4;
    undefined4 field248_0x1e8;
    undefined4 field249_0x1ec;
    undefined4 field250_0x1f0;
    obj_tag *player;          // 0x1f4 - the player whose view it is (Player_Init)
    undefined4 field252_0x1f8;
    undefined4 field253_0x1fc;
    undefined1 field254_0x200;
    undefined1 field255_0x201;
    undefined1 field256_0x202;
    undefined1 field257_0x203;
    undefined1 field258_0x204;
    undefined1 fadeActive; // 0x205 - set to 1 by Camera_SetFade (guards it from stomping an in-progress fade);
                            // read by Script_Update to avoid double-triggering a scripted-camera-NIS fade-out
    undefined2 nightVisionRelated;
};

struct world_tag {
    cel_tag* firstCel;
    // ?
};

// An opening between two cels (0x7c bytes). Only the fields our code reads are named.
typedef struct portal_tag portal_tag;
struct portal_tag {
    portal_tag *next;           // 0x00 - the next portal of the same cel
    portal_tag *partner;        // 0x04 - the same opening, from the other cel
    cel_tag *cel;               // 0x08 - the cel on the other side (Collide_StraddleCels adds it)
    char _unknown0c[0x10 - 0x0c];
    plane_equ_tag plane;        // 0x10
    uint32_t stamp;             // 0x20 - GameState.collisionStamp when Collide_StraddleCels last visited it
    char _unknown24[0x28 - 0x24];
    _VECTOR corners[4];         // 0x28 - two triangles, through Intersect_Portal's index table
    _VECTOR centre;             // 0x58
    _VECTOR boxMin;             // 0x64
    _VECTOR boxMax;             // 0x70
};
static_assert(sizeof(portal_tag) == 0x7c, "portal_tag is 0x7c bytes");

struct cel_tag { /* Confirmed size in build_alloc_cel */
    undefined field0_0x0;
    undefined field1_0x1;
    undefined field2_0x2;
    undefined field3_0x3;
    undefined field4_0x4;
    undefined field5_0x5;
    undefined field6_0x6;
    undefined field7_0x7;
    obj_tag *firstObject;      // 0x08 - first object in this cel
    undefined field9_0xc;
    undefined field10_0xd;
    undefined field11_0xe;
    undefined field12_0xf;
    undefined field13_0x10;
    undefined field14_0x11;
    undefined field15_0x12;
    undefined field16_0x13;
    cel_tag *nextCel;
    cel_tag *childCel;                  // 0x18 - a cel's first cel within it, and a cel's next in the same one
                                        //        (Collide_Pick)
    undefined field28_0x1c;
    undefined field29_0x1d;
    undefined field30_0x1e;
    undefined field31_0x1f;
    undefined field32_0x20;
    undefined field33_0x21;
    undefined field34_0x22;
    undefined field35_0x23;
    struct AINode_tag *aiNodes;         // 0x24 - the AI path nodes in this cel (AIPath_BindNodes)
    portal_tag *portals;                // 0x28 - this cel's portals, linked through portal_tag.next
    undefined field44_0x2c;
    undefined field45_0x2d;
    undefined field46_0x2e;
    undefined field47_0x2f;
    struct AIBoundNode_tag *boundNodes; // 0x30 - the AI bound nodes in this cel (AIBounds_Link2Cel)
    struct AIBoundLink_tag *boundLinks; // 0x34 - the AI bound links with an end in this cel (AIBounds_Prepare)
    celglist_tag *glist;                // 0x38 - the cel's geometry; NULL for none
    uint bitsFromPlacementTag;          // 0x3c - CEL_WORLD_SPACE: the geometry has no matrix (Collide_Intersect)
    _VECTOR someMin;
    _VECTOR someMax;
    _VECTOR position;                   // 0x58 - the geometry's matrix (Collide_Intersect): translation
    _VECTOR rotation;                   // 0x64 - and rotation
    _VECTOR sphere;                     // 0x70 - the bounding sphere Collide_Pick tests
    float sphereRadius;                 // 0x7c
    float shearHeight;                 // 0x80 - a drone more than 0.3 m below it does not fall (NDrone2_Fall)
    uint flags84;                      // 0x84 - CEL_ flags
    undefined field111_0x88;
    undefined field112_0x89;
    undefined field113_0x8a;
    undefined field114_0x8b;
    undefined field115_0x8c;
    undefined field116_0x8d;
    undefined field117_0x8e;
    undefined field118_0x8f;
    undefined addedToDraw;
    uchar musicEvent;                  // 0x91 - Player_Update sends it (Music_Event) every frame the player is in the cel
    undefined1 someSwitchChannel; /* Created by retype action */
    undefined field122_0x93;
};
static_assert(offsetof(cel_tag, childCel) == 0x18, "Bad offset of cel_tag.childCel");
static_assert(offsetof(cel_tag, glist) == 0x38, "Bad offset of cel_tag.glist");
static_assert(offsetof(cel_tag, position) == 0x58, "Bad offset of cel_tag.position");
static_assert(offsetof(cel_tag, sphereRadius) == 0x7c, "Bad offset of cel_tag.sphereRadius");
static_assert(offsetof(cel_tag, flags84) == 0x84, "Bad offset of cel_tag.flags84");

// cel_tag::bitsFromPlacementTag: the geometry is in world space, and Collide_Pick tests the cel's box (not its sphere)
#define CEL_WORLD_SPACE 0x08000000
// cel_tag::flags84
enum {
    CEL_NO_COLLISION = 0x20,        // Collide_Pick passes it by
    CEL_NO_CONTACT = 0x40,          // its hits are HIT_NO_CONTACT (Drone_FeetOnPoint does not stand on them)
    CEL_MASKABLE = 0x1000,          // a test with mask 0x20 passes it by
};



#pragma pack(pop)

