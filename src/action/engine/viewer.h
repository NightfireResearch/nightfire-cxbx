
#include "../actionhelpers.h"
#include "../../common/xbeClass.h"

#include "../math/math.h"
#include "../util/Stack.h"

#pragma pack(push, 1)
// One thing for a view to draw: an object or a cel, sorted on key (View_DrawObjList)
struct DrawEntry {
    float key;                // the square of its distance from the view, negative to draw it back to front
    obj_tag *obj;
    cel_tag *cel;
};

// A view's three draw lists (opaque, sorted back to front, and last), each a ring of drawSizes entries in one block
struct ViewDrawLists {
    DrawEntry *lists[3];      // 0x08
    ushort counts[3];         // 0x14
    ushort sizes[3];          // 0x1a - powers of two
    uchar skyVisible;         // 0x20 - a cel drawn this frame shows the sky (vision_generate_display_list)
    undefined pad21[3];
};

// viewer_tag::fadeState
enum {
    VIEWFADE_NONE = 0,
    VIEWFADE_FADING = 1,
    VIEWFADE_DONE = 2,        // back to VIEWFADE_NONE the frame after
};

struct viewer_tag { /* Size confirmed in build_alloc_viewer */
    struct world_tag *world;
    cel_tag* cel;             // 0x04 - the cel holding pos (Camera_CheckLocation)
    ViewDrawLists draw;       // 0x08 - what View_CaptureScene gathers to draw, allocated while the view is enabled
    undefined4 camMode;       // 0x24 - the player's camera mode, copied every frame (Player_CollisionHandler)
    undefined1 idx; /* Created by retype action */
    undefined1 enabled;       // 0x29 - drawn (Camera_Enable)
    undefined1 spritesShown;  // 0x2a
    undefined1 cameraUpdated;
    STACKINFO stateStack;
    int stateStackMem[12];    // 0x34 - the stack's entries
    obj_tag *cameraUpdateObj;
    cameraUpdateFunc cameraUpdateCallback;
    plane_equ_tag frustumPlanes[4];
    _VECTOR move;             // 0xac - pos less prevPos, each frame (Camera_CheckLocation)
    _VECTOR pos; /* View? */
    _VECTOR prevPos;          // 0xc4 - where Camera_CheckLocation last placed the view in a cel
    _VECTOR rot;              // 0xd0 - pitch, yaw and roll (Camera_Set)
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
    float swingPhase;         // 0x1e0 - Camera_ApplySwing's, a turn every four seconds
    undefined4 field247_0x1e4;
    undefined4 field248_0x1e8;
    undefined4 field249_0x1ec;
    undefined4 field250_0x1f0;
    obj_tag *player;          // 0x1f4 - the player whose view it is (Player_Init)
    float fade;               // 0x1f8 - the fade (Camera_SetFade): 0 to 1
    float fadeRate;           // 0x1fc - added each frame (Camera_UpdateGlbVars)
    uchar fadeColour[4];      // 0x200 - red, green, blue and the alpha the fade gives
    undefined1 fadeHold;      // 0x204 - Camera_SetFade's last argument: no new fade while one runs
    undefined1 fadeState;     // 0x205 - VIEWFADE_
    undefined2 visionMode;    // 0x206 - VisionMode
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
    obj_tag *door;              // 0x0c - a door in the opening: closed, the vision does not look through
    plane_equ_tag plane;        // 0x10
    uint32_t stamp;             // 0x20 - GameState.collisionStamp when Collide_StraddleCels last visited it
    ushort unknown24;           // 0x24 - 0xffff from build_alloc_portal
    uchar seen;                 // 0x26 - looked through (Vision_Recurse), on both sides
    uchar open;                 // 0x27 - the vision may look through it
    _VECTOR corners[4];         // 0x28 - two triangles, through Intersect_Portal's index table
    _VECTOR centre;             // 0x58
    _VECTOR boxMin;             // 0x64
    _VECTOR boxMax;             // 0x70
};
LAYOUT_32(sizeof(portal_tag) == 0x7c, "portal_tag is 0x7c bytes");

