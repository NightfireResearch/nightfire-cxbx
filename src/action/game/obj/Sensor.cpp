#include "Sensor.h"
#include "Switch.h"            // ObjData_Switch, SwitchList's entries
#include "../../../common/xbeClass.h"

#include <math.h>

#include "../../engine/Collide.h"
#include "../../engine/viewer.h"
#include "../../sound/Sound.h"
#include "../../game/view.h"
#include "../sp/SwitchChannels.h"
#include "../Spline.h"
#include "../../math/X87Ops.h"

// XBE_GLOBAL(0x0029aabc, 0xc)
LLISTINFO_tag CameraList;

// Scanning cameras with conical beams, eg in evil base and office, but NOT the lasers which trigger the alarm - presumably some kind of Trigger instead

#pragma pack(push, 1)

typedef struct {
    PlacementHeader header;                // 0x00
    HASHCODE beamHashcode;                 // 0x2c
    uint viewConeHalfAngle;                // 0x30
    uint beamLength;                       // 0x34
    ushort alertSwitchChannel;             // 0x38
    uchar padding3a[2];
    ushort disableSwitchChannel;           // 0x3c
    uchar padding3e[2];
    uchar dontAddToCameraList;             // 0x40
    uchar padding41[3];
    uchar monitorGroup;                    // 0x44
    uchar padding45[3];
    HASHCODE bodyHashcode;                 // 0x48
    ushort monitoredChannel;               // 0x4c
    uchar padding4e[2];
} Create_Sensor_Params;

#pragma pack(pop)

static_assert(offsetof(Create_Sensor_Params, beamHashcode) == 0x2c, "Bad offset of beamHashcode");
static_assert(offsetof(Create_Sensor_Params, bodyHashcode) == 0x48, "Bad offset of bodyHashcode");
static_assert(sizeof(Create_Sensor_Params) == 0x50, "Wrong size for Create_Sensor_Params");

// XBE_GLOBAL(0x0029aab0, 0xc)
LLISTINFO_tag SwitchList;

// The map stores this angle in half-degrees; pi / 360 converts it to radians.
static constexpr float HALF_DEGREES_TO_RADIANS = 0.008726646f;

// AUTOINJECT
void Sensor_Init(void) {
    LList_Init(&CameraList, sizeof(SENSOR), 0);
    LList_Init(&SwitchList, sizeof(ObjData_Switch), 0);
}

// AUTOINJECT
obj_tag* Sensor_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {

    Create_Sensor_Params* createParams = (Create_Sensor_Params*)lvl;

    obj_tag* obj = control_create_object(sizeof(SENSOR), pos, rot, NULL);

    if(obj == NULL)
        return NULL;

    SENSOR* sensor = (SENSOR*)obj->extraObjectData;

    sensor->viewConeHalfAngle = createParams->viewConeHalfAngle;
    sensor->viewConeCos = (float)FCos((double)sensor->viewConeHalfAngle * HALF_DEGREES_TO_RADIANS);
    sensor->viewConeSin = (float)FSin((double)sensor->viewConeHalfAngle * HALF_DEGREES_TO_RADIANS);
    sensor->beamLength = (float)createParams->beamLength;
    sensor->alertSwitchChannel = createParams->alertSwitchChannel;
    sensor->disableSwitchChannel = createParams->disableSwitchChannel;
    sensor->dontAddToCameraList = createParams->dontAddToCameraList;
    sensor->monitorGroup = createParams->monitorGroup;
    sensor->monitoredChannel = createParams->monitoredChannel;

    // The placement's path is the sweep
    sensor->sweepPosition = 0.0f;
    sensor->sweepSpline = createParams->header.spline;

    sensor->sweepLastPoint = createParams->header.splinePoints == 0 ? 0 : createParams->header.splinePoints - 1;
    // On the x87, the frames' share and the step stay unrounded until stored
    double frames = (double)sensor->sweepLastPoint * REC_FRAME_RATE;
    sensor->sweepStep = (float)(_FRAME_RATE / (double)createParams->header.splineLength * frames);

    sensor->owner = obj;
    sensor->beamObject = NULL;
    sensor->monitorView = MONITORVIEW_NONE;

    if(createParams->beamHashcode != 0) {
        sensor->beamObject = control_create_object(0, pos, rot, NULL);
        sensor->beamObject->objectType = OBJECTTYPE_GFX;
        sensor->beamObject->effectFlags |= EFFECT_NO_COLLISION;
        sensor->beamObject->transformFlags |= TRANSFORM_SCALE_LENGTH_ONLY | TRANSFORM_MATRIX_PLACED;
        sensor->beamObject->scale = sensor->beamLength;
        sensor->beamObject->parent = obj;
        Mat_Copy(&obj->transformMatrix, &sensor->beamObject->transformMatrix);
        hashtable_set_object_to_entity_gfx(sensor->beamObject, createParams->beamHashcode);
    }

    if(createParams->bodyHashcode != 0) {
        _VECTOR direction;
        Mat_GetDir(&direction, &obj->transformMatrix);

        sensor->bodyObject = control_create_object(0, pos, rot, NULL);
        sensor->bodyObject->objectType = OBJECTTYPE_GFX;
        sensor->bodyObject->transformFlags |= TRANSFORM_MATRIX_PLACED;
        sensor->bodyObject->parent = obj;
        Mat_Align2Up(sensor->bodyObject->transformMatrix.m, &CONST_UP_VECTOR.x, &direction.x);
        hashtable_set_object_to_entity_gfx(sensor->bodyObject, createParams->bodyHashcode);
    }

    if(sensor->sweepLastPoint != 0)
        obj->transformFlags |= TRANSFORM_MATRIX_PLACED | TRANSFORM_POSITION_FROM_MATRIX;

    obj->objectType = OBJECTTYPE_SENSOR;
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    build_LinkToRoom(obj, 0, glb_world);

    sensor->enabledGraphics = celgl;
    sensor->enabledGraphicsLod = celgl->lod;

    if(!sensor->dontAddToCameraList)
        LList_Add(&CameraList, &sensor->cameraListNode);

    return obj;
}

