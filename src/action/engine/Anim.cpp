#include "Anim.h"
#include <string.h>    // memcpy
#include "../memory.h" // Mem_Malloc
#include "../util/hashtable.h"


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



// AUTOGEN
void psiBuildMatrixPalette(obj_tag *gameObj, AnimObj *animObj, char param_3);

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

// AUTOGEN
void AnimFrameCopy(float *out, sAnimSeq_tag *seq, sAnimSkin_tag *skin, float frame);

// The hashcode type byte of a sequence file.
#define ANIM_HASHTYPE_SEQ 0x04000000

// The decoded pose of one frame, as AnimFrameCopy writes it: the bones' translations first.
#define ANIM_FRAME_BUFFER_SIZE 0xa80

// Points seq at its sequence file and copies what sampling needs from the file's header. NULL, and seq left as it
// was, when hashcode is not a loaded sequence. Inlined into both its callers on the Xbox.
static sAnimRawSeq_tag *AnimSeqGetInfo(HASHCODE hashcode, sAnimSeq_tag *seq) {
    if ((hashcode & 0xff000000) != ANIM_HASHTYPE_SEQ)
        return NULL;

    sAnimRawSeq_tag *raw = (sAnimRawSeq_tag *)hashtable_getitem(hashcode);
    if (raw == NULL)
        return NULL;

    seq->rawSeq = raw;
    memcpy(seq->boundSphere, raw->boundSphere, sizeof(seq->boundSphere));
    memcpy(seq->rootBox, raw->rootBox, sizeof(seq->rootBox));
    seq->numFrames = raw->numFrames;
    seq->skeletonNum = raw->skeletonNum;
    seq->raw3e = raw->raw3e;
    seq->numElems = raw->numElems;
    seq->flags = (seq->flags & ~0xf) | (raw->flags & 0xf);
    return raw;
}

// The root translation of a script's bone sequence at the script's current frame. False, with out untouched, for a
// script without ANIMSCRIPT_HAS_ROOT_TRANS or whose sequence is not loaded.
// AUTOINJECT
bool AnimGetFirstTrans(sAnimScript_tag *script, _VECTOR *out) {
    float frame[ANIM_FRAME_BUFFER_SIZE / sizeof(float)];

    if (!(script->flags & ANIMSCRIPT_HAS_ROOT_TRANS))
        return false;

    sAnimSeq_tag *seq = script->boneAnimSeq;
    if (AnimSeqGetInfo(seq->hashcode, seq) == NULL)
        return false;

    AnimFrameCopy(frame, seq, NULL, script->curFrame);
    out->x = frame[0];
    out->y = frame[1];
    out->z = frame[2];
    return true;
}

// The root translation of a sequence at a frame, straight from the file, with no object or script. A sequence that
// is not loaded is still decoded, from the zeroed record.
// AUTOINJECT
void AnimGetFrameTransRaw(HASHCODE hashcode, int frame, _VECTOR *out) {
    sAnimSeq_tag seq;
    float pose[ANIM_FRAME_BUFFER_SIZE / sizeof(float)];

    memset(&seq, 0, sizeof(seq));
    seq.hashcode = hashcode;
    AnimSeqGetInfo(hashcode, &seq);
    seq.firstFrame = 1;
    seq.lastFrame = seq.numFrames;
    AnimFrameCopy(pose, &seq, NULL, (float)frame);
    out->x = pose[0];
    out->y = pose[1];
    out->z = pose[2];
}

// The n-th rigid part of an animated object, NULL past the skin's count.
// AUTOINJECT
obj_tag* AnimGetNDiscreteObj(obj_tag *obj, uint n) {
    if (obj == NULL || obj->animState == NULL)
        return NULL;

    AnimObj *animObj = &obj->animState->animObj;
    if (n >= animObj->skin->numDiscrete)
        return NULL;

    return animObj->discreteObj[n];
}

// ---- aiming and joint collision

// The joint box tables, by skin, and the boxes AnimGetCollData builds from them
#define HumanCollisionBones ((const AnimCollBone *)0x00163198)
#define AstronautCollisionBones ((const AnimCollBone *)0x001632f0)
#define HitList2 ((COLLJOINT_tag *)0x001d6bf0)