struct cel_tag { /* Confirmed size in build_alloc_cel */
    void *unusedLinks[2];      // 0x00 - where an object has llPrev and llNext, so that firstObject and headPrev lie
                               //        where an object's nextInCel and prevInCel do
    obj_tag *firstObject;      // 0x08 - first object in this cel
    obj_tag *headPrev;         // 0x0c - kept NULL: the cel heads its objects' list as an object would (prevInCel)
    struct ATTACH_tag *attachments;     // 0x10 - the marks on it (Effect_Bullet, Effect_Player)
    cel_tag *nextCel;
    cel_tag *childCel;                  // 0x18 - a cel's first cel within it, and a cel's next in the same one
                                        //        (Collide_Pick)
    cel_tag *parentCel;                 // 0x1c - the room it is linked into (build_link_objects_to_rooms)
    cel_tag *nextVisible;               // 0x20 - the cels drawn this frame, from GfxList (vision_generate_display_list)
    struct AINode_tag *aiNodes;         // 0x24 - the AI path nodes in this cel (AIPath_BindNodes)
    portal_tag *portals;                // 0x28 - this cel's portals, linked through portal_tag.next
    struct ENV_tag *env;                // 0x2c - the cel's wind (Env_GetWindVel); NULL for none
    struct AIBoundNode_tag *boundNodes; // 0x30 - the AI bound nodes in this cel (AIBounds_Link2Cel)
    struct AIBoundLink_tag *boundLinks; // 0x34 - the AI bound links with an end in this cel (AIBounds_Prepare)
    celglist_tag *glist;                // 0x38 - the cel's geometry; NULL for none
    uint bitsFromPlacementTag;          // 0x3c - its CelKind, and CelPlacementFlags
    _VECTOR boxMin;                     // 0x40 - its box (parsemap: the model's extent)
    _VECTOR boxMax;                     // 0x4c - its y is the ceiling smoke rises to (gases_smoke)
    _VECTOR position;                   // 0x58 - the geometry's matrix (Collide_Intersect): translation
    _VECTOR rotation;                   // 0x64 - and rotation
    _VECTOR sphere;                     // 0x70 - the bounding sphere Collide_Pick tests
    float sphereRadius;                 // 0x7c
    float shearHeight;                 // 0x80 - a drone more than 0.3 m below it does not fall (NDrone2_Fall)
    uint flags;                        // 0x84 - CelFlags
    short reverb;                      // 0x88 - its reverb depth (SFXSetEnvironment), 0-100 or SFX_ENVIRONMENT_UNDERWATER;
                                       //        0x14 and more counts as outdoors (Sound_UpdateListeners)
    uchar ambientR[2];                 // 0x8a - the ambient light of objects in it, by its channel's state
    uchar ambientG[2];                 // 0x8c   (Lights_CalcAmbientLight)
    uchar ambientB[2];                 // 0x8e
    undefined addedToDraw;
    uchar musicEvent;                  // 0x91 - Player_Update sends it (Music_Event) every frame the player is in the cel
    undefined1 ambientChannel;         // 0x92 - the SwitchChannel choosing its ambient light
    uchar blink;                       // 0x93 - its alpha: bits 0-1 the mode, 4-7 the speed (View_SetupCelRenderModes)
};
LAYOUT_32(offsetof(cel_tag, childCel) == 0x18, "Bad offset of cel_tag.childCel");
LAYOUT_32(offsetof(cel_tag, nextVisible) == 0x20, "Bad offset of cel_tag.nextVisible");
LAYOUT_32(offsetof(cel_tag, glist) == 0x38, "Bad offset of cel_tag.glist");
LAYOUT_32(offsetof(cel_tag, position) == 0x58, "Bad offset of cel_tag.position");
LAYOUT_32(offsetof(cel_tag, sphereRadius) == 0x7c, "Bad offset of cel_tag.sphereRadius");
LAYOUT_32(offsetof(cel_tag, flags) == 0x84, "Bad offset of cel_tag.flags");
LAYOUT_32(offsetof(cel_tag, ambientB) == 0x8e, "Bad offset of cel_tag.ambientB");
LAYOUT_32(offsetof(cel_tag, reverb) == 0x88, "Bad offset of cel_tag.reverb");

