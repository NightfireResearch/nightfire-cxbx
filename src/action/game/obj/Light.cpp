#include "Light.h"

#include <string.h>

#include "../../util/DList.h"
#include "../../util/LList.h"
#include "../../memory.h"
#include "../../engine/psiLight.h"

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

// AUTOGEN
light_tag * Light_Create(_VECTOR *pos,undefined1 clr_r,undefined1 clr_g,undefined1 clr_b,float maybeBrightness,undefined2 param_6,short param_7,undefined1 param_8,float param_9,undefined2 param_10,ushort param_11,undefined2 param_12,int param_13);