// Internal implementation with standard calling convention
short Sensor_SetBeam(obj_tag *targetObj, obj_tag *sensorObj, SENSOR *sensor) {
    short returnValue = 0;
    HITDATA_tag *hitData;
    _VECTOR direction;
    _VECTOR endPosition;

    // Update body object orientation if it exists
    if (sensor->bodyObject != NULL) {
        Mat_GetDir(&direction, &sensorObj->transformMatrix);
        Mat_Align2Up(sensor->bodyObject->transformMatrix.m, &CONST_UP_VECTOR.x, &direction.x);
        sensor->bodyObject->transformFlags |= TRANSFORM_MOVED;
    }

    // Update beam object and check for hit
    if ((sensor->beamObject != NULL) && (targetObj != NULL)) {
        Mat_Copy(&sensorObj->transformMatrix, &sensor->beamObject->transformMatrix);
        sensor->beamObject->transformFlags |= TRANSFORM_MOVED;

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
            HITMASK_SKIP_NO_CONTACT,
            0
        );

        if (hitResult) {
            sensor->beamObject->scale = hitData->dist;
            if (hitData->hitObj == targetObj) {
                returnValue = 1;
            }
            Collide_FreeHitList(&hitData);
        }

        return returnValue;
    }

    return -1;  // Error: missing beam or target
}

// AUTOINJECT
bool Sensor_InCone(obj_tag *targetObj, obj_tag *sensorObj, SENSOR *sensor) {

    // First check if the target is directly hit by the beam
    short beamResult = Sensor_SetBeam(targetObj, sensorObj, sensor);
    if (beamResult == 1)
        return true;

    // If no target object, can't be in cone
    if (targetObj == NULL)
        return false;

    // Check if target is within range (beam length + target radius)
    float maxDistance = targetObj->radius + sensor->beamLength;
    _VECTOR *sensorPosition = Mat_Position(sensorObj->transformMatrix);
    double distSq = Vec_SqDist3D(sensorPosition, &targetObj->centrePoint);

    if (distSq > (double)maxDistance * maxDistance)
        return false;

    // Check if the target sphere intersects the sensor's cone
    _VECTOR direction;
    Mat_GetDir(&direction, &sensorObj->transformMatrix);
    bool inCone = Intersect_ConeSphere(
        sensorPosition,
        &direction,
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
        HITMASK_SKIP_GLASS | HITMASK_SKIP_NO_CONTACT
    );
}

