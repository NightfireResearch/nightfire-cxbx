// The object types' functions (control.h)

#include "control.h"

#include "../../engine/Anim.h"
#include "../drone/Drone.h"
#include "../drone/DroneCollision.h"
#include "../mp/multiplayer.h"
#include "Apocalypse.h"
#include "BodyGlow.h"
#include "Break.h"
#include "CamSubject.h"
#include "Cloud.h"
#include "Copter.h"
#include "Corona.h"
#include "Creature.h"
#include "Destroy.h"
#include "Door.h"
#include "DroneSpawner.h"
#include "Drone_AIHints.h"
#include "DynamicObject.h"
#include "Effect.h"
#include "Emitter.h"
#include "Env.h"
#include "Explode.h"
#include "Flicker.h"
#include "FuseBox.h"
#include "GT.h"
#include "Gas.h"
#include "GunImp.h"
#include "Hint.h"
#include "Hurt.h"
#include "LeafGen.h"
#include "Lightning.h"
#include "Lock.h"
#include "Mine.h"
#include "MiniSub.h"
#include "Monitor.h"
#include "OneSided.h"
#include "PCQWorm.h"
#include "Pickup.h"
#include "Player.h"
#include "RigidBody.h"
#include "Ripples.h"
#include "Rotor.h"
#include "SS.h"
#include "ScriptPlayer.h"
#include "Searchlight.h"
#include "Sensor.h"
#include "Shooter.h"
#include "SpaceLaser.h"
#include "SpaceMissile.h"
#include "Sub.h"
#include "Switch.h"
#include "Swoosh.h"
#include "ThirdIcon.h"
#include "Trigger.h"
#include "bullet.h"
#include "car.h"

// The game's empty Delete and Collide functions, which the linker made one
static void SHARED_EMPTY(obj_tag *obj) {}

