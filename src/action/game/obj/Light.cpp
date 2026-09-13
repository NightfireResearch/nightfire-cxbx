#include "Light.h"

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

#define LightList (*(DLISTINFO_tag*)0x00215700)
#define NUM_LIGHTS 32

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
    light->field33_0x34 = param_9;
    light->soundState = soundState;
    light->soundTimingBase = soundTimingBase >> 1;

    uint randomValue = Rand_Rand(soundTimingBase);
    light->soundTimingCounter = (short)randomValue + soundTimingBase;

    light->flickerMask = param_12;

    if (soundID == 0) {
        light->soundID = (short)0xffff;
    } else {
        light->soundID = (short)soundID;
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

            // Handle sound logic
            if (light->soundID != -1) {
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

