#ifndef PLAYER_H_
#define PLAYER_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

typedef enum {
    CamMode_Default = 0x00,
    CamMode_PostMPGameThirdPerson = 0x01,
    CamMode_Redeemer = 0x0c, // Redeemer = Sentinel Missile?
    CamMode_RemoteDeviceBehind = 0x09, // behind the remote device, from its rotation
    CamMode_FollowObject = 0x0a,       // following BLData::cameraFollowObject
    CamMode_CreepWall = 0x0b,
    CamMode_RCCar = 0x0d,
    CamMode_GunImp = 0x0e,   // on a gun emplacement (GunImp_Activate)
    CamMode_Ronin = 0x0f,
} CamMode;

#pragma pack(push, 1)

// One per base weapon (BLData::weaponStats, indexed by weapon_definition_tag::weaponBaseNum, or by the variant that
// holds the ammo: Player_AmmoIndex). Player_RamSave carries all of them from one part of a mission to the next.
typedef struct WeaponStatus {
    float zoom;           // 0x0 - the zoom this weapon was left at (Player_Zoom); sqrt(maxZoom) to start with
    short clipOrCooldown; // 0x4 - rounds loaded, or the charge of a weapon that recharges
    char enabled;         // 0x6 - the player has it
    // For a weapon with no alt-fire variant, Player_Weapon steps this on each alt-fire press and wraps it at
    // weapon_data[].numFireModes; it picks weapon_definition_tag::shotsPerTrigger (Player_WeaponFiring).
    char fireModeIndex;   // 0x7
    char variant;         // 0x8 - the variant last chosen, less the base (Player_WeaponSelect, Player_WeaponChange)
    char pad[3];
} WeaponStatus;

static_assert(sizeof(WeaponStatus) == 0xc, "WeaponStatus size wrong");
static_assert(offsetof(WeaponStatus, clipOrCooldown) == 0x4, "Offset of clipOrCooldown not correct");
static_assert(offsetof(WeaponStatus, fireModeIndex) == 0x7, "Offset of fireModeIndex not correct");
static_assert(offsetof(WeaponStatus, variant) == 0x8, "Offset of variant not correct");

// BLData::waterFlags
#define WATER_HEAD_WAS_UNDER 0x01   // the head went under and came back up
#define WATER_HEAD_UNDER     0x10   // the head is under water, or the player is on a wire

// WIP
// The multiplayer end-of-match camera's: it circles the target, above it, as far out as the walls allow
typedef struct EndCamera {
    _VECTOR target;         // 0x00 - the player's head (Player_PositionCamera)
    float range;            // 0x0c - how far out it would be, and how high above the target at most
    float distance;         // 0x10 - how far out and up it is
    float distanceWanted;   // 0x14 - range, or where a sphere at the view meets something
    float angle;            // 0x18 - round the target, 0.7 a second
    obj_tag *subject;       // 0x1c - the player it is on
} EndCamera;

