#include "psiLight.h"
#include "Direct3D/d3dSeam.h"

#include <math.h>

// Nothing to create or free: the game's light records are used as they are
// AUTOINJECT
undefined4 psiLight_Create(void) {
    return 0;
}

// AUTOINJECT
bool psiLight_Delete(void) {
    return true;
}

// A light record, as far as this reads it
struct LightRecord {
    char unknown00[8];
    uint32_t dirX, dirY, dirZ;   // 0x08 passed on as they are (float bits)
    char unknown14[0x1c];
    float range;                 // 0x30
    float intensity;             // 0x34 NaN for a light that is off
    uint8_t r, g, b;             // 0x38
};

// Sets the GPU's four lights from the first lights in the list that are on, each scaled by its intensity and the
// tint (r, g, b, each at most 1); the rest are switched off (0x000de610)
// AUTOINJECT
void psiLight_SetLights(void **lights, ushort count, float r, float g, float b) {
    if (r > 1.0f) r = 1.0f;
    if (g > 1.0f) g = 1.0f;
    if (b > 1.0f) b = 1.0f;
    int used = 0;
    if (lights != NULL && count != 0) {
        for (int i = 0; i < 4 && i < (int)count; i++) {
            const LightRecord *light = (const LightRecord *)lights[i];
            if (isnan(light->intensity))
                continue;
            float k = light->intensity;
            d3dSetLight(used, light->dirX, light->dirY, light->dirZ, light->range, (float)light->r * k * r,
                        (float)light->g * k * g, (float)light->b * k * b);
            used++;
        }
        if (used > 3)
            return;
    }
    for (; used < 4; used++)
        d3dDisableLight(used);
}
