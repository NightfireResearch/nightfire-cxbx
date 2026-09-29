#include "Anim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// AUTOGEN
bool AnimObjectIsClose(obj_tag* obj, uint unk);

// AUTOGEN
void AnimObjectDelete(obj_tag* obj);

// Turns bone 5 (the head) towards the head-tracking target, inside psiBuildMatrixPalette - see Anim.h.
// AUTOGEN
void AnimObjectHeadTrack(obj_tag* gameObj, AnimObj* animObj, quaternion_tag *worldQuats, _MATRIX *boneMtxs, int bone);

// Turns bone 2 (the spine) towards the aim target, inside psiBuildMatrixPalette.
// AUTOGEN
void AnimObjectAimAt(obj_tag *gameObj, AnimObj *animObj, quaternion_tag *worldQuats, _MATRIX *boneMtxs, int bone);

// Concat(A, B): B = B * A, a full affine product (B.t = B.t * A.R + A.t). Its AUTOGEN body is in Player.cpp.
void Concat(float *a, float *b);

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



// ---------------------------------------------------------------------------------------------------------------
// psiBuildMatrixPalette: from the local pose to world bone matrices, and from those to the skinning palette.
//
// The pose buffer holds each bone's local rotation and translation (translations already scaled by the skin).
// The hierarchy comes from the skin: bones are stored parents first, so one pass in index order resolves them.
// Bones the skin does not use are skipped and keep whatever matrices they had.
//
// Two quirks of the original, kept:
//  - the root's translation is added to the object's position without being rotated by the object's matrix
//    (RotTransMat adds translations), so a root offset stays in world axes whichever way the object faces;
//  - head tracking and aiming act on bones 5 and 2 inside the walk, so every child of those bones follows.
//
// ANIMOBJ_BONES_VALID / ANIMOBJ_PALETTE_VALID make a second call in the same frame free; AnimObjectUpdate clears
// them. The palette is only built for the draw (buildPalette) and only for a skin with rigged meshes.
// ---------------------------------------------------------------------------------------------------------------

