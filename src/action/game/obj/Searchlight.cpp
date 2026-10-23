#include "Searchlight.h"

#include <math.h>

#include "../../util/Random.h"
#include "../../util/DList.h"
#include "../../engine/viewer.h"
#include "../../engine/psiLight.h"
#include "../../sound/Sound.h"
#include "../../game/view.h"
#include "../../math/math.h"
#include "../../math/X87Ops.h"
#include "../sp/SwitchChannels.h"
#include "control.h"
#include "Light.h"
#include "Player.h"
#include "Gas.h"

// Searchlight sweep timing constants
// Phase increments by FRAME_RATE_MUL each frame, completing one full cycle after SWEEP_PERIOD_FRAMES
#define SWEEP_PERIOD_FRAMES 480.0f  // 8 seconds at 60fps
static constexpr float kPhaseToAngle = 0.0130899698f;   // 2pi / SWEEP_PERIOD_FRAMES
static_assert(std::bit_cast<uint32_t>(kPhaseToAngle) == 0x3c567750);

#pragma pack(push, 1)

typedef struct {
    uchar padding00[0x2c];                 // 0x00-0x2b
    ushort searchlightType;                // 0x2c
    uchar padding2e[2];                    // 0x2e-0x2f (align to 0x30)
    uchar reenableSwitchChannel;           // 0x30
    uchar padding31[3];                    // 0x31-0x33 (align to 0x34)
    uchar disableSwitchChannel;            // 0x34
    uchar padding35[3];                    // 0x35-0x37 (align to 0x38)
    uchar alertSwitchChannel;              // 0x38
    uchar padding39[3];                    // 0x39-0x3b (align to 0x3c)
    uint minAngleFull;                     // 0x3c - actual min sweep angle
    uint maxAngleFull;                     // 0x40 - actual max sweep angle
} Create_Searchlight_Params;
#pragma pack(pop)

typedef enum {
    Searchlight_Sweeping = 0, // Sweeping around a circular path
    Searchlight_Tracking, // Actively following the player
    Searchlight_Destroyed, // Shot
    Searchlight_Inactive // Not yet turned on
} SearchlightState;

