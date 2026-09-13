#include "Sensor.h"

#include <math.h>

#include "../../engine/Collide.h"
#include "../../engine/viewer.h"
#include "../../sound/Sound.h"
#include "../../game/view.h"
#include "../sp/SwitchChannels.h"
#include "../Spline.h"

// Scanning cameras with conical beams, eg in evil base and office, but NOT the lasers which trigger the alarm - presumably some kind of Trigger instead

// Sensors are kept in CameraList, so the list node has to remain first.
#pragma pack(push, 1)
typedef struct SENSOR {
    LLNODE_tag cameraListNode;             // 0x00
    obj_tag* owner;                        // 0x08
    celglist_tag* enabledGraphics;         // 0x0c
    uint enabledGraphicsLod;               // 0x10
    quaternion_tag trackedRotation;        // 0x14
    void* sweepSpline;                     // 0x24
    float sweepPosition;                   // 0x28
    float sweepStep;                       // 0x2c
    uint sweepLastPoint;                   // 0x30
    uint viewConeHalfAngle;                // 0x34, in half-degrees
    float viewConeCos;                     // 0x38
    float viewConeSin;                     // 0x3c
    float beamLength;                      // 0x40
    float alertTimer;                      // 0x44
    float alertBeepTimer;                  // 0x48
    obj_tag* beamObject;                   // 0x4c
    obj_tag* bodyObject;                   // 0x50
    DYNAMICSOUNDS* loopSound;              // 0x54
    ushort alertSwitchChannel;             // 0x58
    ushort disableSwitchChannel;           // 0x5a
    ushort unknown4c;                      // 0x5c
    uchar unknown44;                       // 0x5e
    uchar dontAddToCameraList;             // 0x5f
    uchar childObjectVisibilityState;      // 0x60
    uchar padding[3];
} SENSOR;

static_assert(sizeof(SENSOR) == 0x64, "Wrong size for SENSOR");
static_assert(offsetof(SENSOR, beamObject) == 0x4c, "Bad offset of beamObject");
static_assert(offsetof(SENSOR, bodyObject) == 0x50, "Bad offset of bodyObject");

typedef struct {
    ushort unknown00;                      // 0x00
    ushort sweepPointCount;                // 0x02
    ushort sweepDuration;                  // 0x04
    uchar padding06[2];
    void* sweepSpline;                     // 0x08
    uchar padding0c[0x20];
    HASHCODE beamHashcode;                 // 0x2c
    uint viewConeHalfAngle;                // 0x30
    uint beamLength;                       // 0x34
    ushort alertSwitchChannel;             // 0x38
    uchar padding3a[2];
    ushort disableSwitchChannel;           // 0x3c
    uchar padding3e[2];
    uchar dontAddToCameraList;             // 0x40
    uchar padding41[3];
    uchar unknown44;                       // 0x44
    uchar padding45[3];
    HASHCODE bodyHashcode;                 // 0x48
    ushort unknown4c;                      // 0x4c
    uchar padding4e[2];
} Create_Sensor_Params;

#pragma pack(pop)

static_assert(offsetof(Create_Sensor_Params, beamHashcode) == 0x2c, "Bad offset of beamHashcode");
static_assert(offsetof(Create_Sensor_Params, bodyHashcode) == 0x48, "Bad offset of bodyHashcode");
static_assert(sizeof(Create_Sensor_Params) == 0x50, "Wrong size for Create_Sensor_Params");

#define CameraList (*(LLISTINFO_tag*)0x0029aabc)
#define SwitchList (*(LLISTINFO_tag*)0x0029aab0)

// The map stores this angle in half-degrees; pi / 360 converts it to radians.
static constexpr float HALF_DEGREES_TO_RADIANS = 0.008726646f;

// AUTOINJECT
void Sensor_Init(void) {
    LList_Init(&CameraList, 100, 0);
    LList_Init(&SwitchList, 28, 0);
}