typedef struct BLData {
    _VECTOR lastPosition; // 0x00 - Player_Update copies the object's position here every frame
    // This frame's movement, in the object's local frame. Player_Move fills it in, Player_Update rotates it
    // by the object's matrix and adds it to the position, then copies it to lastMovement for next time -
    // which is what makes lastMovement the state Player_Move smooths from.
    _VECTOR movement;     // 0x0c
    _VECTOR lastMovement; // 0x18
    // This frame's change in the object's rotation, not an angle: Player_Update zeroes it before the
    // movement handlers run and adds it to obj_tag::rotation afterwards. .y is the turn, and it goes
    // negative turning right. Anything wanting to steer the player adds to it in between - which is what
    // mouse look does, in Player_ViewClamping.
    _VECTOR rotationDelta; // 0x24
    _VECTOR lastRotationDelta; // 0x30 - Player_Update copies rotationDelta here once it is applied
    _VECTOR field_0x3c; // 0x3c - zeroed by GunImp_Deactivate and Player_SSScanMode
    _VECTOR muzzle;     // 0x48 - what the player's anim aims at in multiplayer (Player_StandAtNewPosition)
    // Zero G: the player turned upright (Player_ZeroG builds it on a press of reload), and the same as a
    // quaternion, which the player's matrix is slerped towards for zeroGTurn frames.
    _MATRIX zeroGUpright;       // 0x54
    quaternion_tag zeroGQuat;   // 0x90
    char _pad_0a0[0xac - 0xa0];
    short zeroGTurn;            // 0xac
    char _pad_0ae[0xb0 - 0xae];
    EndCamera endCamera; // 0xb0 - the multiplayer end-of-match camera's (Camera_LookAtFrom)
    float floorNormalY; // 0xd0 - the floor's slope: below 0.5 the player slides (Player_Collision)
    obj_tag *autoAimTarget; // 0xd4 - what Check_AutoAim last locked on to, whose cone is wider; NULL for none
    // Where the crosshair is aimed on the screen while aiming without a scope (Player_Aiming): within +-0.296 and
    // +-0.4; pushed past them, the view turns instead.
    float aimX; // 0xd8
    float aimY; // 0xdc
    float crosshairOffsetX; // 0xe0
    float crosshairOffsetY; // 0xe4
    float aimTurnX; // 0xe8 - zeroed by Player_ClearAimInertia
    float aimTurnY; // 0xec
    // Whether the game is easing the view pitch back towards pitchAutoLevelTarget. 0 and 1 mean it is not,
    // 1 specifically meaning "the player is aiming right now"; 2 and 3 mean it is. Player_Aiming sets this
    // to 1 on any frame the stick moves the view, which is what stops the easing fighting the player - see
    // Player_ClampSomeAngles, which is the other half of it, and Player_ViewClamping, which is where mouse
    // look has to do the same thing for the same reason.
    char aimAutoLevelState; // 0xf0
    // 1 walking slowly or standing, 2 walking fast (Player_UpdateWalkClass); a change sets aimAutoLevelState to 2
    // when the player's auto-centre option is on.
    char walkClass; // 0xf1
    char field_0xf2; // 0xf2 - zeroed by Player_InitWeapon
    char crosshairType; // 0xf3
    // The grapple (Player_InitGrapple clears all of it): where it caught, how fast it pulls, what it caught on, the
    // rope drawn to it, its state (GrappleState) and the frames before landing can end the pull.
    _VECTOR grapplePoint;   // 0xf4
    float grappleSpeed;     // 0x100
    obj_tag *grappleObj;    // 0x104
    obj_tag *rope;          // 0x108
    short grappleState;     // 0x10c
    short grappleWait;      // 0x10e
    // Non-zero while something else is driving the player - Player_Move takes the controls away entirely.
    char movementDisabled; // 0x110
    char _pad_11b[0x114-0x111];
    // How much of each ammo type the player carries, not counting what is loaded (weaponStats[].clipOrCooldown):
    // indexed by weapon_definition_tag::ammoType, capped at ammo_data[].maxNumPerPlayer.
    short ammo[34]; // 0x114
    WeaponStatus weaponStats[114]; // 0x158
    char keys[0x6e4-0x6b0]; // 0x6b0 - a byte for each key held (Player_EquipKey); Player_RamSave carries the first 0x33 bytes with the weapons
    // The capsule the player collides with, set up every frame by Player_Collision. Its hitType is 0 while
    // Player_Disable has the player out of play, 0x800 from Player_Enable.
    HITTEST_tag hitTest; // 0x6e4
    HUDINFO_tag* hudInfo; // 0x770
    char _pad_2222[4];
    obj_tag* weaponObject; // 0x778 - the gun in the first-person view (its state: Player_SetWeaponAnimObj)
    // The beam drawing objects (Player_CreateBeams creates them): [0] the laser sight's and the laser's beam
    // (Player_LaserPointer, Player_SetupLaser), all of them the taser's jagged one (Player_SetupTaser)
    obj_tag* beamObjs[32]; // 0x77c
    obj_tag* sightObj; // 0x7fc - the laser sight's dot where it meets a surface (Player_LaserPointer)
    char _unknown[4];
    obj_tag* muzzleFlashObj; // 0x804
    obj_tag* remoteControlDevice; // 0x808
    obj_tag* cameraFollowObject; // 0x80c - what camera mode 10 follows (Player_PositionCamera)
    // What Check_Target found for the gadgets to work on: a Q-Worm's target, a lock (Player_WeaponFiring); cleared by
    // Player_Init
    obj_tag* gadgetTarget; // 0x810
    // The ladder (or wire, zipline...) the player is on, in the movement substates that attach him to one.
    // Name from the PS2 symbols.
    obj_tag* attachedToSpecialMovementItem; // 0x814
    // The player's alpha (obj_tag::tweakA, drawn at 40 below a half): eased up to 1 every frame (Player_Update)
    float alpha; // 0x818
    obj_tag *bodyGlow; // 0x81c - BodyGlow_Create's object for the player
    sAnimSet_tag *animSet; // 0x820 - the anim set PlayerAnimSetInit* chose; cleared on every change of substate
    float health; // 0x824 - used eg. by SP_Update to abort an in-progress NIS if the player has died
    DYNAMICSOUNDS *zoomSound; // 0x828 - the zoom motor's sound, stopped when the zoom stops (Player_Zoom, Player_ScopeOff)
    DYNAMICSOUNDS *waterSound; // 0x82c - the sound Player_MonitorAir starts when the head goes under
    // The ladder's ends as Player_Climb last worked them out: above ladderTop the player climbs off the top, below
    // ladderBottom he lets go.
    float ladderTop; // 0x830
    float ladderBottom; // 0x834
    // Where the player is looking vertically, as a fraction of a right angle rather than in radians, so
    // +-1 is straight up and straight down - which is exactly the range Player_ViewClamping enforces.
    float pitchFromHorizontal; // 0x838
    float pitchAutoLevelTarget; // 0x83c - what aimAutoLevelState above eases pitchFromHorizontal towards
    // Armour, which Player_RamSave carries between the parts of a mission beside health. Player_InitWeapon
    // takes it away at the start of Castle Indoors 1 and sets it to 50 on the space station levels.
    float armor; // 0x840
    float field_0x844; // 0x844 - zeroed by Player_Init
    float healthShown; // 0x848 - the health when Player_Init set it
    // HUD_UpdateHealthPane draws the armour and health bars at 4x this alpha, capped at 1, and sets it to 1 while
    // the game is paused - so the bars fade in over its first quarter. (Its writers were not traced.)
    float hudFadeIn; // 0x84c
    float air; // 0x850 - 0 to 100: falls while the head is under water outside zero G, and hurts below 1
    float underwater; // 0x854 - eased to 1 while the head is under water, back to 0 out of it
    uint waterFlags; // 0x858 - WATER_*
    float ropeFatigue; // 0x85c - 0 to 100 over 30 seconds of hanging from a rope; at 100 the player lets go
    // The weapon's zoom, 1 when not zoomed: Player_Zoom drives it, Player_PositionCamera hands it to the viewer
    // (viewer_tag::zoom), and the view is drawn at fovRadians / zoom.
    float zoom; // 0x860
    float zoomTarget; // 0x864 - 1 from Player_Init
    float zoomMax; // 0x868 - 1 from Player_Init
    // The gun's sway (Player_WeaponRecoil): the walk's phase, the kick of firing (decaying by a quarter a frame) and the
    // idle sway's phase
    float swayPhase; // 0x86c
    float recoilX; // 0x870
    float recoilY; // 0x874
    float idleSwayPhase; // 0x878
    // AccelFunc0's carried states: the scope's turn and pitch, the crosshair's turn and pitch (Player_Aiming), the turn
    // (Player_Move) and the pitch without aiming (Player_Aiming)
    float scopeAccelX; // 0x87c
    float scopeAccelY; // 0x880
    float aimAccelX; // 0x884
    float aimAccelY; // 0x888
    float turnAccelState; // 0x88c
    float lookAccel; // 0x890
    float ladderYaw; // 0x894 - the player's facing as he got onto the ladder (PlayerCollLadder)
    float headSwimOffset; // 0x898 - eased towards 0.7 while surfaced (Player_PositionCamera)
    float headCrouchOffset; // 0x89c - how far crouching has lowered the view, 0 to 0.45
    // Added to the turn every frame and decaying by a tenth a frame: -0.1 or 0.1 from hits by fists (weapon bases 0x63,
    // 0x65 and 0x66, Player_DealWithObjHit)
    float turnKick; // 0x8a0
    float visibility; // 0x8a4 - how well lit the player is, 0 to 1, halved crouching (Player_InShadow)
    // Player_SetFlashBang: the flash's length and what is left of it, and its blur.
    float flashTime; // 0x8a8
    float flashTimeLeft; // 0x8ac
    float nightVisionTimer; // 0x8b0
    float stepDistance; // 0x8b4 - set to 999 on landing from a jump
    float fallFrames; // 0x8b8 - time airborne: Player_CollisionHandler adds FRAME_RATE_MUL a tick, so it counts
                      //         60 Hz frames at any rate
    float refireTimer; // 0x8bc - frames before the gun fires again (Player_WeaponFiring)
    // The weapon and the gadget held last, which a press of the other cycle goes back to (Player_WeaponChange)
    short lastWeapon; // 0x8c0
    short lastGadget; // 0x8c2
    // The weapon the player was switching to when Player_RamSave ran at the end of the previous part of the
    // mission (it saves animState->switchingToWeaponId); Player_RamLoad puts it back here, and
    // Player_InitWeapon hands it back to animState->switchingToWeaponId.
    short ramLoadedWeaponId; // 0x8c4
    // Shots left in this pull of the trigger: weapon_definition_tag::shotsPerTrigger for the fire mode, counted down
    // as each is fired (Player_WeaponFiring)
    short burstShotsLeft; // 0x8c6
    short muzzleFlashFrames; // 0x8c8 - frames the muzzle flash shows: 3, 7 for weapon base 0x33 (Player_MuzzleFlash)
    // Player_SetWeaponAnimObj's idle anims: frames since any input with the gun idle (a fidget at 20 seconds), and
    // frames before the gun may be lowered (20 seconds, set again on any action)
    short idleTicks; // 0x8ca
    short field_0x8cc; // 0x8cc - zeroed by Player_StandAtNewPosition
    short idleWait; // 0x8ce
    ushort groundBits; // 0x8d0 - non-zero: the player has stood on something lately, and may jump
    short previousSubState; //0x8d2
    short remoteControlTimer; // 0x8d4 - frames before a remote-controlled device lets go, when set
    short pauseMenu; // 0x8d6 - the menu manager Player_Update opened for the pause menu
    // Frames left of the "Bond moment" icon (HT_Texture_gold007, the health pane's sprite 16): it spins in while
    // above 100, then sits at the top of the screen; HUD_UpdateHealthPane counts it down by FRAME_RATE_MUL.
    short bondMomentTimer; // 0x8d8
    // 0 while Player_Disable has the player out of play, 2 from Player_Enable; HUD_MonitorNightSight does
    // nothing at 0
    char active; // 0x8da
    char frozen; // 0x8db - set by Player_Disable(player, 1), cleared by Player_Enable
    char jumpState; // 0x8dc - JUMP_*: Player_HandleJump
    // The step of the substate on a wire, zipline or wall (Player_Wire, Player_Zipline, Player_Creep): which way
    // the player is moving, or the grab
    char traverseStep; // 0x8dd
    char playerNum; // 0x8de
    char playerNum2; // 0x8df - Player_Init sets it to the player number too
    char camMode; // 0x8e0 - CamMode
    // Frames before the player can catch on a ladder, wire or wall again after letting go (the frame rate, from
    // leaving a ladder); while it runs after leaving a wall, Player_Move does not strafe.
    char traverseWait; // 0x8e1
    char jumpWait; // 0x8e2 - frames of a jump before it can land
    char field_0x8e3; // 0x8e3 - zeroed by Player_StandAtNewPosition
    uchar field_0x8e4; // 0x8e4 - set to 0xff when the walking, crouching or zero G anim set is chosen
    char field_0x8e5; // 0x8e5 - zeroed by Player_Init
    char field_0x8e6; // 0x8e6 - zeroed by Player_InitWeapon
    char aimAutoLevelDelay; // 0x8e7 - frames before Player_ClampSomeAngles eases the pitch; zeroed by Player_ZeroG
    // Which hand fires next for twin guns, and which reload anim plays next for alternating ones (Player_SetFiringAnim,
    // Player_WeaponFiring); zeroed by Player_InitWeapon
    char twinGunHand; // 0x8e8
    char idleState; // 0x8e9 - IDLE_*: Player_SetWeaponAnimObj's idle anims
    char field_0x8ea; // 0x8ea - zeroed on every change of substate
    // A shot left this frame (Player_WeaponInitBullet; Player_WeaponFiring clears it): Player_CollisionHandler draws
    // the muzzle flash at muzzle
    char shotFired; // 0x8eb
    char bulletSpawned; // 0x8ec - this fire anim has spawned its bullet (weapon_definition_tag::bulletSpawnFrame)
    char taserHit; // 0x8ed - the taser's shot reached a drone or player in range (Player_WeaponInitBullet)
    char weaponChangeTimer; // 0x8ee - frames the ammo pane shows a new weapon's name (HUD_UpdateAmmoPane); zeroed by Player_StandAtNewPosition
    // The third-person action icon to show (index into HUD.cpp's ThirdIconSprites: grapple, wire, standing, ...),
    // 0xff for none
    uchar thirdIcon; // 0x8ef
    // Frames the anim set waits for a crouch or stand anim to finish before it is chosen again (the frame rate,
    // a second, from Player_ChangeSubState)
    char animWait; // 0x8f0
    char nightVisionActive; // 0x8f1
    char ambientBoost; // 0x8f2 - objects with effect flag 0x10000 are lit twice as bright (Lights_CalcAmbientLight)
    uchar flashBlur; // 0x8f3 - Player_SetFlashBang
    char laserSight; // 0x8f4 - the gun has a laser sight (weapon flag 8: Player_SetWeaponAnim)
    char punchSkin; // 0x8f5 - which of the fists' models the level's skin goes with (Player_Init)
    char field_0x8f6; // 0x8f6 - set to 8 on landing from a jump
    // Which sides the player was last hit from (bits 0-3: the health pane's full-screen overlays 20-23) and how
    // bright that flash still is; HUD_UpdateHealthPane fades it by FRAME_RATE_MUL a frame
    uchar hitDirections; // 0x8f7
    uchar hitDirectionFade; // 0x8f8
    char _pad_end[0x8fc-0x8f9];
} BLData;