// AUTOINJECT
obj_tag * Searchlight_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_Searchlight_Params* createParams = (Create_Searchlight_Params*)lvl;

    obj_tag* obj = control_create_object(sizeof(ObjData_Spotlight), pos, rot, NULL);

    if (obj == NULL) {
        return NULL;
    }

    ObjData_Spotlight* spotlight = (ObjData_Spotlight*)obj->extraObjectData;

    // Initialize spotlight data
    spotlight->searchlightType = createParams->searchlightType;
    spotlight->phase = (float)Rand_Rand((uint)SWEEP_PERIOD_FRAMES);
    Vec_Copy(rot, &spotlight->rotationAngles);
    spotlight->standObject = NULL;
    spotlight->unusedLinkedObject = NULL;
    spotlight->projectionSpot = NULL;
    spotlight->volumetricCone = NULL;
    spotlight->lensFlare = NULL;
    spotlight->enableSwitchChannel = createParams->reenableSwitchChannel;
    spotlight->disableSwitchChannel = createParams->disableSwitchChannel;
    spotlight->alertSwitchChannel = createParams->alertSwitchChannel;

    // Calculate sweep range from angle parameters
    uint minAngle = createParams->minAngleFull;
    uint maxAngle = createParams->maxAngleFull;

    uint minVal = (minAngle <= maxAngle) ? minAngle : maxAngle;
    uint maxVal = (minAngle <= maxAngle) ? maxAngle : minAngle;

    // On the x87: the radians unrounded until the half range and the centre are stored
    Extended minRadians = Extended((double)minVal) * Extended(0.017453292f);
    Extended maxRadians = Extended((double)maxVal) * Extended(0.017453292f);

    spotlight->sweepHalfRange = (float)((maxRadians - minRadians) * Extended(0.5f));
    spotlight->sweepCenter = (float)((maxRadians + minRadians) * Extended(0.5f));

    // Set up object
    obj->objectType = OBJECTTYPE_SPOTLIGHT;
    obj->curState = (spotlight->enableSwitchChannel != 0) ? Searchlight_Inactive : Searchlight_Sweeping; // If we have a channel to enable, start disabled. Otherwise, start in the scanning state. 
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    build_LinkToRoom(obj, 0, glb_world);

    // Link to nearby searchlights in the same cel
    obj_tag* otherObj = obj->inCel->firstObject;
    while (otherObj != NULL) {
        if ((otherObj != obj) && (otherObj->objectType == OBJECTTYPE_SPOTLIGHT)) {
            double distance = Vec_Dist3D(&obj->position, &otherObj->position);
            if (distance < 3.0f) {
                ObjData_Spotlight* otherSpotlight = (ObjData_Spotlight*)otherObj->extraObjectData;

                // If this is the master body (type 1), link visual components by type
                if (spotlight->searchlightType == 1) {
                    // Store component at appropriate slot: type 0->standObject, type 1->unusedLinkedObject, type 2->projectionOrLight
                    obj_tag** linkedSlots = &spotlight->standObject;
                    linkedSlots[otherSpotlight->searchlightType] = otherObj;

                    // If starting disabled, hide non-stand visual components
                    if (spotlight->enableSwitchChannel != 0) {
                        if (otherSpotlight->searchlightType != 0) {
                            View_SetDrawInNoViews(otherObj);
                        }
                    }
                }
                // If other is master body and this is a component, register with master by type
                else if (otherSpotlight->searchlightType == 1) {
                    obj_tag** otherLinkedSlots = &otherSpotlight->standObject;
                    otherLinkedSlots[spotlight->searchlightType] = obj;

                    // If master starting disabled, hide this non-stand component
                    if (otherSpotlight->enableSwitchChannel != 0) {
                        if (spotlight->searchlightType != 0) {
                            View_SetDrawInNoViews(obj);
                        }
                    }
                }
            }
        }
        otherObj = otherObj->nextInCel;
    }

    // Type 2 is the light holder - create dynamic light and move holder far away
    if (spotlight->searchlightType == 2) {
        _VECTOR lightPos = obj->position;
        light_tag* light = Light_Create(&lightPos, 0xff, 0xff, 0xff, 500.0f, 0, -1, 2, 1.0f, 0, 0, 0, 0xffff);
        light->parentObject = obj;       // Back-reference to holder
        spotlight->dynamicLight = light;  // Store the light_tag

        // Move the holder object far away (light position is managed separately)
        obj->position.x -= 10000.0f;
        obj->position.y -= 10000.0f;
        obj->position.z -= 10000.0f;
    }

    return obj;
}

// Helper function to clean up linked searchlight visual effects
// This was originally inlined in the original code
static void Searchlight_CleanupLinkedObjects(obj_tag *searchlight) {
    ObjData_Spotlight* spotlight = (ObjData_Spotlight*)searchlight->extraObjectData;

    // Clean up projection spot (type 1) or dynamic light (type 2)
    if (spotlight->projectionSpot != NULL) {
        if (spotlight->searchlightType == 2) {
            // Type 2: Destroy the dynamic light
            Light_Delete(spotlight->dynamicLight);
        } else {
            // Type 1: Mark projection spot object for deletion
            spotlight->projectionSpot->flags |= 1;
        }

        spotlight->projectionSpot = NULL;
    }

    // NOTE: standObject and unusedLinkedObject are NOT cleaned up - they remain visible

    // Clean up volumetric cone
    if (spotlight->volumetricCone != NULL) {
        spotlight->volumetricCone->flags |= 1;
        spotlight->volumetricCone = NULL;
    }

    // Clean up lens flare
    if (spotlight->lensFlare != NULL) {
        spotlight->lensFlare->flags |= 1;
        spotlight->lensFlare = NULL;
    }
}