// AUTOINJECT
obj_tag* Sensor_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {

    Create_Sensor_Params* createParams = (Create_Sensor_Params*)lvl;

    obj_tag* obj = control_create_object(sizeof(SENSOR), pos, rot, NULL);

    if(obj == NULL)
        return NULL;

    SENSOR* sensor = (SENSOR*)obj->extraObjectData;

    sensor->viewConeHalfAngle = createParams->viewConeHalfAngle;
    sensor->viewConeCos = cosf((float)sensor->viewConeHalfAngle * HALF_DEGREES_TO_RADIANS);
    sensor->viewConeSin = sinf((float)sensor->viewConeHalfAngle * HALF_DEGREES_TO_RADIANS);
    sensor->beamLength = (float)createParams->beamLength;
    sensor->alertSwitchChannel = createParams->alertSwitchChannel;
    sensor->disableSwitchChannel = createParams->disableSwitchChannel;
    sensor->dontAddToCameraList = createParams->dontAddToCameraList;
    sensor->unknown44 = createParams->unknown44; // Unused?
    sensor->unknown4c = createParams->unknown4c; // Unused?

    sensor->sweepPosition = 0.0f;
    sensor->sweepSpline = createParams->sweepSpline;

    sensor->sweepLastPoint = createParams->sweepPointCount == 0 ? 0 : createParams->sweepPointCount - 1;
    sensor->sweepStep = ((float)sensor->sweepLastPoint * REC_FRAME_RATE) * (_FRAME_RATE / (float)createParams->sweepDuration);

    sensor->owner = obj;
    sensor->beamObject = NULL;
    sensor->childObjectVisibilityState = 0;

    if(createParams->beamHashcode != 0) {
        sensor->beamObject = control_create_object(0, pos, rot, NULL);
        sensor->beamObject->objectType = OBJECTTYPE_GFX;
        sensor->beamObject->effectFlags |= 0x20;
        sensor->beamObject->renderType |= 0x0005;
        sensor->beamObject->scale = sensor->beamLength;
        sensor->beamObject->maybeParent = obj;
        Mat_Copy(&obj->transformMatrix, &sensor->beamObject->transformMatrix);
        hashtable_set_object_to_entity_gfx(sensor->beamObject, createParams->beamHashcode);
    }

    if(createParams->bodyHashcode != 0) {
        _VECTOR direction;
        Mat_GetDir(&direction, &obj->transformMatrix);

        sensor->bodyObject = control_create_object(0, pos, rot, NULL);
        sensor->bodyObject->objectType = OBJECTTYPE_GFX;
        sensor->bodyObject->renderType |= 0x0004;
        sensor->bodyObject->maybeParent = obj;
        Mat_Align2Up(sensor->bodyObject->transformMatrix.m, &CONST_UP_VECTOR.x, &direction.x);
        hashtable_set_object_to_entity_gfx(sensor->bodyObject, createParams->bodyHashcode);
    }

    if(sensor->sweepLastPoint != 0)
        obj->renderType |= 0x0044;

    obj->objectType = OBJECTTYPE_SENSOR;
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    build_LinkToRoom(obj, 0, glb_world);

    sensor->enabledGraphics = celgl;
    sensor->enabledGraphicsLod = celgl->lodRelated;

    if(!sensor->dontAddToCameraList)
        LList_Add(&CameraList, &sensor->cameraListNode);

    return obj;
}

// Internal implementation with standard calling convention
ushort _Sensor_SetBeam(obj_tag *targetObj, obj_tag *sensorObj, SENSOR *sensor) {
    ushort returnValue = 0;
    HITDATA_tag *hitData;
    _VECTOR direction;
    _VECTOR endPosition;

    // Update body object orientation if it exists
    if (sensor->bodyObject != NULL) {
        Mat_GetDir(&direction, &sensorObj->transformMatrix);
        Mat_Align2Up(sensor->bodyObject->transformMatrix.m, &CONST_UP_VECTOR.x, &direction.x);
        sensor->bodyObject->renderType |= 0x20;
    }

    // Update beam object and check for hit
    if ((sensor->beamObject != NULL) && (targetObj != NULL)) {
        Mat_Copy(&sensorObj->transformMatrix, &sensor->beamObject->transformMatrix);
        sensor->beamObject->renderType |= 0x20;

        Mat_GetDir(&direction, &sensorObj->transformMatrix);
        auxVec_AddMulR32(&sensorObj->position, &direction, sensor->beamLength, &endPosition);
        sensor->beamObject->scale = sensor->beamLength;

        bool hitResult = Collide_RayIntersect(
            &sensorObj->position,
            &endPosition,
            sensorObj->inCel,
            sensorObj,
            sensor->beamObject,
            &hitData,
            0,
            8,
            0
        );

        if (hitResult) {
            sensor->beamObject->scale = hitData->unknown1f;
            if (hitData->hitObj == targetObj) {
                returnValue = 1;
            }
            Collide_FreeHitList(&hitData);
        }

        return returnValue;
    }

    return 0xffff;
}