LAYOUT_32(sizeof(BLData) == 0x8fc, "BLData is 0x8fc bytes (Player_Init)");

LAYOUT_32(offsetof(BLData, crosshairOffsetX) == 0xe0, "Offset of crosshairOffsetX not correct");
static_assert(offsetof(BLData, muzzle) == 0x48, "Offset of muzzle not correct");
static_assert(offsetof(BLData, zeroGQuat) == 0x90, "Offset of zeroGQuat not correct");
static_assert(offsetof(BLData, zeroGTurn) == 0xac, "Offset of zeroGTurn not correct");
LAYOUT_32(offsetof(BLData, aimTurnX) == 0xe8, "Offset of aimTurnX not correct");
LAYOUT_32(offsetof(BLData, walkClass) == 0xf1, "Offset of walkClass not correct");
LAYOUT_32(offsetof(BLData, grapplePoint) == 0xf4, "Offset of grapplePoint not correct");
LAYOUT_32(offsetof(BLData, rope) == 0x108, "Offset of rope not correct");
LAYOUT_32(offsetof(BLData, grappleWait) == 0x10e, "Offset of grappleWait not correct");
LAYOUT_32(offsetof(BLData, animSet) == 0x820, "Offset of animSet not correct");
LAYOUT_32(offsetof(BLData, waterSound) == 0x82c, "Offset of waterSound not correct");
LAYOUT_32(offsetof(BLData, ladderBottom) == 0x834, "Offset of ladderBottom not correct");
LAYOUT_32(offsetof(BLData, air) == 0x850, "Offset of air not correct");
LAYOUT_32(offsetof(BLData, ropeFatigue) == 0x85c, "Offset of ropeFatigue not correct");
LAYOUT_32(offsetof(BLData, traverseStep) == 0x8dd, "Offset of traverseStep not correct");
LAYOUT_32(offsetof(BLData, visibility) == 0x8a4, "Offset of visibility not correct");
LAYOUT_32(offsetof(BLData, stepDistance) == 0x8b4, "Offset of stepDistance not correct");
LAYOUT_32(offsetof(BLData, groundBits) == 0x8d0, "Offset of groundBits not correct");
LAYOUT_32(offsetof(BLData, field_0x8f6) == 0x8f6, "Offset of field_0x8f6 not correct");
LAYOUT_32(offsetof(BLData, field_0x8cc) == 0x8cc, "Offset of field_0x8cc not correct");
LAYOUT_32(offsetof(BLData, previousSubState) == 0x8d2, "Offset of previousSubState not correct");
LAYOUT_32(offsetof(BLData, field_0x8e3) == 0x8e3, "Offset of field_0x8e3 not correct");
LAYOUT_32(offsetof(BLData, field_0x8ea) == 0x8ea, "Offset of field_0x8ea not correct");
LAYOUT_32(offsetof(BLData, weaponChangeTimer) == 0x8ee, "Offset of weaponChangeTimer not correct");
LAYOUT_32(offsetof(BLData, animWait) == 0x8f0, "Offset of animWait not correct");
LAYOUT_32(offsetof(BLData, flashBlur) == 0x8f3, "Offset of flashBlur not correct");
LAYOUT_32(offsetof(BLData, gadgetTarget) == 0x810, "Offset of gadgetTarget not correct");
LAYOUT_32(offsetof(BLData, alpha) == 0x818, "Offset of alpha not correct");
LAYOUT_32(offsetof(BLData, bodyGlow) == 0x81c, "Offset of bodyGlow not correct");
LAYOUT_32(offsetof(BLData, healthShown) == 0x848, "Offset of healthShown not correct");
LAYOUT_32(offsetof(BLData, zoomMax) == 0x868, "Offset of zoomMax not correct");
LAYOUT_32(offsetof(BLData, turnKick) == 0x8a0, "Offset of turnKick not correct");
LAYOUT_32(offsetof(BLData, remoteControlTimer) == 0x8d4, "Offset of remoteControlTimer not correct");
LAYOUT_32(offsetof(BLData, pauseMenu) == 0x8d6, "Offset of pauseMenu not correct");
LAYOUT_32(offsetof(BLData, playerNum2) == 0x8df, "Offset of playerNum2 not correct");
LAYOUT_32(offsetof(BLData, field_0x8e5) == 0x8e5, "Offset of field_0x8e5 not correct");
LAYOUT_32(offsetof(BLData, shotFired) == 0x8eb, "Offset of shotFired not correct");
LAYOUT_32(offsetof(BLData, punchSkin) == 0x8f5, "Offset of punchSkin not correct");
LAYOUT_32(offsetof(BLData, hudInfo) == 0x770, "Offset of hudInfo not correct");
LAYOUT_32(offsetof(BLData, nightVisionActive) == 0x8f1, "Offset of nightVisionActive not correct");
LAYOUT_32(offsetof(BLData, nightVisionTimer) == 0x8b0, "Offset of nightVisionTimer not correct");
LAYOUT_32(offsetof(BLData, hudFadeIn) == 0x84c, "Offset of hudFadeIn not correct");
LAYOUT_32(offsetof(BLData, bondMomentTimer) == 0x8d8, "Offset of bondMomentTimer not correct");
LAYOUT_32(offsetof(BLData, thirdIcon) == 0x8ef, "Offset of thirdIcon not correct");
LAYOUT_32(offsetof(BLData, hitDirections) == 0x8f7, "Offset of hitDirections not correct");
LAYOUT_32(offsetof(BLData, hitDirectionFade) == 0x8f8, "Offset of hitDirectionFade not correct");
LAYOUT_32(offsetof(BLData, nightVisionActive) == 0x8f1, "Offset of nightVisionActive not correct");