// AUTOINJECT
void psiBuildMatrixPalette(obj_tag *obj, AnimObj *ao, char buildPalette) {
    sAnimSkin_tag *skin = ao->skin;
    _MATRIX *bone = ao->boneMtxs;
    int numBones = SkeletonTable[skin->skeletonNum]->numBones;
    uint flags = ao->flags;

    if (!(flags & ANIMOBJ_BONES_VALID)) {
        ao->flags = flags | ANIMOBJ_BONES_VALID;

        bool headTrack = (flags & (ANIMOBJ_HEADTRACK_REQ | ANIMOBJ_HEADTRACK_ON)) != 0;
        bool aim = (flags & (ANIMOBJ_AIM_REQ | ANIMOBJ_AIM_ON)) != 0;

        _MATRIX objMtx = obj->transformMatrix;
        _VECTOR *trans = (_VECTOR *)ao->pose;
        quaternion_tag *quats = (quaternion_tag *)((char *)ao->pose + numBones * sizeof(_VECTOR));
        quaternion_tag worldQuats[96]; // the skeleton's translation mask has room for 96 bones; the original's
                                       // stack array is the same size

        for (int b = 0; b < numBones; b++) {
            uchar link = skin->bones[b];
            if (!(link & ANIM_BONE_USED))
                continue;

            int parent = link & ANIM_BONE_PARENT_MASK;
            _MATRIX *m = &bone[b];

            if (parent == ANIM_BONE_ROOT) {
                Quat_QuatTransToMat(&quats[b], (float *)&trans[b], m);
                RotTransMat(&objMtx, m);
                Quat_MatToQuat(&worldQuats[b], m);
            } else {
                MultiplyQuaternionAltOrder(&worldQuats[parent], &quats[b], &worldQuats[b]);
                Quat_QuatToMat(&worldQuats[b], m);
                MatrixMultiplyVector(&bone[parent], &trans[b], Mat_Position((*m)));

                if (headTrack && b == ANIM_HEADTRACK_BONE)
                    AnimObjectHeadTrack(obj, ao, worldQuats, bone, ANIM_HEADTRACK_BONE);
                if (aim && b == ANIM_AIM_BONE)
                    AnimObjectAimAt(obj, ao, worldQuats, bone, ANIM_AIM_BONE);
            }
        }
    }

    if (buildPalette && skin->numRigged != 0 && !(ao->flags & ANIMOBJ_PALETTE_VALID)) {
        ao->flags |= ANIMOBJ_PALETTE_VALID;

        _MATRIX *palette = ao->palette;
        for (int b = 0; b < numBones; b++) {
            if (!(skin->bones[b] & ANIM_BONE_USED))
                continue;
            // A bind-space vertex v lands at (v + invBind.t) * R + t: the inverse bind poses have no rotation.
            Mat_CopyRot(&bone[b], &palette[b]);
            MatrixMultiplyVector(&bone[b], Mat_Position(skin->invBind[b]), Mat_Position(palette[b]));
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// AnimGetBoneWorldTrans: where a bone or datum is in the world this frame.
//
// 'which' with bit 31 set is a raw bone index; otherwise which + datumOffset is a datum id, looked up in the skin
// (weapons ask for their datum as whichBone + 3, for instance). posOut always receives the position; mtxOut, if
// given, the whole matrix. Without a resolved pose - or for a datum the skin lacks - the answer is the origin
// and the identity. The datum's bone is not range-checked, as in the original.
// ---------------------------------------------------------------------------------------------------------------

// AUTOINJECT
void AnimGetBoneWorldTrans(obj_tag *obj, uint which, int datumOffset, _VECTOR *posOut, _MATRIX *mtxOut) {
    AnimState *as;
    if (obj != NULL && (as = obj->animState) != NULL && (as->animObj.flags & ANIMOBJ_POSE_VALID)) {
        AnimObj *ao = &as->animObj;
        psiBuildMatrixPalette(obj, ao, 0);
        sAnimSkin_tag *skin = ao->skin;

        if (which & 0x80000000) {
            _MATRIX *m = &ao->boneMtxs[which & 0x7fffffff];
            *posOut = *Mat_Position((*m));
            if (mtxOut != NULL)
                *mtxOut = *m;
            return;
        }

        int id = (int)which + datumOffset;
        for (int d = 0; d < skin->numDatums; d++) {
            AnimDatum *datum = &skin->datums[d];
            if (datum->id != id)
                continue;
            _MATRIX m;
            Quat_QuatTransToMat(&datum->q, (float *)&datum->t, &m);
            Concat(ao->boneMtxs[datum->bone].m, m.m);
            *posOut = *Mat_Position(m);
            if (mtxOut != NULL)
                *mtxOut = m;
            return;
        }
    }

    Vec_Zero(posOut);
    if (mtxOut != NULL)
        Mat_Identity(mtxOut);
}

// ---------------------------------------------------------------------------------------------------------------
// AnimFrameCopy: decodes one whole frame of a sequence into a pose buffer.
//
// A sequence is a list of tracks, one per channel: for bone b of the skeleton, tx ty tz qx qy qz if its bit is
// set in the skeleton's transMask, else only qx qy qz (the translation is then the skeleton's default offset).
// A morph (facial) sequence has one scalar track per morph target instead. A track is a u32 head - its byte
// size in the low half, its first key word in the high half - then key words (frames << 6 | coefficient mask),
// taken from u32s low half first, each followed by its float coefficients: a polynomial of degree up to five in
// absolute time t = frame - 1. A key covers the frames up to the running total of the lengths so far.
//
// Output: _VECTOR trans[numBones] then quaternion_tag quat[numBones] (x, y, z, w; w is derived, and a stored
// z >= 2 means z - 4 with a negative w), or float weights[numElems] for a morph sequence. Translations are
// multiplied by the skin scale, except bone 0 (root motion), which is put back into raw units at the end.
// Bones the skin does not use are left untouched.
//
// The arithmetic follows the original step for step - four channels are evaluated together as
// acc += P[i] * c_i for i = 1..5, rounded to float each time, with P[i] = t^i computed in double - because
// the aim is a bit-exact match (the x87 runs at 53-bit precision here; the D3D9 device preserves it).
//
// Two quirks of the original are kept: the "first translation" recorded in seq+0x18/+0x24 is read one past
// the last translation (quat[0].xyz), and the inverse skin scale is applied to the first three weights of a
// morph sequence too.
// ---------------------------------------------------------------------------------------------------------------

// The four channels being evaluated together (the original's globals at 0x1d69c0 and 0x1d6a34).
typedef struct {
    float coef[6][4];   // coef[i][lane]; coef[0] doubles as the accumulator
    int channel[4];
    int count;
} AnimChannelBatch;

enum {
    ANIM_CH_QUAT_Z = 0, // and w
    ANIM_CH_QUAT_Y = 1,
    ANIM_CH_QUAT_X = 2,
    ANIM_CH_TRANS_Z = 3,
    ANIM_CH_TRANS_Y = 4,
    ANIM_CH_TRANS_X = 5,
    ANIM_CH_MORPH = 6,
};

// INVENTED NAME for the original's FUN_00013c70.
static void AnimFrameEmitChannels(AnimChannelBatch *batch, const float powers[6], float **morphOut,
                                  _VECTOR **transOut, quaternion_tag **quatOut) {
    for (int i = 1; i < 6; i++)
        for (int lane = 0; lane < 4; lane++)
            batch->coef[0][lane] = (float)((double)powers[i] * batch->coef[i][lane] + batch->coef[0][lane]);

    for (int lane = 0; lane < batch->count; lane++) {
        float v = batch->coef[0][lane];
        switch (batch->channel[lane]) {
        case ANIM_CH_MORPH:
            *(*morphOut)++ = v;
            break;
        case ANIM_CH_TRANS_X:
            (*transOut)->x = v * Glb_SkinScale.x;
            break;
        case ANIM_CH_TRANS_Y:
            (*transOut)->y = v * Glb_SkinScale.y;
            break;
        case ANIM_CH_TRANS_Z:
            (*transOut)->z = v * Glb_SkinScale.z;
            (*transOut)++;
            break;
        case ANIM_CH_QUAT_X:
            (*quatOut)->q[0] = v;
            break;
        case ANIM_CH_QUAT_Y:
            (*quatOut)->q[1] = v;
            break;
        default: {
            quaternion_tag *q = *quatOut;
            bool negativeW = !(v < 2.0f);
            q->q[2] = negativeW ? (float)((double)v - 4.0) : v;
            double s = 1.0 - (((double)q->q[2] * q->q[2] + (double)q->q[1] * q->q[1]) + (double)q->q[0] * q->q[0]);
            q->q[3] = s > 0.0 ? (float)(negativeW ? -sqrt(s) : sqrt(s)) : 0.0f;
            (*quatOut)++;
            break;
        }
        }
    }
    batch->count = 0;
}

// AUTOINJECT
void AnimFrameCopy(float *out, sAnimSeq_tag *seq, sAnimSkin_tag *skin, float frameArg) {
    float frame = (float)(int)frameArg;
    if (frame == -1.0f)
        frame = seq->frame;
    if (frame < (float)seq->firstFrame)
        frame = (float)seq->firstFrame;
    if (frame > (float)seq->lastFrame)
        frame = (float)seq->lastFrame;

    double t = (double)frame - 1.0;
    float powers[6];
    double p = t;
    powers[0] = 1.0f;
    for (int i = 1; i < 6; i++) {
        powers[i] = (float)p;
        p *= t;
    }
    uint tFrame = (uint)(int)t;

    SkeletonInfo *skel = SkeletonTable[seq->skeletonNum];
    bool morph = (seq->flags & ANIMSEQ_FLAG_MORPH) != 0;
    int numElems = morph ? seq->numElems : skel->numBones;

    sAnimRawSeq_tag *raw = seq->rawSeq;
    raw->cursor = raw->trackData;
    uint *trackBase = raw->cursor;
    uint *track = trackBase;

    float *morphOut = out;
    _VECTOR *transOut = (_VECTOR *)out;
    quaternion_tag *quatOut = (quaternion_tag *)((_VECTOR *)out + numElems);
    bool useCache = (seq->flags & ANIMSEQ_FLAG_UNPAK_CACHE) != 0;
    AnimUnpakEntry *cache = (AnimUnpakEntry *)seq->unpakCache - 1;

    AnimChannelBatch batch;
    batch.count = 0;

    for (int b = 0; b < numElems; b++) {
        bool skip = false;
        int channel;
        if (morph) {
            channel = ANIM_CH_MORPH;
        } else {
            // Channels are evaluated four at a time, but never across a bone.
            if (batch.count > 0)
                AnimFrameEmitChannels(&batch, powers, &morphOut, &transOut, &quatOut);
            if (skin != NULL && !(skin->bones[b] & ANIM_BONE_USED)) {
                transOut++;
                quatOut++;
                skip = true;
            }
            if (skel->transMask[b >> 5] & (1u << (b & 31))) {
                channel = ANIM_CH_TRANS_X;
            } else {
                if (!skip) {
                    transOut->x = skel->boneOffset[b].x * Glb_SkinScale.x;
                    transOut->y = skel->boneOffset[b].y * Glb_SkinScale.y;
                    transOut->z = skel->boneOffset[b].z * Glb_SkinScale.z;
                    transOut++;
                }
                channel = ANIM_CH_QUAT_X;
            }
        }

        for (;;) {
            uint head = *track;
            uint *next = (uint *)((uchar *)track + (head & 0xffff));
            track++;

            if (!skip) {
                uint *p = track;
                uint pending = head;   // its high half is the first key word
                bool havePending = true;
                uint acc = 0;
                if (useCache) {
                    cache++;
                    if (cache->keyOffset != 0) {
                        p = (uint *)(((uint)(size_t)trackBase + cache->keyOffset) & ~1u);
                        pending = (uint)cache->pendingHigh << 16;
                        havePending = (cache->keyOffset & 1) != 0;
                        acc = cache->accBefore;
                    }
                }

                uint mask;
                for (;;) {
                    if (useCache) {
                        cache->keyOffset = (uint)((uchar *)p - (uchar *)trackBase) | (havePending ? 1 : 0);
                        cache->pendingHigh = (ushort)(pending >> 16);
                        cache->accBefore = (ushort)acc;
                    }
                    uint word;
                    if (havePending) {
                        word = pending >> 16;
                        havePending = false;
                    } else {
                        pending = *p++;
                        word = pending & 0xffff;
                        havePending = true;
                    }
                    acc += word >> 6;
                    mask = word & 0x3f;
                    if (tFrame <= acc)
                        break;
                    for (uint m = mask; m != 0; m &= m - 1)
                        p++;
                }

                for (int i = 0; i < 6; i++, mask >>= 1)
                    batch.coef[i][batch.count] = (mask & 1) ? *(float *)p++ : 0.0f;
                batch.channel[batch.count] = channel;
                if (++batch.count == 4)
                    AnimFrameEmitChannels(&batch, powers, &morphOut, &transOut, &quatOut);
            }

            if (channel == ANIM_CH_MORPH)
                channel = 0;
            track = next;
            if (channel-- == 0)
                break;
        }
    }
    if (batch.count > 0)
        AnimFrameEmitChannels(&batch, powers, &morphOut, &transOut, &quatOut);

    _VECTOR *root = (_VECTOR *)out;
    root->x *= Glb_InvSkinScale.x;
    root->y *= Glb_InvSkinScale.y;
    root->z *= Glb_InvSkinScale.z;

    if (!(seq->flags & ANIMSEQ_FLAG_FIRST_TRANS)) {
        seq->flags |= ANIMSEQ_FLAG_FIRST_TRANS;
        seq->firstTrans = *transOut;
    }
    Vec_Subtract(transOut, &seq->firstTrans, &seq->transRelFirst);
}

// At address 0x001d6ac8 we should find a single pointer to the skeleton data currently being loaded (post the header / bone displacements)
#define pAnimData (*(char**)0x001d6ac8)

// AUTOINJECT
void AnimSkeletonProcess(char *data) {

    SkeletonInfo *skel = (SkeletonInfo*)data;

    printf("Processing a skeleton, idx: %i, num bones: %i, translated bones %i\n", skel->skeletonIdx, skel->numBones, skel->numTransBones);

    // Copy to SkeletonTable
    SkeletonTable[skel->skeletonIdx] = skel;

    // Step past the header and the default bone offsets
    pAnimData = (char *)data + 0x10 + skel->numBones * 12;
}

// AUTOGEN
void AnimPostLoadInit(void);
// AUTOGEN
void AnimLoadFile(HASHCODE hashcode,char param_2);