// AUTOINJECT
void Searchlight_Update(obj_tag *searchlight) {
    ObjData_Spotlight* spotlight;
    obj_tag* player;
    obj_tag* componentObj;
    _VECTOR sphericalDir;
    _VECTOR rayEnd;
    _VECTOR playerDir;
    _VECTOR delta;
    HITDATA_tag* hitData;
    float angleY, angleX;
    float distance;

    spotlight = (ObjData_Spotlight*)searchlight->extraObjectData;
    player = glb_players[0];
    hitData = NULL;

    // Only master body (type 1) runs update logic
    if (spotlight->searchlightType != 1) {
        return;
    }

    // Check disable switch channel
    if ((spotlight->disableSwitchChannel != 0) && (switch_channels[spotlight->disableSwitchChannel] != 0)) {
        searchlight->curState = Searchlight_Inactive;
        Searchlight_CleanupLinkedObjects(searchlight);
        spotlight->enableSwitchChannel = 0;
        spotlight->disableSwitchChannel = 0;
        spotlight->alertSwitchChannel = 0;
        Control_NextLOD(searchlight);
    }

    switch ((SearchlightState)searchlight->curState) {
    case Searchlight_Sweeping:
        // On the x87: the angle, its sine or cosine and the sums all unrounded
        searchlight->rotation.x = (float)(FSin((double)spotlight->phase * kPhaseToAngle) * spotlight->sweepHalfRange +
                                          spotlight->sweepCenter);
        searchlight->rotation.y = (float)(FCos((double)spotlight->phase * kPhaseToAngle) * spotlight->sweepHalfRange +
                                          spotlight->rotationAngles.y);
        spotlight->phase += FRAME_RATE_MUL;
        break;

    case Searchlight_Tracking:
        // Activate alert switch
        if (spotlight->alertSwitchChannel != 0) {
            switch_channels[spotlight->alertSwitchChannel] = 1;
        }

        // Calculate direction to player
        delta.x = player->position.x - searchlight->position.x;
        delta.y = (float)(((double)player->position.y - searchlight->position.y) - 1.0f);
        delta.z = player->position.z - searchlight->position.z;

        // Track player horizontally (yaw), by FPATAN; the turn is a fifth or so of the unrounded difference
        angleY = (float)FPatan(delta.x, delta.z);
        searchlight->rotation.y =
            (float)(Vec_AngleDifference(searchlight->rotation.y, angleY) * 0.15f + searchlight->rotation.y);

        // Clamp to sweep range (each side tested on the difference from the yaw as it then is)
        if (Vec_AngleDifference(searchlight->rotation.y, spotlight->rotationAngles.y) > spotlight->sweepHalfRange) {
            searchlight->rotation.y = spotlight->rotationAngles.y - spotlight->sweepHalfRange;
        }
        if (Vec_AngleDifference(searchlight->rotation.y, spotlight->rotationAngles.y) < -spotlight->sweepHalfRange) {
            searchlight->rotation.y = spotlight->rotationAngles.y + spotlight->sweepHalfRange;
        }

        // Track player vertically (pitch)
        angleX = (float)-FPatan(delta.y, sqrt((double)delta.z * delta.z + (double)delta.x * delta.x));
        searchlight->rotation.x =
            (float)(Vec_AngleDifference(searchlight->rotation.x, angleX) * 0.15f + searchlight->rotation.x);

        // Clamp to sweep range
        if (Vec_AngleDifference(searchlight->rotation.x, spotlight->sweepCenter) > spotlight->sweepHalfRange) {
            searchlight->rotation.x = spotlight->sweepCenter - spotlight->sweepHalfRange;
        }
        if (Vec_AngleDifference(searchlight->rotation.x, spotlight->sweepCenter) < -spotlight->sweepHalfRange) {
            searchlight->rotation.x = spotlight->sweepCenter + spotlight->sweepHalfRange;
        }
        break;

    case Searchlight_Destroyed:
        // Spawn smoke particles occasionally
        if ((searchlight->drawnCountdown != 0) &&
            ((searchlight->creationTimeFrames & 0xff) + GameState.NumFramesUnpaused) % 7 == 0) {
            _VECTOR gasVec;
            gasVec.x = 0.0f;
            gasVec.y = 0.0f;
            gasVec.z = Float_FRand(M_2PI);
            Gas_Init(searchlight->inCel, &searchlight->position, &gasVec, NULL, 0.05f, 0.25f, HT_Entity_smoke_1_dust, 7,
                     0x48403080, 1, 0x80);
        }

        // Gradually tilt down
        if (searchlight->rotation.x >= 0.8f) {
            return;
        }
        searchlight->transformFlags |= TRANSFORM_MOVED;
        searchlight->rotation.x += 0.05f;
        return;

    case Searchlight_Inactive:
        // Check enable switch
        if ((spotlight->enableSwitchChannel != 0) && (switch_channels[spotlight->enableSwitchChannel] != 0)) {
            searchlight->curState = 0;
            Sound_Play3D(SFX_ENV_SEARCHLIGHT_SWITCH_ON, &searchlight->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        }
        goto check_bullet_hits;
    }

    // Update stand object (only copies yaw, not pitch)
    componentObj = spotlight->standObject;
    if (componentObj != NULL) {
        componentObj->transformFlags |= TRANSFORM_MOVED;
        componentObj->rotation.y = searchlight->rotation.y;
    }

    // Update ground projection spot (where beam hits surface)
    componentObj = spotlight->projectionSpot;
    if (componentObj != NULL) {
                // Cast ray in searchlight direction
        Vec_Spherical_2_Cartesian(&sphericalDir.x, 100.0f, searchlight->rotation.y, -searchlight->rotation.x);
        rayEnd.x = sphericalDir.x + searchlight->position.x;
        rayEnd.y = sphericalDir.y + searchlight->position.y;
        rayEnd.z = sphericalDir.z + searchlight->position.z;

        bool hit = Collide_RayIntersect(&searchlight->position, &rayEnd, searchlight->inCel, searchlight, NULL, &hitData, 0, 0x18, 0);

        if (hit) {
            if (hitData == NULL) {
                // Hit but no data - hide projection spot
                View_SetDrawInNoViews(componentObj);
                Collide_FreeHitList(&hitData);
            } else {
                // Position projection spot at hit point on surface
                componentObj->position = hitData->hitPosition;
                componentObj->transformFlags |= TRANSFORM_MOVED;

                // Orient projection spot to match surface normal (so it lies flat on ground/walls)
                float normalX = hitData->surfaceNormal.normal.x;
                float normalY = hitData->surfaceNormal.normal.y;
                float normalZ = hitData->surfaceNormal.normal.z;
                componentObj->rotation.x =
                    (float)(1.5707964f - Atan2(normalY, (float)sqrt((double)normalX * normalX + (double)normalZ * normalZ)));
                componentObj->rotation.y = Atan2(normalX, normalZ);
                componentObj->rotation.z = 0.0f;

                // Offset slightly above surface to avoid z-fighting
                auxVec_AddMulR32(&componentObj->position, &hitData->surfaceNormal.normal, 0.01f, &componentObj->position);
                View_SetDrawInAllViews(componentObj);
                Collide_FreeHitList(&hitData);
            }
        } else {
            // No hit - hide projection spot
            View_SetDrawInNoViews(componentObj);
        }

        // Check if player is in beam (using projection spot position)
        // The projection spot is another obj_tag with ObjData_Spotlight extraObjectData
        // Its phase field is repurposed as the lost-player counter
        ObjData_Spotlight* projectionData = (ObjData_Spotlight*)componentObj->extraObjectData;
        // The distance is compared unrounded, and within 2 counts as in the beam
        double beamDistance = Vec_Dist3D(&componentObj->position, &player->position);

        if ((searchlight->curState == Searchlight_Sweeping) && (beamDistance <= 2.0f)) {
            searchlight->curState = Searchlight_Tracking;  // Player spotted - start tracking
            projectionData->phase = 0.0f;  // Reset lost-player counter
        }

        if (searchlight->curState == Searchlight_Tracking) {
            if (beamDistance <= 2.0f) {
                // Player still in beam - reset counter
                projectionData->phase = 0.0f;
            } else {
                // Player not in beam - increment lost counter
                projectionData->phase = projectionData->phase + 1.0f;
            }

            // Lose track after 15 seconds
            if (_FRAME_RATE * 15.0f < projectionData->phase) {
                searchlight->curState = Searchlight_Sweeping;
            }
        }
    }

    // Update volumetric light cone beam effect
    componentObj = spotlight->volumetricCone;
    if (componentObj != NULL) {
        componentObj->transformFlags |= TRANSFORM_MATRIX_PLACED | TRANSFORM_MOVED;
        View_SetDrawInAllViews(componentObj);

        // Orient cone in beam direction toward player
        Vec_Spherical_2_Cartesian(&sphericalDir.x, 1.0f, searchlight->rotation.y, -searchlight->rotation.x);
        Vec_Subtract(&player->position, &searchlight->position, &playerDir);
        Mat_Align2Dir(&componentObj->transformMatrix, &sphericalDir, &playerDir, &playerDir);
        Matrix_SetTrans(&searchlight->position, &componentObj->transformMatrix);
    }

    // Update lens flare effect
    componentObj = spotlight->lensFlare;
    if (componentObj != NULL) {
        if (searchlight->drawnCountdown != 0) {
            // Position flare slightly in front of searchlight
            Vec_Copy(&searchlight->position, &componentObj->position);
            Vec_Spherical_2_Cartesian(&sphericalDir.x, 0.4f, searchlight->rotation.y, -searchlight->rotation.x);
            Vec_Add2(&componentObj->position, &sphericalDir, &componentObj->position);

            // Calculate angle between player's view and searchlight direction
            _VECTOR headPos;
            Player_GetHeadPos(&headPos, player, NULL);
            distance = Vec_Dist3D(&headPos, &searchlight->position);

            Vec_Subtract(&headPos, &searchlight->position, &playerDir);
            // Summed on the x87, z first; compared unrounded, and kept as a float
            double dot = (((double)playerDir.z * sphericalDir.z + (double)playerDir.y * sphericalDir.y) +
                          (double)playerDir.x * sphericalDir.x) / distance;
            float dotProduct = (float)dot;

            // Only show flare when player is looking toward the searchlight
            if (dot > 0.2f) {
                View_SetDrawInAllViews(componentObj);
                componentObj->transformFlags |= TRANSFORM_MOVED;

                // Scale and brighten based on viewing angle
                double intensity = ((double)dotProduct - 0.2f) * 4.0f;
                componentObj->scale = (float)(4.0f * intensity);
                componentObj->tweakA = (uchar)(int)(intensity * 255.0f);

                // Brighten further if direct line of sight
                if (componentObj->tweakA != 0) {
                    bool lineOfSight = Collide_LineOfSight(&glb_viewer[0]->pos, &componentObj->position, player->inCel, componentObj, player, 8);
                    if (lineOfSight) {
                        componentObj->effectFlags |= 0x2000;
                        componentObj->scale *= 1.5f;
                    } else {
                        componentObj->effectFlags &= ~0x2000;
                    }
                }
            } else {
                View_SetDrawInNoViews(componentObj);
            }
        } else {
            View_SetDrawInNoViews(componentObj);
        }
    }

    searchlight->transformFlags |= TRANSFORM_MOVED;

check_bullet_hits:
    // Check for bullet damage
    for (HITDATA_tag* hit = searchlight->hitList; hit != NULL; hit = hit->next) {
        if ((hit->hitObj != NULL) && (hit->hitObj->objectType == OBJECTTYPE_BULLET) && (hit->dmgAmt > 0.0f)) {
            searchlight->curState = Searchlight_Destroyed;
            Searchlight_CleanupLinkedObjects(searchlight);
            Control_NextLOD(searchlight);
            Sound_Play3D(SFX_ENV_SEARCHLIGHT_SHATTER_01, &searchlight->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        }
    }
}