//char (*__kaboom)[offsetof(BLData,playerNum)] = 1;
LAYOUT_32(offsetof(BLData, remoteControlDevice) == 0x808, "Offset of remoteControlDevice not correct");
static_assert(offsetof(BLData, field_0x3c) == 0x3c, "Offset of field_0x3c not correct");
static_assert(offsetof(BLData, endCamera) == 0xb0, "Offset of endCamera not correct");
LAYOUT_32(sizeof(EndCamera) == 0x20, "EndCamera is 0x20 bytes");
LAYOUT_32(offsetof(BLData, cameraFollowObject) == 0x80c, "Offset of cameraFollowObject not correct");
LAYOUT_32(offsetof(BLData, zoom) == 0x860, "Offset of zoom not correct");
LAYOUT_32(offsetof(BLData, headSwimOffset) == 0x898, "Offset of headSwimOffset not correct");
LAYOUT_32(offsetof(BLData, fallFrames) == 0x8b8, "Offset of fallFrames not correct");
LAYOUT_32(offsetof(BLData, weaponObject) == 0x778, "Offset of weaponObject not correct");
LAYOUT_32(offsetof(BLData, health) == 0x824, "Offset of health not correct");
LAYOUT_32(offsetof(BLData, hitTest) == 0x6e4, "Offset of hitTest not correct");
LAYOUT_32(offsetof(BLData, floorNormalY) == 0xd0, "Offset of floorNormalY not correct");
LAYOUT_32(offsetof(BLData, active) == 0x8da, "Offset of active not correct");
LAYOUT_32(offsetof(BLData, jumpState) == 0x8dc, "Offset of jumpState not correct");
LAYOUT_32(offsetof(BLData, jumpWait) == 0x8e2, "Offset of jumpWait not correct");
LAYOUT_32(offsetof(BLData, playerNum) == 0x8de, "Offset of playerNum not correct");
static_assert(offsetof(BLData, rotationDelta) == 0x24, "Offset of rotationDelta not correct");
static_assert(offsetof(BLData, movement) == 0x0c, "Offset of movement not correct");
static_assert(offsetof(BLData, lastMovement) == 0x18, "Offset of lastMovement not correct");
LAYOUT_32(offsetof(BLData, movementDisabled) == 0x110, "Offset of movementDisabled not correct");
LAYOUT_32(offsetof(BLData, ammo) == 0x114, "Offset of ammo not correct");
LAYOUT_32(offsetof(BLData, weaponStats) == 0x158, "Offset of weaponStats not correct");
LAYOUT_32(offsetof(BLData, turnAccelState) == 0x88c, "Offset of turnAccelState not correct");
LAYOUT_32(offsetof(BLData, traverseWait) == 0x8e1, "Offset of traverseWait not correct");
LAYOUT_32(offsetof(BLData, pitchFromHorizontal) == 0x838, "Offset of pitchFromHorizontal not correct");
LAYOUT_32(offsetof(BLData, pitchAutoLevelTarget) == 0x83c, "Offset of pitchAutoLevelTarget not correct");
LAYOUT_32(offsetof(BLData, aimAutoLevelState) == 0xf0, "Offset of aimAutoLevelState not correct");
LAYOUT_32(offsetof(BLData, autoAimTarget) == 0xd4, "Offset of autoAimTarget not correct");
LAYOUT_32(offsetof(BLData, aimY) == 0xdc, "Offset of aimY not correct");
LAYOUT_32(offsetof(BLData, scopeAccelX) == 0x87c, "Offset of scopeAccelX not correct");
LAYOUT_32(offsetof(BLData, lookAccel) == 0x890, "Offset of lookAccel not correct");
LAYOUT_32(offsetof(BLData, field_0xf2) == 0xf2, "Offset of field_0xf2 not correct");
LAYOUT_32(offsetof(BLData, weaponObject) == 0x778, "Offset of weaponObject not correct");
LAYOUT_32(offsetof(BLData, beamObjs) == 0x77c, "Offset of beamObjs not correct");
LAYOUT_32(offsetof(BLData, zoomSound) == 0x828, "Offset of zoomSound not correct");
LAYOUT_32(offsetof(BLData, attachedToSpecialMovementItem) == 0x814, "Offset of attachedToSpecialMovementItem not correct");
LAYOUT_32(offsetof(BLData, armor) == 0x840, "Offset of armor not correct");
LAYOUT_32(offsetof(BLData, swayPhase) == 0x86c, "Offset of swayPhase not correct");
LAYOUT_32(offsetof(BLData, refireTimer) == 0x8bc, "Offset of refireTimer not correct");
LAYOUT_32(offsetof(BLData, ramLoadedWeaponId) == 0x8c4, "Offset of ramLoadedWeaponId not correct");
LAYOUT_32(offsetof(BLData, burstShotsLeft) == 0x8c6, "Offset of burstShotsLeft not correct");
LAYOUT_32(offsetof(BLData, lastWeapon) == 0x8c0, "Offset of lastWeapon not correct");
LAYOUT_32(offsetof(BLData, muzzleFlashFrames) == 0x8c8, "Offset of muzzleFlashFrames not correct");
LAYOUT_32(offsetof(BLData, idleWait) == 0x8ce, "Offset of idleWait not correct");
LAYOUT_32(offsetof(BLData, idleState) == 0x8e9, "Offset of idleState not correct");
LAYOUT_32(offsetof(BLData, taserHit) == 0x8ed, "Offset of taserHit not correct");
LAYOUT_32(offsetof(BLData, laserSight) == 0x8f4, "Offset of laserSight not correct");
LAYOUT_32(offsetof(BLData, idleSwayPhase) == 0x878, "Offset of idleSwayPhase not correct");
LAYOUT_32(offsetof(BLData, keys) == 0x6b0, "Offset of keys not correct");
LAYOUT_32(offsetof(BLData, field_0x8e6) == 0x8e6, "Offset of field_0x8e6 not correct");
LAYOUT_32(offsetof(BLData, twinGunHand) == 0x8e8, "Offset of twinGunHand not correct");

