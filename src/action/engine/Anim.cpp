#include "Anim.h"
#include "../memory.h"

#include <stdio.h>
#include <string.h>

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

// Verify the syntax has been understood correctly
static_assert(sizeof(SkeletonTable[0]) == 4, "SkeletonTable entry size incorrect");
static_assert(sizeof(SkeletonTable) == 128 * 4, "SkeletonTable size incorrect");
static_assert((int)(&SkeletonTable) == 0x1d7578, "SkeletonTable address incorrect");

// At address 0x001d6ac8 we should find a single pointer to the skeleton data currently being loaded (post the header / bone displacements)
#define pAnimData (*(char**)0x001d6ac8)

// AUTOINJECT
void AnimSkeletonProcess(char *data) {

    SkeletonInfo *skel = (SkeletonInfo*)data;

    printf("Processing a skeleton, idx: %i, num bones: %i, unknown data 0x%02x\n", skel->skeletonIdx, skel->numBones, skel->maybeTypeOrFlags);
    
    // Confirmed that maybeTypeOrFlags is not always zero - 1 seen

    // Copy a pointer into the SkeletonTable
    SkeletonTable[skel->skeletonIdx] = skel;

    // Consume some (variable-length) header based on the number of bones - bone displacement data?
    // This is simplified vs the original code, which iterated over each one?
    pAnimData = data + 0x10 + skel->numBones * 12;
}

// AUTOGEN
void AnimPostLoadInit(void);

// UNINJECTABLE - uses custom calling convention. Only used from AnimScriptNew
sAnimSeq_tag * AnimSeqNew(size_t size, MallocFlags allocType) {

    // Allocate memory for the sequence object
    sAnimSeq_tag * seq = (sAnimSeq_tag*)Mem_Malloc(sizeof(sAnimSeq_tag), 0x2304, 0);

    if(seq == NULL)
        return NULL;
    
    // Zero it out
    memset(seq, 0, sizeof(sAnimSeq_tag));

    // Also allocate for the sequence data
    seq->rawData = Mem_Malloc(size, allocType, 0);

    if(seq->rawData == NULL) {
        // If we couldn't allocate the raw data, free the previously allocated sequence object
        Mem_Free((void**)&seq);
        return NULL;
    }

    // Zero out the raw data as well
    memset(seq->rawData, 0, size);

    return seq;
}

// On Xbox, inlined in all of the known call sites (AnimScriptNew and AnimScriptDelete) so the original function does not exist
// therefore this is uninjectable. We have an implementation on PS2, and the inlined code for reference though. 
void AnimSeqDelete(sAnimSeq_tag **seqPtr) {
   
    // Original code does not guard against NULL pointer input, but no harm in adding it?
    if(seqPtr == NULL)
        return;

    // It does however guard against NULL *value*
    if(*seqPtr == NULL)
        return;

    sAnimSeq_tag *seq = *seqPtr;

    // If a "seqUnpakOpt" has been  set up, free it first
    if(seq->seqUnpakOpt != NULL) {
        Mem_Free((void**)&(seq->seqUnpakOpt));
    }

    // If an animation data buffer has been set up, free it too
    if(seq->rawData != NULL) {
        Mem_Free((void**)&(seq->rawData));
    }

    // Free the sequence object itself
    Mem_Free((void**)seqPtr);
}


