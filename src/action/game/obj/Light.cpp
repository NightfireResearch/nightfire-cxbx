#include "Light.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../../util/DList.h"
#include "../../util/LList.h"
#include "../../util/Random.h"
#include "../../memory.h"
#include "../../engine/psiLight.h"
#include "../../math/math.h"
#include "../../sound/Sound.h"
#include "../sp/SwitchChannels.h"
#include "../../game.h"
#include "../../engine/celglist.h"
#include "../../engine/viewer.h"
#include "../../math/X87Ops.h"
#include "../../util/Sort.h"

#pragma fp_contract(off)

// XBE_GLOBAL(0x00215700, 0x18)
DLISTINFO_tag LightList;
#define NUM_LIGHTS 32
// Lights_CalcClosestLights' answer
// XBE_GLOBAL(0x00215718, 0x78)
light_tag *ClosestLights[30];


// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

// AUTOINJECT
void Light_Init(void) {
    DList_Init(&LightList, sizeof(light_tag), 0);
    Light_AllocateEntries();
}

// AUTOINJECT
void Light_AllocateEntries(void) {
    size_t allocSize = sizeof(light_tag) * NUM_LIGHTS;
    light_tag* lightMemory = (light_tag*)Mem_Malloc(allocSize, (MallocFlags)0x3704, 0);

    memset(lightMemory, 0, allocSize);

    for (int i = 0; i < NUM_LIGHTS; i++) {
        LList_Add(&LightList.freeList, (LLNODE_tag*)&lightMemory[i]);
    }
}

// None on the Xbox, where the linker shares psiLight_Create's `return 0` with it: no object finds its light
light_tag *Light_FindMapLight(_VECTOR *pos, float radius) {
    return NULL;
}

// AUTOINJECT
bool Light_Delete(light_tag* light) {
    if (light == NULL) {
        return false;
    }

    LLNODE_tag* removedNode = LList_Remove(&LightList.activeList, (LLNODE_tag*)light);
    if (removedNode == NULL) {
        return false;
    }

    psiLight_Delete();
    LList_Add(&LightList.freeList, removedNode);
    return true;
}

// AUTOINJECT
light_tag* Light_Create(_VECTOR *pos, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b,
                        float brightness, ushort switchChannel, short lifetimeFrames, undefined1 lightType,
                        float param_9, ushort soundState, ushort soundTimingBase, undefined2 param_12, int soundID) {
    light_tag* light = (light_tag*)DList_MoveFromFree2InUse(&LightList);

    if (light == NULL) {
        Light_AllocateEntries();
        light = (light_tag*)DList_MoveFromFree2InUse(&LightList);
    }

    light->clr_r = clr_r;
    light->brightness = brightness;
    light->clr_g = clr_g;
    light->clr_b = clr_b;
    light->enabled = 1;
    light->switchChannel = switchChannel;

    if (lifetimeFrames == -1) {
        light->lifetimeFrames = 0;
    } else {
        light->lifetimeFrames = lifetimeFrames + 1;
    }

    light->lightType = lightType;
    light->intensity = param_9;
    light->soundState = soundState;
    light->soundTimingBase = soundTimingBase >> 1;

    uint randomValue = Rand_Rand(soundTimingBase);
    light->soundTimingCounter = (short)randomValue + soundTimingBase;

    light->flickerMask = param_12;

    if (soundID == 0) {
        light->soundID = 0xffff;
    } else {
        light->soundID = soundID;
    }

    Vec_Copy(pos, &light->position);

    light->psiLight = (undefined4)psiLight_Create();

    return light;
}

// AUTOINJECT
void Light_Update(void) {
    
    LLNODE_tag* node = LightList.activeList.head;
    while (node != NULL) {
        light_tag* light = (light_tag*)node;
        node = node->next;

        // Update lifetime and delete expired lights
        if (!light->lifetimeFrames || (light->lifetimeFrames--, light->lifetimeFrames != 0)) {

            // Update enabled state from switch channel
            if (light->switchChannel) {
                light->enabled = switch_channels[light->switchChannel];
            }

            // Flicker, with its sound: keyed on +0x18, not on the sound id
            if (light->soundState != 0) {
                // Check if we should trigger sound (counter at 0)
                if ((light->soundTimingCounter & 0xfff) == 0) {
                    light->soundTimingCounter = 0xf000;
                    Sound_Play3D((Action_SFX)light->soundID, &light->position, 100.0, -1.0, -1.0, 0, 0, 0);
                }

                // Update sound timing
                if ((light->soundTimingCounter & 0xf000) == 0) {
                    // Countdown mode
                    light->soundTimingCounter--;
                    light->enabled = 1;
                } else {
                    // Random flickering mode based on frame parity
                    light->enabled = (light->flickerMask == (light->flickerMask & GameState.NumFramesUnpaused));
                    light->soundTimingCounter++;

                    // Reset counter if exceeded base value
                    if (light->soundState < (light->soundTimingCounter & 0xfff)) {
                        uint randomValue = Rand_Rand(light->soundTimingBase);
                        light->soundTimingCounter = (short)randomValue + light->soundTimingBase;
                    }
                }
            }

        } else {
            // Lifetime expired - delete light
            Light_Delete(light);
        }
    }
}