typedef enum {
    MovementType_Walk = 0,
    MovementType_Climb = 1, // Ladder
    MovementType_Grapple = 2,
    MovementType_Swim = 3,
    MovementType_Crouch = 4,
    MovementType_ScanMode = 5, // flying freely, no collision (Player_SSScanMode; debug_scanmode, the fly cheat)
    MovementType_Wire = 6,
    MovementType_Creep = 7,
    MovementType_ZeroG = 8,
    MovementType_ZeroG_Anim = 9, // Ghidra's ZEROG_2 - the same movement, animated; Player_ZeroG handles both
    MovementType_RemoteControl = 10, // Steering the Sentinel's guided missile (Bullet_Update)
    MovementType_RCVehicle = 11,     // Driving an RC car or helicopter (Car_Activate, Car_Update)
    MovementType_Emplacement = 12,   // On a gun emplacement (GunImp_Activate, GunImp_Update)
    MovementType_Dead = 13,          // Player_CheckForDeath, out of water
    MovementType_DeadInWater = 14,   // Player_CheckForDeath, in water
    MovementType_Zipline = 15,
    MovementType_Ronin = 16,
} MovementType;

typedef struct {
    char unknown[0x2c];
    short requiredSwitch;
    char unknown2[0xb0-2-0x2c];
} level_tag_PlayerStartPosition;

