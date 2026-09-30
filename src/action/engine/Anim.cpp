#include "Anim.h"
#include <string.h>    // memcpy
#include "../memory.h" // Mem_Malloc


// AUTOGEN
void AnimGetBoneWorldTrans(obj_tag *param_1,uint whichBoneMatrix,int param_3,_VECTOR *param_4,_MATRIX *param_5);

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

// XBE_GLOBAL(0x001634c0, 0x20)
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

// The hashcode type byte of a skin file (AnimLoadFile dispatches 0x04 sequences, 0x05 skins, 0x06 scripts).
#define ANIM_HASHTYPE_SKIN 0x05000000

// Mem_Malloc tags for the two allocations (Ghidra: malloc_anim_skin_structs|Unknown and
// malloc_anim_skinmat_structs|Unknown - the low 0x04 is the "Unknown" bit every tag in the game carries).
#define MALLOC_ANIM_SKIN_STRUCTS    ((MallocFlags)0x2004)
#define MALLOC_ANIM_SKINMAT_STRUCTS ((MallocFlags)0x2104)

// A rigged body hashcode with this bit set is not a real entity: it stands for "the player's sleeve", and is
// replaced with the first sleeve entity that is loaded (AnimSleeveGetEntity(0)). The rest of the value is ignored.
#define ANIM_RIGGED_IS_SLEEVE 0x00100000

// Reads a little-endian dword from the anim file cursor and advances it by 4. The file data is not aligned (the
// header is a run of bytes), so the original assembles the value a byte at a time - FUN_00011000 does the same for
// the three scale floats, and the rigged/discrete/bind loops inline it.
static uint32_t AnimData_ReadU32(void) {
    const unsigned char *p = (const unsigned char *)pAnimData;
    uint32_t value = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    pAnimData += 4;
    return value;
}

// FUN_00011000 (0x00011000): AnimData_ReadU32 returned through the x87 stack as a float.
static float AnimData_ReadFloat(void) {
    uint32_t bits = AnimData_ReadU32();
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static unsigned char AnimData_ReadU8(void) {
    unsigned char value = *(unsigned char *)pAnimData;
    pAnimData += 1;
    return value;
}

static void AnimData_AlignTo4(void) {
    while (((uintptr_t)pAnimData & 3) != 0)
        pAnimData++;
}

// Converts a loaded skin file in place: the file stays in memory (its bone/discrete-bone bytes, the hashcode
// arrays - overwritten with celglist pointers - and the datums are pointed at by the new sAnimSkin_tag), and the
// hashtable entry for the file is repointed from the raw file to that sAnimSkin_tag.
// Called only by AnimLoadFile, straight after loading a 0x05xxxxxx file; hashcode arrives in EAX (see the
// adaptor below).
void _AnimProcessSkinData(HASHCODE hashcode) {

    if ((hashcode & 0xff000000) != ANIM_HASHTYPE_SKIN)
        return;

    pAnimData = (char *)hashtable_getitem(hashcode);
    if (pAnimData == NULL)
        return;

    // Not checked for NULL by the original either
    sAnimSkin_tag *skin = (sAnimSkin_tag *)Mem_Malloc(sizeof(sAnimSkin_tag), MALLOC_ANIM_SKIN_STRUCTS, 0);
    hashtable_modify(hashcode, skin);

    // The first dword of the file is skipped (never read)
    pAnimData += 4;

    skin->skinScale.x = AnimData_ReadFloat();
    skin->skinScale.y = AnimData_ReadFloat();
    skin->skinScale.z = AnimData_ReadFloat();

    skin->numRigged = AnimData_ReadU8();
    skin->numDiscrete = AnimData_ReadU8();
    skin->numMorphs = AnimData_ReadU8(); // stored, nothing more read for morphs here
    skin->numDatums = AnimData_ReadU8();
    skin->skeletonNum = AnimData_ReadU8();

    // The skeleton must already be loaded - no NULL check in the original
    uint numBones = SkeletonTable[skin->skeletonNum]->numBones;

    // One byte per bone of the skeleton
    skin->bones = (uchar *)pAnimData;
    pAnimData += numBones;

    if (skin->numDiscrete != 0) {
        // One byte per discrete object (the bone it hangs off), then the hashcodes, dword aligned
        skin->discreteBone = (uchar *)pAnimData;
        pAnimData += skin->numDiscrete;
        AnimData_AlignTo4();

        // The hashcodes are replaced in place by their celglists: discreteObj[i] is the dword just read
        skin->discreteObj = (celglist_tag **)pAnimData;
        for (uint i = 0; i < skin->numDiscrete; i++) {
            HASHCODE hc = (HASHCODE)AnimData_ReadU32();
            skin->discreteObj[i] = (hc == (HASHCODE)0xffffffff) ? NULL : hashtable_hashcode_to_celglist(hc);
        }
    }

    if (skin->numRigged != 0) {
        AnimData_AlignTo4();

        // Converted in place like discreteObj
        skin->riggedBody = (celglist_tag **)pAnimData;
        for (uint i = 0; i < skin->numRigged; i++) {
            HASHCODE hc = (HASHCODE)AnimData_ReadU32();
            celglist_tag *body;
            if (hc == (HASHCODE)0xffffffff) {
                body = NULL;
            } else {
                // The original inlines AnimSleeveGetEntity(0) here: the first loaded entry of
                // SleeveEnts_SleeveTable, or 0 if none is - and then still asks for 0's celglist.
                if (((uint32_t)hc & ANIM_RIGGED_IS_SLEEVE) != 0)
                    hc = AnimSleeveGetEntity(0);
                body = hashtable_hashcode_to_celglist(hc);
            }
            skin->riggedBody[i] = body;
        }

        // Inverse bind matrices: one per skeleton bone, built from a translation then a quaternion in the file.
        // Only skins with rigged bodies have them; allocated even when numBones is 0 (a zero-byte Mem_Malloc).
        skin->invBind = (_MATRIX *)Mem_Malloc(numBones * sizeof(_MATRIX), MALLOC_ANIM_SKINMAT_STRUCTS, 0);
        for (uint i = 0; i < numBones; i++) {
            // Copied as raw bits, as the original does (integer moves, no x87)
            uint32_t transBits[3];
            uint32_t quatBits[4];
            for (int k = 0; k < 3; k++)
                transBits[k] = AnimData_ReadU32();
            for (int k = 0; k < 4; k++)
                quatBits[k] = AnimData_ReadU32();

            float trans[3];
            quaternion_tag quat;
            memcpy(trans, transBits, sizeof(trans));
            memcpy(quat.q, quatBits, sizeof(quat.q));
            Quat_QuatTransToMat(&quat, trans, &skin->invBind[i]);
        }
    }

    if (skin->numDatums != 0) {
        // Left in the file as they are; the original reads each datum's quaternion into a dead local, which has no
        // effect beyond stepping over it
        skin->datums = (AnimDatum *)pAnimData;
        pAnimData += skin->numDatums * sizeof(AnimDatum);
    }
}

// The original takes hashcode in EAX (link-time code generation; pops nothing, preserves EBX/ESI/EDI/EBP).
// Only AnimLoadFile (still original) calls it. C callers must call _AnimProcessSkinData, not this.
// AUTOLTCG
void __declspec(naked) AnimProcessSkinData(HASHCODE hashcode) {
    _asm {
        push eax
        call _AnimProcessSkinData
        add esp, 4
        ret
    }
}
