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

HASHCODE SleeveEnts_SleeveTable[] = {
    (HASHCODE) 0x020009d0, // Watch_BlackGloves
    (HASHCODE) 0x02000900, // Watch_BareHands
    (HASHCODE) 0x020001f7, // Watch3_MP
    (HASHCODE) 0x02000929, // spacesuit_hands1
    (HASHCODE) 0x020009e6, // Bond_hands_malewhite
    (HASHCODE) 0x020009ff, // Bond_hands_maleblack
    (HASHCODE) 0x02000a14, // Bond_hands_femwhite
    (HASHCODE) 0x02000a13, // Bond_hands_femblack
};

// UNINJECTABLE - uses custom calling convention
HASHCODE AnimSleeveGetEntity(int idx) {
    
    // Bad input, use the first item in the list by default
    if (idx >= ARRAY_SIZE(SleeveEnts_SleeveTable))
        idx = 0;
    
    while(true) {
        HASHCODE hashcode = SleeveEnts_SleeveTable[idx];

        // Does the data exist (is the sleeve loaded/present?)
        if (hashtable_getitem(hashcode) != NULL) 
            return hashcode;
        
        // If it isn't loaded, skip to the next item in the list
        idx = idx + 1;
        
        // If we've run out of options, nothing more we can try
        if(idx >= ARRAY_SIZE(SleeveEnts_SleeveTable))
            break;
    }

    return (HASHCODE)0;
}

// AUTOINJECT
void AnimObjectSetSleeveType(obj_tag *param_1, int sleeveNum) {
  
    HASHCODE HVar1 = AnimSleeveGetEntity(sleeveNum);

    if (HVar1 != (HASHCODE)0) {

        // If the hashcode has the corresponding celglist, apply it
        celglist_tag* celGlist = hashtable_hashcode_to_celglist(HVar1);

        if (celGlist != NULL)
            (param_1->animState->animObj).maybeSleeveGlist = celGlist;

    }
  
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

#pragma pack(push, 1)

typedef struct SkeletonInfo {
    short skeletonIdx; // According to AnimSkeletonProcess, this is the index into the SkeletonTable array
    byte numBones;
    byte maybeTypeOrFlags;
    // Following this is a variable amount of extra data:
    //  numBones x float[3]
    // ...?
} SkeletonInfo;

#pragma pack(pop)

// At address 0x001d7578 we should find a table of pointers, 128 entries long, pointing to the skeleton data (top of the header)
#define SkeletonTable (*(SkeletonInfo*(*)[128])0x001d7578)

// At address 0x001d6ac8 we should find a single pointer to the skeleton data currently being loaded (post the header / bone displacements)
#define pAnimData (*(char**)0x001d6ac8)

// AUTOINJECT
void AnimSkeletonProcess(char *data) {

    SkeletonInfo *skel = (SkeletonInfo*)data;

    printf("Processing a skeleton, idx: %i, num bones: %i, unknown data 0x%02x\n", skel->skeletonIdx, skel->numBones, skel->maybeTypeOrFlags);
    
    // Confirmed that maybeTypeOrFlags is not always zero - 1 seen

    // Copy to SkeletonTable
    SkeletonTable[skel->skeletonIdx] = skel;

    // Consume some (variable-length) header based on the number of bones - bone displacement data?
    // This is simplified vs the original code, which iterated over each one?
    pAnimData = (char *)data + 0x10 + skel->numBones * 12;
}

// AUTOGEN
void AnimPostLoadInit(void);
// AUTOGEN
void AnimLoadFile(HASHCODE hashcode,char param_2);
// AUTOGEN
SCRIPTINFO* Script_Load(HASHCODE param_1,_VECTOR *transform,_VECTOR *rotation,uint *fileBuf,void*, void*, void*);
// AUTOGEN
void Script_Update(SCRIPTINFO *param_1);
// AUTOGEN
bool Script_Play(SCRIPTINFO *param_1, char param_2);