typedef struct {
    _VECTOR pos;
    _VECTOR rot;
    char unknown[2];
    short isEnabled;
    level_tag_PlayerStartPosition levelData;
} PlayerStartPosition;

static_assert(sizeof(PlayerStartPosition) == 0xcc, "Bad size for PlayerStartPosition");

// BLData::thirdIcon: the HUD's third-person action icons (HUD.cpp's ThirdIconSprites)
#define THIRDICON_GRAPPLE  2
#define THIRDICON_WIRE     3
#define THIRDICON_STANDING 6
#define THIRDICON_NONE     0xff

#pragma pack(pop)

void Player_ResetStartPos(void);
void PlayerAnimSetInit(obj_tag *player);
void PlayerAnimSetInitNormal(obj_tag *player);
void PlayerAnimSetInitCrouch(obj_tag *player);
void PlayerAnimCrouch2Stand(obj_tag *player);
void PlayerAnimStand2Crouch(obj_tag *player);
void __stdcall ReadTuningVars(void);
void Player_ChangeState(obj_tag* obj, unsigned short newState);
int Player_ChangeSubState(obj_tag *player, ushort newState); // returns the previous substate
void Player_SetCamMode(BLData *param_1,unsigned short param_2);
void Player_Disable(obj_tag *player, char freeze);
void Player_WeaponNone(obj_tag *param_1);
void Player_PositionCamera(obj_tag *player);
void Player_ViewClamping(obj_tag *player);
void Player_Move(BLData *blData, obj_tag *player, float speedScale);
void Player_Weapon(obj_tag *player);
void Player_Enable(obj_tag *player, _MATRIX *mtx, int checkAttach);
void Player_Cam2Mode(ushort playerNum, uint mode);
void Player_SetHealth(BLData *obj, float health);
void Player_Kill(obj_tag *player);
bool PlayerMoveAllowed(obj_tag *player);
void Player_ScopeOff(obj_tag *player);
void Player_ResetZoom(obj_tag *player);
void Player_CheckWeaponsLoaded(BLData *blData);
short Player_AmmoIndex(short weaponIndex);
uint Player_EquipAmmo(BLData *playerInfo, short weaponIndex, short amount);
ushort Player_AmmoInGun(BLData *playerInfo, ushort weaponIndex);
void Player_CreateSight(obj_tag *playerObj, byte viewerNum);
void Player_CreateMuzzleFlash(obj_tag *playerObj, byte viewerNum);
void Player_SetupLaser(BLData *param_1, _VECTOR *targetPos);
void Player_SetupTaser(BLData *blData, _VECTOR *target);
void PlayerCollLadder(obj_tag *player, BLData *blData, obj_tag *ladder);
obj_tag* PlayerNotDrone_FeetOnPoint(obj_tag *obj, HITTEST_tag *test, float *floorNormalY, char dying);
void PlayerOrDrone_FeetOnPoint_DoCollision(obj_tag *obj, struct HITDATA_tag *hit);
void Player_Start(void);
void Player_AddNewStartPos(_VECTOR *pos, _VECTOR *rot, ushort enabled, level_tag *lvl);
void Player_GetHeadPos(_VECTOR *headPosOut, obj_tag *player, _MATRIX *param_3);
void Player_InitWeapon(BLData *blData, obj_tag *player);