// By ObjectType
// XBE_GLOBAL(0x00163b98, 0x3fc) readonly relocate
ControlFunction control_funcs[OBJECTTYPE_FUNCS] = {
    {NULL, NULL, NULL},                                                    // 0
    {NULL, NULL, NULL},                                                    // 1 OBJECTTYPE_GFX
    {Drone_Control, Drone_CollisionHandler, Drone_Delete},                 // 2 OBJECTTYPE_DRONE
    {Player_Update, Player_CollisionHandler, NULL},                        // 3 OBJECTTYPE_PLAYER
    {NULL, NULL, NULL},                                                    // 4
    {Bullet_Update, Bullet_CollisionHandler, Bullet_Delete},               // 5 OBJECTTYPE_BULLET
    {Bubble_Update, NULL, NULL},                                           // 6
    {rotor_update, NULL, NULL},                                            // 7 OBJECTTYPE_ROTOR
    {NULL, NULL, NULL},                                                    // 8
    {NULL, NULL, NULL},                                                    // 9
    {NULL, NULL, NULL},                                                    // 10
    {NULL, NULL, NULL},                                                    // 11
    {Particles_Update, NULL, NULL},                                        // 12 OBJECTTYPE_PARTICLES
    {Bullet_casing_update, NULL, NULL},                                    // 13 OBJECTTYPE_CASING
    {gases_smoke, NULL, NULL},                                             // 14 OBJECTTYPE_GAS
    {Explode_Update, Explode_CollisionHandler, Explode_Delete},            // 15 OBJECTTYPE_EXPLODE
    {Debris_Update, NULL, NULL},                                           // 16 OBJECTTYPE_EFFECT
    {Drone_Control, NULL, Drone_Delete},                                   // 17 OBJECTTYPE_DEAD_DRONE
    {Player_Update, Player_CollisionHandler, NULL},                        // 18 OBJECTTYPE_DEAD_PLAYER
    {NULL, NULL, NULL},                                                    // 19
    {NULL, NULL, NULL},                                                    // 20
    {(ObjectUpdateFunc)AnimObjectUpdateThunk, NULL, NULL},                         // 21 OBJECTTYPE_ANIMDEBUGHANDLER
    {NULL, NULL, NULL},                                                    // 22
    {NULL, NULL, NULL},                                                    // 23
    {NULL, NULL, NULL},                                                    // 24
    {Sunreflection_Update, NULL, NULL},                                    // 25
    {RB_Update, RB_CollisionHandler, NULL},                                // 26 OBJECTTYPE_RIGIDBODY
    {SP_Update, NULL, SP_Delete},                                          // 27 OBJECTTYPE_SCRIPTPLAYER
    {Door_Update, Door_ReactToHit, NULL},                                  // 28 OBJECTTYPE_DOOR
    {Trigger_Update, NULL, NULL},                                          // 29 OBJECTTYPE_TRIGGER
    {NULL, NULL, NULL},                                                    // 30
    {(ObjectUpdateFunc)AnimObjectUpdateThunk, NULL, NULL},                         // 31
    {Break_Update, NULL, NULL},                                            // 32 OBJECTTYPE_BREAK
    {Destroy_Update, Destroy_CollisionHandler, NULL},                      // 33 OBJECTTYPE_DESTROY
    {Searchlight_Update, NULL, NULL},                                      // 34 OBJECTTYPE_SPOTLIGHT
    {Creature_Update, NULL, Creature_Delete},                              // 35 OBJECTTYPE_CREATURE
    {cloud_update, NULL, NULL},                                            // 36 OBJECTTYPE_CLOUD
    {SS_Update, NULL, NULL},                                               // 37 OBJECTTYPE_SIMPLESCRIPT
    {Flicker_Update, NULL, NULL},                                          // 38 OBJECTTYPE_FLICKER
    {Hurt_Update, NULL, NULL},                                             // 39 OBJECTTYPE_HURT
    {Car_Update, Car_CollisionHandler, Car_Delete},                        // 40 OBJECTTYPE_CAR
    {NULL, NULL, NULL},                                                    // 41
    {Occlude_Update, NULL, NULL},                                          // 42 OBJECTTYPE_OCCLUDE
    {NULL, NULL, NULL},                                                    // 43 OBJECTTYPE_CREEPWALL
    {NULL, NULL, NULL},                                                    // 44 OBJECTTYPE_THIRDCAM_OR_WIRE
    {NULL, NULL, NULL},                                                    // 45 OBJECTTYPE_LADDER
    {Leaf_Update, NULL, NULL},                                             // 46 OBJECTTYPE_LEAF
    {Pickup_Update, NULL, NULL},                                           // 47 OBJECTTYPE_PICKUP
    {DroneSpawner_Control, NULL, DroneSpawner_Delete},                     // 48 OBJECTTYPE_DRONE_SPAWNER
    {LeafGen_Update, NULL, NULL},                                          // 49 OBJECTTYPE_LEAFGEN
    {Emitter_Update, NULL, SHARED_EMPTY},                                  // 50 OBJECTTYPE_EMITTER
    {Lightning_Update, NULL, NULL},                                        // 51 OBJECTTYPE_LIGHTNING
    {Ripples_Update, NULL, NULL},                                          // 52 OBJECTTYPE_RIPPLES
    {MP_ObjectUpdate, NULL, SHARED_EMPTY},                                 // 53 OBJECTTYPE_MPOBJECT
    {GT_Update, NULL, GT_Delete},                                          // 54 OBJECTTYPE_GUNTURRET
    {Sensor_Update, NULL, NULL},                                           // 55 OBJECTTYPE_SENSOR
    {Monitor_Update, NULL, NULL},                                          // 56 OBJECTTYPE_MONITOR
    {Switch_Update, NULL, NULL},                                           // 57 OBJECTTYPE_SWITCH
    {Lock_Update, NULL, NULL},                                             // 58 OBJECTTYPE_LOCK
    {Copter_Update, NULL, Copter_Delete},                                  // 59 OBJECTTYPE_COPTER
    {FuseBox_Update, NULL, NULL},                                          // 60 OBJECTTYPE_FUSEBOX
    {NULL, NULL, Discrete_Delete},                                         // 61 OBJECTTYPE_ANIMOBJECT
    {NULL, NULL, Drone_AIVolume_Delete},                                   // 62 OBJECTTYPE_DRONE_AIVOLUME
    {Env_Tree_Update, NULL, NULL},                                         // 63 OBJECTTYPE_TREE
    {SoundTrigger_Update, NULL, NULL},                                     // 64 OBJECTTYPE_SOUNDTRIGGER
    {Swoosh_Update, NULL, NULL},                                           // 65 OBJECTTYPE_SWOOSH
    {NULL, ThirdIcon_Update, NULL},                                        // 66 OBJECTTYPE_THIRDICON
    {Hint_Update, NULL, NULL},                                             // 67 OBJECTTYPE_HINT
    {MusicTrigger_Update, NULL, NULL},                                     // 68 OBJECTTYPE_MUSIC_TRIGGER
    {Corona_Update, NULL, NULL},                                           // 69 OBJECTTYPE_CORONA
    {BodyGlow_Update, NULL, NULL},                                         // 70 OBJECTTYPE_BODYGLOW
    {GunImp_Update, NULL, NULL},                                           // 71 OBJECTTYPE_GUNTURRET2
    {CamSubject_Update, NULL, NULL},                                       // 72 OBJECTTYPE_CAMSUBJECT
    {PCQWorm_Update, NULL, NULL},                                          // 73 OBJECTTYPE_QWORM
    {Sub_Update, SHARED_EMPTY, SHARED_EMPTY},                              // 74 OBJECTTYPE_SUB
    {Mine_Update, NULL, NULL},                                             // 75 OBJECTTYPE_MINE
    {OneSided_Update, NULL, NULL},                                         // 76 OBJECTTYPE_ONESIDED
    {MiniSub_Update, SHARED_EMPTY, SHARED_EMPTY},                          // 77 OBJECTTYPE_MINISUB
    {Shooter_Update, NULL, NULL},                                          // 78 OBJECTTYPE_SHOOTER
    {SpaceMissile_Update, NULL, NULL},                                     // 79 OBJECTTYPE_SPACEMISSILE
    {SpaceLaser_Update, NULL, NULL},                                       // 80 OBJECTTYPE_SPACELASER
    {NULL, NULL, NULL},                                                    // 81 OBJECTTYPE_GRAPPLE
    {DynamicObject_Update, NULL, NULL},                                    // 82 OBJECTTYPE_DYNAMICOBJECT
    {InverterTrigger_Update, NULL, NULL},                                  // 83 OBJECTTYPE_INVERTER_TRIGGER
    {Apocalypse_Update, NULL, NULL},                                       // 84 OBJECTTYPE_APOCALYPSE
};
