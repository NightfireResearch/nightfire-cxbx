#include "Anim.h"


// AUTOGEN
uchar AnimGetBoneWorldTrans(obj_tag *param_1,uint whichBoneMatrix,int param_3,_VECTOR *param_4,_MATRIX *param_5);

// AUTOGEN
bool AnimObjectIsClose(obj_tag* obj, uint unk);

// AUTOGEN
void AnimObjectDelete(obj_tag* obj);

// NOAUTOINJECT
void AnimObjectHeadTrack(obj_tag* gameObj, AnimObj* animObj, quaternion_tag *q, _MATRIX *m, int param_5) {
    // With nothing implemented here, guards will stare straight forwards
    // but animations are otherwise not broken in any way it seems

}

// NOAUTOINJECT
void AnimObjectAimAt(obj_tag *param_1,AnimObj *param_2,quaternion_tag *param_3,_MATRIX *param_4, int param_5) {
    // With nothing implemented here, guards will appear to shoot horizontally straight forwards
    // but animations are otherwise not broken in any way it seems. Their shots will still
    // target the player correctly.
}



// NOAUTOINJECT
void psiBuildMatrixPalette(obj_tag *gameObj, AnimObj *animObj, char param_3) {

    // AnimSkin *skin = animObj->skinPtr;
    // _MATRIX* boneMtxs = animObj->boneMtxs;
    // int numBones = SkeletonTable[skin->skeletonNum] + 2;
    // uint flags = animObj->flags;


}

// NOAUTOINJECT
// void AnimObjectDraw(obj_tag *obj, viewer_tag *viewer) {

//     if(obj == NULL)
//         return;
    
//     if(obj->animState == NULL)
//         return;

//     // animFlags & 1 == 0
//     // TODO
//     if(false)
//         return;

//     // numFrames
//     // TODO
//     if(false)
//         return;

//     AnimObj* animObj = obj->animState->animObj;
//     // Everything beyond here is a massive hack to just see if I can get them to TPose everywhere
//     psiBuildMatrixPalette(obj, animObj, 1);

// }