void PositionBeam(obj_tag *param_1, _VECTOR *param_2, _VECTOR *param_3);

// Player.cpp
obj_tag* Player_Init(ushort playerNum, _VECTOR *pos, _VECTOR *rot, level_tag *spawnPointData);
void Player_StandAtNewPosition(obj_tag *player, _VECTOR *pos, _VECTOR *rot, ushort subState);
void Player_ClearInertia(obj_tag *player);
void Player_ClearAimInertia(obj_tag *player);
void Player_SetFlashBang(BLData *blData, float time, uchar blur);
bool Player_CheckForDeath(obj_tag *player, uint objectType);
void Player_Update(obj_tag *player);
void Player_InShadow(BLData *blData, obj_tag *player);
double AccelFunc0(float input, float *speed, float base, float mul, float steps, float deadZone, float fullZone);
double AccelFunc0Flag(float input, float *speed, float base, float mul, float steps, float deadZone, int full);
void Player_Aiming(obj_tag *player);
void Player_Collision(obj_tag *player);
float Player_DealWithObjHit(obj_tag *player, BLData *blData, HITDATA_tag *hit);
void Player_CollisionHandler(obj_tag *player);
void PlayerAnimSetInitDead(obj_tag *player);
void Player_UpdateWalkClass(obj_tag *player, float speed, float levelTarget);
int Player_InWater(obj_tag *player, BLData *blData);
void Player_HandlePain(obj_tag *player, float damage, BLData *blData, short hitLocation, ushort painType);
float Player_Hurt(obj_tag *player, float damage, _VECTOR *from);
void Player_HandleDeath(obj_tag *player);
void Player_SSWalk(obj_tag *player, BLData *blData);
void Player_SSCrouch(obj_tag *player, BLData *blData);
void Player_MonitorAir(obj_tag *player, BLData *blData);
void Player_HandleJump(BLData *blData, obj_tag *player, _VECTOR *gravity);
void Player_ZeroG(BLData *blData, obj_tag *player);
void Player_SSScanMode(BLData *blData, obj_tag *player);
uchar Player_Activate(BLData *blData, obj_tag *player);

// PlayerTraverse.cpp
void Player_LeaveLadder(obj_tag *player, BLData *blData);
void Player_Climb(BLData *blData, obj_tag *player);
void Player_SetGrapplePoint(BLData *blData, _VECTOR *point, obj_tag *grappleObj);
void Player_CollWire(obj_tag *player, BLData *blData, obj_tag *wire);
void Player_HangByHandA(obj_tag *player, _VECTOR *at);
void Player_HangByHandB(obj_tag *player, _VECTOR *at);
void Player_Wire(obj_tag *player);
void Player_Zipline(obj_tag *player);
void Player_CollCreepWall(obj_tag *player, BLData *blData, obj_tag *wall);
void Player_Creep(BLData *blData, obj_tag *player);
void Player_InitGrapple(BLData *blData, obj_tag *player);
void Player_SSGrapple(BLData *blData, obj_tag *player);
void Player_GrappleSetRope(obj_tag *player);
extern HASHCODE WireAnim; // the wire anim playing, which Player_Wire chooses the next one by

extern HASHCODE PunchSkin[4];

#endif // PLAYER_H_