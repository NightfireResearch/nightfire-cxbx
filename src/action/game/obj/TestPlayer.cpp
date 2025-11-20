#include "Player.h"
#include "../actionhelpers.h"
#include "math.h"

// AUTOGEN
undefined4 * __cdecl Lights_CalcClosestLights(float *param_1,uint param_2,ushort *param_3);

// NOAUTOINJECT
void Player_InShadow(BLData* param_1, obj_tag *gameObj) {

    // Maybe emitted light? Or an arbitrary "minimum brightness" so that even in no light coverage you can be seen?
    uint avgColour = (uint)((gameObj->light_related3 + gameObj->light_related2 +gameObj->light_related1) / 3.0);


    ushort numLightsFound = 0;
    light_tag** lightsFoundArray = Lights_CalcClosestLights((float *)&poVar6->field28_0x60,(uint)*(ushort *)&poVar6->effectTypes,&numLightsFound);

    int i = 0;
    for(int i = 0; i < numLightsFound; i++) {
        light_tag * light = lightsFoundArray[i];
        float brightness = *(float *)(light + 0x30);
        float distance = Vec_Dist3D(light.maybeTargetPos, &gameObj->field28_0x60);
        float modifyColour = (float)((int)(plVar4->clr_r + plVar4->clr_g + plVar4->clr_b) >> 3);
        avgColour += (int)(distance / brightness * modifyColour)
    }

    param_1->visibility = (float)(local_c & 0xffff) * 0.003921569f;

    if (gameObj->movementType == CROUCH) {
        param_1->visibility *= 0.5f;
    }

}