// AUTOLTCG
ushort __declspec(naked) Sensor_SetBeam(obj_tag *targetObj, obj_tag *sensorObj, SENSOR *sensor) {
    // Assembly wrapper for custom calling convention
    // Original game code calls with: param_1 (targetObj) on stack, param_2 (sensorObj) in EDI, param_3 (sensor) in ESI
    _asm {
        push esi                    // Push param_3 (sensor) from ESI
        push edi                    // Push param_2 (sensorObj) from EDI
        push dword ptr [esp + 12]   // Push param_1 (targetObj) from stack
        call _Sensor_SetBeam
        add esp, 12                 // Clean up stack (3 params)
        ret
    }
}

// AUTOINJECT
bool Sensor_InCone(obj_tag *targetObj, obj_tag *sensorObj, SENSOR *sensor) {

    // First check if the target is directly hit by the beam
    ushort beamResult = _Sensor_SetBeam(targetObj, sensorObj, sensor);
    if (beamResult == 1)
        return true;

    // If no target object, can't be in cone
    if (targetObj == NULL)
        return false;

    // Check if target is within range (beam length + target radius)
    float maxDistance = targetObj->radius + sensor->beamLength;
    _VECTOR *sensorPosition = Mat_Position(sensorObj->transformMatrix);
    float distSq = Vec_SqDist3D(sensorPosition, &targetObj->centrePoint);

    if (distSq > maxDistance * maxDistance)
        return false;

    // Check if the target sphere intersects the sensor's cone
    _VECTOR direction;
    Mat_GetDir(&direction, &sensorObj->transformMatrix);
    bool inCone = Intersect_ConeSphere(
        sensorPosition,
        &direction.x,
        sensor->viewConeCos,
        sensor->viewConeSin,
        &targetObj->centrePoint
    );

    if (!inCone)
        return false;

    // Final line-of-sight check
    return Collide_LineOfSight(
        &sensorObj->position,
        &targetObj->position,
        sensorObj->inCel,
        sensorObj,
        targetObj,
        10
    );
}

