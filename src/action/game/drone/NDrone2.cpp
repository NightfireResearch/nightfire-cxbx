#include "NDrone2.h"

// AUTOGEN
obj_tag* NDrone2_CreateObj(DIVars_tag *diVars); 

// AUTOGEN
obj_tag* NDrone2_CreateFromDIVars(DIVars_tag *diVars);

// NOAUTOINJECT
obj_tag* NDrone2_CreateFromDIVars1(DIVars_tag *diVars) {
    
    // if(diVars == NULL)
    //     return NULL;

    // obj_tag *newObj = NDrone2_CreateObj(diVars);

    // diVars->gameObj = newObj;

    // if(newObj == NULL)
    //     return NULL;

    // Drone_tag *newDrone = (Drone_tag*)diVars->gameObj->extraObjectData;
    
    // newDrone->creationTimeFrames = Rand_Rand(10000); // Some AI seed?

    // (newDrone->diVars).gameObj = newObj;
    // Vec_Copy(&diVars->position, &(newDrone->diVars).position);
    // Vec_Copy(&diVars->rotation, &(newDrone->diVars).rotation);
    // memcpy()

    // newDrone->someField1 = (diVars->animSkin).someThing1;
    // newDrone->someField2 = (diVars->animSkin).someThing2;

    // return newObj;
    return NULL;
}