// UNINJECTABLE - uses custom calling convention. Called from AnimScriptAdd, AnimScriptAppend, AnimDebugCreate only 
sAnimScript_tag * AnimScriptNew(obj_tag *gameObj, sAnimScript_tag *existingScriptList) {

    if(gameObj == NULL)
        return NULL;

    sAnimScript_tag * newScript = (sAnimScript_tag*)Mem_Malloc(sizeof(sAnimScript_tag), 0x2504, 0);

    if(newScript == NULL)
        return NULL;

    memset(newScript, 0, sizeof(sAnimScript_tag));

    newScript->magicStateIndicator = ANIM_SCRIPT_STATE_MAYBE_INITIALISED;

    // Allocate data for bone-related sequence?
    uint skelNum = ((gameObj->animState->animObj).skinPtr)->skeletonNum;
    uint numBones = SkeletonTable[skelNum]->numBones;
    newScript->boneAnimSeq = AnimSeqNew(28 * numBones, 0x2404);

    if(newScript->boneAnimSeq == NULL)
        goto cleanup_alloc_failed;

    // If morph targets are present, allocate a fixed-size morph data buffer (as of yet unclear what goes in here)
    if(((gameObj->animState->animObj).skinPtr)->maybeNumMorphs > 0) {
        newScript->morphAnimSeq = AnimSeqNew(0x58, 0x3b04);

        if(newScript->morphAnimSeq == NULL)
            goto cleanup_alloc_failed;

    }

    // All allocations succeeded, insert into double-linked list if one is provided
    if(existingScriptList == NULL) {
        // No existing list, just set next/prev to NULL
        newScript->prevScript = NULL;
        newScript->nextScript = NULL;
    }
    else {
        // Insert into position 1 of the existing list
        sAnimScript_tag *tmp = existingScriptList->nextScript;
        newScript->prevScript = existingScriptList;
        newScript->nextScript = tmp;
        existingScriptList->nextScript = newScript;
        if (tmp != NULL) {
            tmp->prevScript = newScript;
        }
    }

    // "Happy path" (successful allocation of all elements) returns here
    return newScript;


cleanup_alloc_failed:

    // This was inlined on Xbox, the PS2 code confirms it's AnimSeqDelete
    // It iterates over all the sequences and frees them.
    // AnimSeqDelete is safe to call with NULL pointers, so we can just call it regardless of what stage it failed at
    // The original code did skip the deletion step if bone sequence allocation failed, but this is tidier and more consistent
    AnimSeqDelete(&newScript->morphAnimSeq);
    AnimSeqDelete(&newScript->boneAnimSeq);

    Mem_Free((void**)&newScript);
    
    // "Sad path" (failed to allocate; clean up) returns here
    return NULL;

}

// AUTOGEN - and types are wrong, it's a callback function
undefined4 AnimListBuild(obj_tag *gameObj, uint param_2, uint param_3, uchar* callback, uchar param_5);
// AUTOGEN
void AnimListDelete(obj_tag *gameObj);

typedef struct {
    char unknown[4];
    short field1_0x4;
    short field2_0x6;
} ScriptDataStuff;


// UNINJECTABLE - uses custom calling convention.
void AnimScriptInit(obj_tag *gameObj, sAnimScript_tag *script, HASHCODE animScriptHashcode) {

    bool shouldReverse = ReverseFlag;
    ReverseFlag = false;

    if(gameObj == NULL)
        return;

    if(gameObj->animState == NULL)
        return;

    if(script == NULL)
        return;

    if(animScriptHashcode & 0xFF000000 != 0x6000000)
        return;
    
    script->animScriptData = hashtable_getitem(animScriptHashcode);

    if(script->animScriptData == NULL)
        return;

    script->hashcode = animScriptHashcode;

    // NOTE: Most zeroing of fields below is skipped - the Init function is only ever called on newly allocated scripts
    // so they are already zeroed out from AnimScriptNew

    ScriptDataStuff * sds = (ScriptDataStuff *)(script->animScriptData);

    script->field74_0x74 = 0x20000000;
    script->field97_0xac = sds->field1_0x4;
    short sVar1 = sds->field2_0x6;
    if (sVar1 < 0) {
        script->field74_0x74 = 0xa0000000;
    }
    script->field98_0xae = (char)sVar1;
    script->field99_0xaf = (byte)((ushort)sVar1 >> 9) & 0x3f;

    if (shouldReverse) {
        script->maybeAnimationSpeed = -1.0;
        script->maybeAnimationSpeed2 = (float)(int)script->field97_0xac;
    }
    else {
        script->maybeAnimationSpeed2 = 1.0;
        script->maybeAnimationSpeed = 1.0;
    }
    script->maybeAnimationSpeed3 = script->maybeAnimationSpeed2;
    script->maybeSpeedControlRelated = 0;
    
    if (AnimScriptTimeStamp == 0) {
    AnimScriptTimeStamp = 1;
    }
    script->timestamp = AnimScriptTimeStamp;
    AnimScriptTimeStamp = AnimScriptTimeStamp + 1;
    
    if ((ScriptCam != 0) && (((gameObj->animState->animObj).skinPtr)->maybeNumMorphs != 0)) {
        script->field76_0x7c = 0xe0000002;
    }
    script->someThing2 = 1.0;
    script->someThing1 = 1.0;
    script->someThing3 = 1;

}

// NOUTOINJECT
sAnimScript_tag * AnimScriptAdd(obj_tag *gameObj, HASHCODE animScriptHashcode) {

    if(gameObj == NULL)
        return NULL;

    if(gameObj->animState == NULL)
        return NULL;
    
    AnimListBuild(gameObj, 1, 0, NULL, 1);
    AnimListDelete(gameObj);

    sAnimScript_tag * newScript = AnimScriptNew(gameObj, NULL);
    (gameObj->animState->animObj).firstScript = newScript;

    AnimScriptInit(gameObj, newScript, animScriptHashcode);
    return newScript;
}