// AUTOINJECT
void Sensor_Update(obj_tag *sensorObj) {

    obj_tag *player = glb_players[0];

    if (sensorObj == NULL)
        return;

    SENSOR *sensor = (SENSOR*)sensorObj->extraObjectData;

    if (sensor->monitorView == MONITORVIEW_HIDE) {
        sensorObj->effectFlags |= EFFECT_HIDDEN;
        if (sensor->beamObject != NULL) {
            sensor->beamObject->effectFlags |= EFFECT_HIDDEN;
        }
        if (sensor->bodyObject != NULL) {
            sensor->bodyObject->effectFlags |= EFFECT_HIDDEN;
        }
        sensor->monitorView = MONITORVIEW_SHOW;
    }
    else if (sensor->monitorView == MONITORVIEW_SHOW) {
        sensorObj->effectFlags &= ~EFFECT_HIDDEN;
        if (sensor->beamObject != NULL) {
            sensor->beamObject->effectFlags &= ~EFFECT_HIDDEN;
        }
        if (sensor->bodyObject != NULL) {
            sensor->bodyObject->effectFlags &= ~EFFECT_HIDDEN;
        }
        sensor->monitorView = MONITORVIEW_NONE;
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
        sensorObj->curState = SENSOR_STATE_DISABLED;
    }

    switch (sensorObj->curState) {
    case SENSOR_STATE_PATROL:
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
            sensorObj->transformFlags |= TRANSFORM_MOVED;

            // On the x87: the new position is stored, and the half-way test made with it unrounded
            Extended position = Extended(FRAME_RATE_MUL) * Extended(sensor->sweepStep) + Extended(sensor->sweepPosition);
            sensor->sweepPosition = (float)position;

            // Play sound at halfway point
            if (Extended((float)sensor->sweepLastPoint) * Extended(0.5f) < position && sensor->alertBeepTimer == 0.0f) {
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
        if ((glb_viewer[VIEWER_SCRIPT]->enabled != 0) || !Sensor_InCone(player, sensorObj, sensor)) {
            sensorObj->subState = 0;
            return;
        }

        // Player detected - increment detection counter (the count before the increment is tested)
        if ((short)sensorObj->subState++ > 30) {
            sensor->alertTimer = 0.0f;
            sensor->alertBeepTimer = 0.0f;
            sensorObj->curState = SENSOR_STATE_ALERT;
            if (sensor->alertSwitchChannel != 0) {
                Sound_Play3D(SFX_ENV_CAMERA_DETECT_BEEP, &sensorObj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            }
        }
        break;

    case SENSOR_STATE_ALERT:
        if (sensor->alertSwitchChannel != 0) {
            Extended beep = Extended(REC_FRAME_RATE) + Extended(sensor->alertBeepTimer);   // tested unrounded
            sensor->alertBeepTimer = (float)beep;

            // Determine beep interval based on alert level
            float beepInterval = 0.5f;
            if (sensor->alertTimer >= 2.0f) {
                beepInterval = 0.25f;
            }
            if (sensor->alertTimer >= 3.0f) {
                beepInterval = 0.125f;
            }

            if (beep >= Extended(beepInterval)) {
                sensor->alertBeepTimer = 0.0f;
                Sound_Play3D(SFX_ENV_CAMERA_DETECT_BEEP, &sensorObj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            }
        }

        // Track player position
        Vec_Track(0.05f, &player->centrePoint.x, &sensorObj->position.x, &sensorObj->transformMatrix, &sensor->trackedRotation);

        // Check if player is still in cone
        if ((glb_viewer[VIEWER_SCRIPT]->enabled == 0) && Sensor_InCone(player, sensorObj, sensor)) {
            sensorObj->subState = 0;
        }
        else {
            sensorObj->subState++;
        }

        // Lost track of player
        if (sensorObj->subState > FRAME_RATE_INT) {
            sensorObj->curState = SENSOR_STATE_RETURNING;
            return;
        }

        {
            Extended alert = Extended(REC_FRAME_RATE) + Extended(sensor->alertTimer);   // tested unrounded
            sensor->alertTimer = (float)alert;

            // Trigger alert switch after 2 seconds
            if (alert >= Extended(2.0f) && sensor->alertSwitchChannel != 0) {
                switch_channels[sensor->alertSwitchChannel] = 1;
                switch_channels_time[sensor->alertSwitchChannel] = GameState.NumFramesUnpaused;
            }
        }
        break;

    case SENSOR_STATE_RETURNING:
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

        Sensor_SetBeam(player, sensorObj, sensor);

        if (Quat_IsEqual(&sensor->trackedRotation, &tmpQuat, 0.01f)) {
            sensorObj->curState = SENSOR_STATE_PATROL;
        }
        break;

    case SENSOR_STATE_DISABLED:
        if (sensor->enabledGraphicsLod != NULL) {
            sensorObj->objGraphics = sensor->enabledGraphicsLod;
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
            sensorObj->curState = SENSOR_STATE_PATROL;
        }
        break;
    }
}

