#include "psiLight.h"
#include "Direct3D/d3dSeam.h"
#include "../game/obj/Light.h"

#include <math.h>
#include <string.h>

// Nothing to create or free: the game's light records are used as they are
// AUTOINJECT
undefined4 psiLight_Create(void) {
    return 0;
}

// AUTOINJECT
bool psiLight_Delete(void) {
    return true;
}

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
            const light_tag *light = (const light_tag *)lights[i];
            if (isnan(light->intensity))
                continue;
            float k = light->intensity;
            uint32_t dir[3];    // passed on as they are (float bits)
            memcpy(dir, &light->position, sizeof(dir));
            d3dSetLight(used, dir[0], dir[1], dir[2], light->brightness, (float)light->clr_r * k * r,
                        (float)light->clr_g * k * g, (float)light->clr_b * k * b);
            used++;
        }
        if (used > 3)
            return;
    }
    for (; used < 4; used++)
        d3dDisableLight(used);
}