// A colour component moved towards another by a share of the way, at least 1 unless already there
// FUNC_AT(0x0006b0a0)
double ClrStep(float from, float to, float share) {
    double step = (double)to - from;
    if (share < 0.0f)
        share = 0.0f;
    else if (share > 1.0f)
        share = 1.0f;
    if (step == 0.0)
        return from;
    step = step * share;
    if (!(AbsFchs(step) < 1.0f))
        return step + from;
    if (step < 0.0f)
        return -1.0f + (double)from;
    return 1.0f + (double)from;
}

// An object lit by its cel (effect flag 0x800) eases its ambient light towards the cel's for the state of the cel's
// channel, twice as bright (up to 255) with flag 0x10000 while player 0's night vision is on
// AUTOINJECT
void Lights_CalcAmbientLight(obj_tag *obj) {
    cel_tag *cel = obj->inCel;
    if (cel == NULL || !(obj->effectFlags & 0x800))
        return;
    ushort state = (uchar)switch_channels[(uchar)cel->someSwitchChannel];
    short r = cel->ambientR[state];
    short g = cel->ambientG[state];
    short b = cel->ambientB[state];
    if ((obj->effectFlags & 0x10000) && glb_blokes[0]->ambientBoost) {
        r = r * 2 > 0xff ? 0xff : r * 2;
        g = g * 2 > 0xff ? 0xff : g * 2;
        b = b * 2 > 0xff ? 0xff : b * 2;
    }
    obj->ambientR = (uchar)Ftol(ClrStep(obj->ambientR, r, 0.1f));
    obj->ambientG = (uchar)Ftol(ClrStep(obj->ambientG, g, 0.1f));
    obj->ambientB = (uchar)Ftol(ClrStep(obj->ambientB, b, 0.1f));
}

// The lights within reach of a sphere (its radius after its centre), up to 30, sorted, in a list of the module's; NULL
// for none. Mask 0x80 lets in the level's ambient lights (type 0) and those carried by objects within 4 across the
// floor (type 2); 0x100 keeps out type 1.
// AUTOINJECT
light_tag** Lights_CalcClosestLights(_VECTOR *sphere, uint mask, ushort *count) {
    ushort found = 0;
    if (count == NULL)
        return NULL;
    for (light_tag *light = (light_tag *)LightList.activeList.head; light != NULL;
         light = (light_tag *)light->node.next) {
        if (found >= 30)
            break;
        if (!light->enabled)
            continue;
        uchar type = light->lightType;
        if (type == 0 && !(mask & 0x80))
            continue;
        if (type == 1 && (mask & 0x100))
            continue;
        double distance;
        if (type == 2) {
            if (!(mask & 0x80))
                continue;
            distance = Vec_Dist2D(&light->maybeParentObject->position, sphere);
            if (distance > 4.0f)
                continue;
            light->intensity = (float)((1.0f - 0.25f * distance) * 50.0f);
        } else {
            distance = Vec_Dist3D(&light->position, sphere);
            if (distance > (double)light->brightness + ((sphere_equ_tag *)sphere)->r)
                continue;
        }
        light->distanceShare = (float)(distance / ((double)light->brightness * 0.5f));
        ClosestLights[found++] = light;
    }
    *count = found;
    if (found == 1)
        return ClosestLights;
    if (found == 0)
        return NULL;
    QuickSort(ClosestLights, found, sizeof(light_tag *), Compare_Unknown1, NULL);
    return ClosestLights;
}

// The level's ambient lights, from its map: type 0, each colour component from 0 to 1, the radius from its square
// AUTOINJECT
void Light_SetAmbientRadiators(AMBIENT_RADIATORS *radiators) {
    for (ushort i = 0; i < radiators->count; i++) {
        AMBIENT_RADIATOR *radiator = &radiators->lights[i];
        _VECTOR pos;
        Vec_Copy(&radiator->pos, &pos);
        float radius = (float)sqrt(radiator->radiusSq);
        uchar b = (uchar)Ftol(radiator->b * 255.0f);
        uchar g = (uchar)Ftol(radiator->g * 255.0f);
        uchar r = (uchar)Ftol(radiator->r * 255.0f);
        Light_Create(&pos, r, g, b, radius, 0, -1, 0, 1.0f, 0, 0, 0, 0xffff);
    }
}