// AUTOINJECT
void Sensor_Update(obj_tag *sensorObj) {

    obj_tag *player = glb_players[0];

    if (sensorObj == NULL)
        return;

    SENSOR *sensor = (SENSOR*)sensorObj->extraObjectData;

    // Handle visibility state changes
    if (sensor->childObjectVisibilityState == 1) {
        sensorObj->effectFlags |= 0x10;
        if (sensor->beamObject != NULL) {
            sensor->beamObject->effectFlags |= 0x10;
        }
        if (sensor->bodyObject != NULL) {
            sensor->bodyObject->effectFlags |= 0x10;
        }
        sensor->childObjectVisibilityState = 2;
    }
    else if (sensor->childObjectVisibilityState == 2) {
        sensorObj->effectFlags &= ~0x10;
        if (sensor->beamObject != NULL) {
            sensor->beamObject->effectFlags &= ~0x10;
        }
        if (sensor->bodyObject != NULL) {
            sensor->bodyObject->effectFlags &= ~0x10;
        }
        sensor->childObjectVisibilityState = 0;
    }

    // Handle loop sound based on disable switch channel
    if ((sensor->disableSwitchChannel == 0) || (switch_channels[sensor->disableSwitchChannel] == 0)) {
        if (sensor->loopSound == NULL) {
            sensor->loopSound = Sound_Play3D(SFX_ENV_CAMERA_LOOP, &sensorObj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        }
    }
    else {
        if (sensor->loopSound != NULL) {
            Sound_Stop(sensor->loopSound);
            sensor->loopSound = NULL;
        }
        sensorObj->curState = 3;
    }

    switch (sensorObj->curState) {
    case 0:  // Normal patrol/scanning state
        if (sensor->beamObject != NULL) {
            View_SetDrawInAllViews(sensor->beamObject);
        }

        if (sensor->alertSwitchChannel != 0) {
            switch_channels[sensor->alertSwitchChannel] = 0;
        }

        sensor->alertTimer = 0.0f;

        // Handle sweeping motion if spline exists
        if (sensor->sweepLastPoint != 0) {
            Spline_Interp(sensor->sweepSpline, 0, (ushort)sensor->sweepLastPoint, sensor->sweepPosition, &sensorObj->transformMatrix, NULL, 2);
            sensorObj->renderType |= 0x20;

            sensor->sweepPosition += FRAME_RATE_MUL * sensor->sweepStep;

            // Play sound at halfway point
            if ((((float)sensor->sweepLastPoint * 0.5f) < sensor->sweepPosition) && (sensor->alertBeepTimer == 0.0f)) {
                Sound_Play3D(SFX_ENV_CAMERA_CHANGE_DIRECTION, &sensorObj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
                sensor->alertBeepTimer = 1.0f;
            }

            // Wrap around at end of sweep
            if ((float)sensor->sweepLastPoint < sensor->sweepPosition) {
                sensor->sweepPosition = 0.0f;
                Sound_Play3D(SFX_ENV_CAMERA_CHANGE_DIRECTION, &sensorObj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
                sensor->alertBeepTimer = 0.0f;
            }
        }

        // Check if player is in cone
        if ((glb_viewer[4]->field25_0x29 != 0) || !Sensor_InCone(player, sensorObj, sensor)) {
            sensorObj->subState = 0;
            return;
        }

        // Player detected - increment detection counter
        sensorObj->subState++;
        if (sensorObj->subState > 30) {
            sensor->alertTimer = 0.0f;
            sensor->alertBeepTimer = 0.0f;
            sensorObj->curState = 1;
            if (sensor->alertSwitchChannel != 0) {
                Sound_Play3D(SFX_ENV_CAMERA_DETECT_BEEP, &sensorObj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            }
        }
        break;

    case 1:  // Alert state - player detected
        if (sensor->alertSwitchChannel != 0) {
            sensor->alertBeepTimer += REC_FRAME_RATE;

            // Determine beep interval based on alert level
            float beepInterval = 0.5f;
            if (sensor->alertTimer >= 2.0f) {
                beepInterval = 0.25f;
            }
            if (sensor->alertTimer >= 3.0f) {
                beepInterval = 0.125f;
            }

            if (sensor->alertBeepTimer >= beepInterval) {
                sensor->alertBeepTimer = 0.0f;
                Sound_Play3D(SFX_ENV_CAMERA_DETECT_BEEP, &sensorObj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            }
        }

        // Track player position
        Vec_Track(0.05f, &player->centrePoint.x, &sensorObj->position.x, &sensorObj->transformMatrix, &sensor->trackedRotation);

        // Check if player is still in cone
        if ((glb_viewer[4]->field25_0x29 == 0) && Sensor_InCone(player, sensorObj, sensor)) {
            sensorObj->subState = 0;
        }
        else {
            sensorObj->subState++;
        }

        // Lost track of player
        if (sensorObj->subState > FRAME_RATE_INT) {
            sensorObj->curState = 2;
            return;
        }

        sensor->alertTimer += REC_FRAME_RATE;

        // Trigger alert switch after 2 seconds
        if ((sensor->alertTimer >= 2.0f) && (sensor->alertSwitchChannel != 0)) {
            switch_channels[sensor->alertSwitchChannel] = 1;
            switch_channels_time[sensor->alertSwitchChannel] = GameState.NumFramesUnpaused;
        }
        break;

    case 2:  // Return to patrol state
        sensor->alertBeepTimer = 0.0f;

        if (sensor->alertSwitchChannel != 0) {
            switch_channels[sensor->alertSwitchChannel] = 0;
        }

        sensorObj->subState = 0;

        _MATRIX tmpMat;
        quaternion_tag tmpQuat;

        // Interpolate back to spline position
        Spline_Interp(sensor->sweepSpline, 0, (ushort)sensor->sweepLastPoint, sensor->sweepPosition, &tmpMat, NULL, 2);
        Quat_MatToQuat(&tmpQuat, &tmpMat);

        Quat_Slerp_Acc(0.1f, (float*)&sensor->trackedRotation, tmpQuat.q, (float*)&sensor->trackedRotation);
        Quat_QuatToMat(&sensor->trackedRotation, &sensorObj->transformMatrix);

        _Sensor_SetBeam(player, sensorObj, sensor);

        if (Quat_IsEqual(&sensor->trackedRotation, &tmpQuat, 0.01f)) {
            sensorObj->curState = 0;
        }
        break;

    case 3:  // Disabled state
        if (sensor->enabledGraphicsLod != 0) {
            sensorObj->objGraphics = (celglist_tag*)sensor->enabledGraphicsLod;
        }

        if (sensor->beamObject != NULL) {
            sensor->beamObject->displayMask = 0;
        }

        if (sensor->loopSound != NULL) {
            Sound_Stop(sensor->loopSound);
            sensor->loopSound = NULL;
        }

        // Re-enable if switch channel deactivated
        if ((sensor->disableSwitchChannel != 0) && (switch_channels[sensor->disableSwitchChannel] == 0)) {
            sensorObj->objGraphics = sensor->enabledGraphics;
            sensorObj->curState = 0;
        }
        break;
    }
}