// The skin that takes AstronautCollisionBones
#define SKIN_ASTRONAUT ((HASHCODE)0x05000078)

// Whether the object aims (AnimObjectSetAiming)
// AUTOINJECT
bool AnimObjectIsAiming(obj_tag *obj) {
    if (obj == NULL || obj->animState == NULL)
        return false;
    return (obj->animState->animObj.flags & (ANIMOBJ_AIMING | ANIMOBJ_AIMING_2)) != 0;
}

// AUTOINJECT
bool AnimObjectIsHeadTracking(obj_tag *obj) {
    if (obj == NULL || obj->animState == NULL)
        return false;
    return (obj->animState->animObj.flags & ANIMOBJ_HEAD_TRACKING) != 0;
}

// Aims the object at target (NULL stops it), within range, turning at rate; with atBone, at that bone of it.
// AUTOINJECT
void AnimObjectSetAiming(obj_tag *obj, const void *target, float range, float rate, char atBone, uchar bone) {
    if (obj == NULL || obj->animState == NULL)
        return;
    AnimObj *animObj = &obj->animState->animObj;
    animObj->aimRate = rate;
    animObj->aimRange2 = range * range;
    if (target == NULL) {
        animObj->flags &= ~ANIMOBJ_AIMING;
        animObj->aimTarget = NULL;
        return;
    }
    animObj->flags |= ANIMOBJ_AIMING;
    animObj->aimTarget = target;
    if (atBone) {
        animObj->aimTargetBone = bone;
        animObj->flags |= ANIMOBJ_AIM_AT_BONE;
    } else {
        animObj->flags &= ~ANIMOBJ_AIM_AT_BONE;
    }
}

// The boxes round an animated object's joints, in the world, for Collide_Jointy: one per row of the skin's table,
// in HitList2, with their count. The first box sits as far above its bone as the bone is above the other. NULL and
// no boxes unless the object is animated with ANIMOBJ_FLAG_1 and ANIMOBJ_FLAG_2.
// AUTOINJECT
COLLJOINT_tag* AnimGetCollData(obj_tag *obj, ushort *count) {
    if (obj == NULL || obj->animState == NULL) {
        *count = 0;
        return NULL;
    }
    AnimObj *animObj = &obj->animState->animObj;
    if (!(animObj->flags & ANIMOBJ_FLAG_1) || !(animObj->flags & ANIMOBJ_FLAG_2)) {
        *count = 0;
        return NULL;
    }

    psiBuildMatrixPalette(obj, animObj, 0);
    sAnimSkin_tag *skin = animObj->skin;
    const AnimCollBone *row = skin->hashcode == SKIN_ASTRONAUT ? AstronautCollisionBones : HumanCollisionBones;
    ushort n = 0;
    for (; row->bone >= 0; row++) {
        int other = row->other;
        if (other < 0)
            other = skin->bones[row->bone] & 0x7f;
        _MATRIX *boneMtx = &animObj->boneMtxs[row->bone];
        _MATRIX *otherMtx = &animObj->boneMtxs[other];
        COLLJOINT_tag *box = &HitList2[n];
        Mat_Copy(otherMtx, &box->m);
        if (n == 0) {
            _VECTOR rise;
            Vec_Subtract(Mat_Position(*boneMtx), Mat_Position(*otherMtx), &rise);
            rise.x = 0.0f;
            rise.z = 0.0f;
            Vec_Add2(Mat_Position(*boneMtx), &rise, Mat_Position(box->m));
        } else {
            box->m.m[12] = (float)(((double)otherMtx->m[12] + boneMtx->m[12]) * 0.5f);
            box->m.m[13] = (float)(((double)otherMtx->m[13] + boneMtx->m[13]) * 0.5f);
            box->m.m[14] = (float)(((double)otherMtx->m[14] + boneMtx->m[14]) * 0.5f);
        }
        n++;
        box->half.x = row->half.x * skin->skinScale.x;
        box->half.y = row->half.y * skin->skinScale.y;
        box->half.z = row->half.z * skin->skinScale.z;
        box->joint = row->bone;
    }
    *count = n;
    return HitList2;
}