// cel_tag::bitsFromPlacementTag's low word: the cel's kind (parseentityfixup_entity), from its placement's. Rooms
// and the other cels by our names; the objects' placements give CELKIND_8019.
enum CelKind {
    CELKIND_DD = 0x00dd,
    CELKIND_8017 = 0x8017,          // linked into a room, whose shearHeight it gives (build_link_objects_to_rooms)
    CELKIND_8018 = 0x8018,          // linked into a room; not drawn from a CELKIND_WATER_ROOM
    CELKIND_8019 = 0x8019,          // linked into a room; a placement of this kind gives cel_tag::blink
    CELKIND_803D = 0x803d,          // made CEL_20000 | CELKIND_8019
    CELKIND_WATER_ROOM = 0xc023,    // a room under water: its reverb, the fish
    CELKIND_ROOM = 0xc047,          // a room with two ambient lights, chosen by a switch channel
    CELKIND_MASK = 0xffff,
};

// cel_tag::bitsFromPlacementTag's high bits
enum CelPlacementFlags {
    CEL_20000 = 0x00020000,
    CEL_ROOM = 0x00040000,          // a room, an object's cel (build_FindCel)
    CEL_400000 = 0x00400000,        // bullets glance off it more often
    CEL_WORLD_SPACE = 0x08000000,   // the geometry is in world space, and Collide_Pick tests the cel's box
    CEL_NO_SKY = 0x10000000,        // the cel does not show the sky (vision_generate_display_list)
};

// cel_tag::flags: its model's flags, the same word as obj_tag::effectFlags (ObjectEffectFlags)
enum CelFlags {
    CEL_NO_COLLISION = 0x20,        // Collide_Pick passes it by
    CEL_NO_CONTACT = 0x40,          // its hits are HIT_NO_CONTACT (Drone_FeetOnPoint does not stand on them)
    CEL_MASKABLE = 0x1000,          // a test with HITMASK_SKIP_MASKABLE passes it by
    CEL_NOT_IN_VIEW_4 = 0x40000,    // not drawn in view 4's pass
};

// viewer_tag::visionMode
enum VisionMode {
    VISION_NORMAL,
    VISION_NIGHT,
    VISION_XRAY,
};

LAYOUT_32(sizeof(viewer_tag) == 0x208, "viewer_tag is 0x208 bytes");
LAYOUT_32(offsetof(viewer_tag, draw) == 0x08, "Bad offset of viewer_tag.draw");
LAYOUT_32(offsetof(viewer_tag, camMode) == 0x24, "Bad offset of viewer_tag.camMode");
LAYOUT_32(offsetof(viewer_tag, move) == 0xac, "Bad offset of viewer_tag.move");
LAYOUT_32(offsetof(viewer_tag, prevPos) == 0xc4, "Bad offset of viewer_tag.prevPos");
LAYOUT_32(offsetof(viewer_tag, viewMatrix) == 0x108, "Bad offset of viewer_tag.viewMatrix");
LAYOUT_32(offsetof(viewer_tag, mtx) == 0x180, "Bad offset of viewer_tag.mtx");
LAYOUT_32(offsetof(viewer_tag, swingPhase) == 0x1e0, "Bad offset of viewer_tag.swingPhase");
LAYOUT_32(offsetof(viewer_tag, fade) == 0x1f8, "Bad offset of viewer_tag.fade");
LAYOUT_32(offsetof(viewer_tag, visionMode) == 0x206, "Bad offset of viewer_tag.visionMode");

#pragma pack(pop)

