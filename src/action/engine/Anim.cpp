#include "Camera.h"
#include "Anim.h"
#include <math.h>
#include <stdio.h>
#include <string.h>    // memcpy
#include "Script.h"    // ScriptCam
#include "psiDraw.h"
#include "psiFile.h"
#include "psiGraphics.h"
#include "viewer.h"
#include "celglist.h"
#include "../memory.h" // Mem_Malloc
#include "../game.h"
#include "../math/math.h"
#include "../math/X87Ops.h"
#include "../util/hashtable.h"
#include "../util/Random.h"    // Rand_Rand
#include "../sound/Sound.h"
#include "../game/view.h"
#include "../game/weapon_stats.h"
#include "../game/mp/multiplayer.h"
#include "../game/drone/NDrone2.h"
#include "../game/obj/Player.h"
#include "../game/obj/control.h"
#include "../game/obj/BodyGlow.h"
#include "../game/obj/build.h"
#include "../game/obj/bullet.h"
#include "../game/obj/Gas.h"
#include "../game/obj/Light.h"
#include "../game/obj/Effect.h"

// XBE_GLOBAL(0x001d7578, 0x200)
SkeletonInfo *SkeletonTable[128];
// XBE_GLOBAL(0x001d77a0, 0x4)
int32_t GlbAnimListCount;
// XBE_GLOBAL(0x001d77a4, 0x80)
sAnimScript_tag *GlbAnimList[32];
// XBE_GLOBAL(0x001d7827, 0x1)
uint8_t FootstepFoot;

#pragma fp_contract(off)

// The skeletal and facial animation of the action engine: loading skins, sequences and scripts, the scripts that play
// on an object and the sets that choose them, sampling and blending the sequences into a pose, the bones' matrices,
// head tracking and aiming, and drawing. docs/anim has the formats.

// ---------------------------------------------------------------------------------------------------------------
// Globals and originals

// AnimSizes: the bytes and counts of what AnimLoadFile loaded
typedef struct AnimSizes {
    uint skinBytes;
    uint seqBytes;
    uint scriptBytes;
    uint numSkins;
    uint numSeqs;
    uint numScripts;
    uint seqFrames;
} AnimSizes;
// XBE_GLOBAL(0x001d6a44, 0x1c)
AnimSizes AnimFileSizes;

// XBE_GLOBAL(0x001d7570, 0x4)
uint32_t AnimScriptTimeStamp;
// XBE_GLOBAL(0x001d6a60, 0x4)
uint32_t AnimTagTimeStamp;
// XBE_GLOBAL(0x001d7794, 0xc)
_VECTOR Glb_SkinScale;
// XBE_GLOBAL(0x001d6a28, 0xc)
_VECTOR Glb_InvSkinScale;
// XBE_GLOBAL(0x001d6ac4, 0x2)
int16_t nDistanceTables;
// XBE_GLOBAL(0x001d6a65, 0x1)
uint8_t Glb_AnimPostLoadDone;
// XBE_GLOBAL(0x001d7828, 0x1)
uint8_t ReverseFlag;
// XBE_GLOBAL(0x001d7778, 0x4)
sAnimScript_tag *Glb_pCurrentScript;
// XBE_GLOBAL(0x001d6a64, 0x1)
uint8_t Glb_bScriptDeleted;
// The footstep a script command asked for: 1 for two of the player's scripts, 2 for the rest (AnimObjectUpdate)
// XBE_GLOBAL(0x001d6a24, 0x1)
uint8_t FootstepType;
// The morphs AnimObjectDraw hands the renderer: the strongest eight, in index order
// XBE_GLOBAL(0x001d7826, 0x1)
uint8_t Morph_Count;
// XBE_GLOBAL(0x001d6a68, 0x58)
float Morph_Weight[22];
// XBE_GLOBAL(0x001d777c, 0x18)
uchar Morph_Index[24];
// XBE_GLOBAL(0x001d7824, 0x1)
uint8_t bCharacterLight;   // characters get their own light
// XBE_GLOBAL(0x001d7825, 0x1)
uint8_t bShadowCharacter;  // draw this character's shadow

// The file being loaded and the cursor the loaders read it with
// XBE_GLOBAL(0x001d6ac8, 0x4)
char *pAnimData;
// XBE_GLOBAL(0x001d6ac0, 0x4)
int32_t AnimFileSize;
// XBE_GLOBAL(0x001d6a20, 0x4)
void *pAnimFileData;

// AnimFrameCopy's working store: the powers of the frame, and four channels evaluated together (coefficient i of
// lane l at [i][l]; row 0 is the sum)
// XBE_GLOBAL(0x001d6acc, 0x18)
static float AnimFramePoly[6];
// XBE_GLOBAL(0x001d69c0, 0x60)
float AnimChannelCoef[6][4];
// XBE_GLOBAL(0x001d6a34, 0x10)
static int AnimChannelType[4];

// The facial scripts by alert status (four) and the blink, for skeleton 1 and for the others
// XBE_GLOBAL(0x00163468, 0x14) readonly relocate
const HASHCODE FacialAnims[5] = {
    HT_Script_FemaleFaceAlert0,
    HT_Script_FemaleFaceAlert1,
    HT_Script_FemaleFaceAlert2,
    HT_Script_FemaleFaceAlert3,
    HT_Script_FemaleFaceBlink,
};
// XBE_GLOBAL(0x00163454, 0x14) readonly relocate
const HASHCODE FacialAnimsAltSkeleton[5] = {
    HT_Script_MaleFaceAlert0,
    HT_Script_MaleFaceAlert1,
    HT_Script_MaleFaceAlert2,
    HT_Script_MaleFaceAlert3,
    HT_Script_MaleFaceBlink,
};

// The bone (by index) the laser sight's beam leaves from, and its datum
#define WEAPON_LASER_PISTOL 0x18

// A morph sequence's pose: fixed size whatever the skin's morph count
#define ANIM_MORPH_POSE_SIZE 0x58
// The decoded pose of one frame
#define ANIM_FRAME_BUFFER_SIZE 0xa80

// The facial morphs that turn the eyes (AnimObjectHeadTrack's lookMorphX/Y)
#define MORPH_EYES_Y 11
#define MORPH_EYES_Y_INV 12
#define MORPH_EYES_X_INV 13
#define MORPH_EYES_X 14

// The originals, called by address
// INVENTED NAME: where the line from a through b meets the sphere about centre, in out; 0 if it misses

// The sequences' distance tables, made once each (AnimDistanceTableGet)
#define NUM_DISTANCE_TABLES 32
typedef struct AnimDistanceTableEntry {
    HASHCODE hashcode;
    AnimDistanceTable *table;
} AnimDistanceTableEntry;
// XBE_GLOBAL(0x001d6af0, 0x100)
AnimDistanceTableEntry DistanceTables[NUM_DISTANCE_TABLES];

// The axes AnimDistanceTableGet measures (AnimDistanceTableCreate's)
#define DISTANCE_AXIS_X 1
#define DISTANCE_AXIS_Y 2
#define DISTANCE_AXIS_Z 4
// A frame's step shorter than this counts as DISTANCE_MIN_STEP
#define DISTANCE_STEP_EPSILON 1e-05f
#define DISTANCE_MIN_STEP 0.01f
// AnimFrameCopy's poses here: as many floats as fit the original's buffers
#define DISTANCE_POSE_FLOATS 0x2a0

// What the original's FCOMP and FCHS give: the sign flipped unless x >= 0, so -0 stays -0 (MSVC's fabs clears it)
static float Magnitude(float x) {
    return x >= 0.0f ? x : std::bit_cast<float>(std::bit_cast<uint32_t>(x) ^ 0x80000000u);
}

// a moved towards b by the fraction t, never past b: the x87's arithmetic, the direction from the stored gap
static float LerpTo(float a, float b, float t) {
    double gap = (double)b - a;
    double result = gap * t + a;
    if (!((float)gap >= 0.0f)) {
        if (result < b)
            result = b;
    } else if (result > b) {
        result = b;
    }
    return (float)result;
}

// The nearest the object is to any of the four views (AnimObj::lod)
static uint AnimObjectNearestLod(obj_tag *obj) {
    const uchar *lod = obj->animState->animObj.lod;
    uint nearest = lod[0];
    if (lod[1] < nearest)
        nearest = lod[1];
    if (lod[2] < nearest)
        nearest = lod[2];
    if (lod[3] < nearest)
        nearest = lod[3];
    return nearest;
}

// ---------------------------------------------------------------------------------------------------------------
// Loading

// XBE_GLOBAL(0x001634c0, 0x20)
HASHCODE SleeveEnts_SleeveTable[] = {
    HT_Entity_Watch_BlackGloves, // Watch_BlackGloves
    HT_Entity_Watch_BareHands, // Watch_BareHands
    HT_Entity_Watch3_MP, // Watch3_MP
    HT_Entity_spacesuit_hands1, // spacesuit_hands1
    HT_Entity_Bond_hands_malewhite, // Bond_hands_malewhite
    HT_Entity_Bond_hands_maleblack, // Bond_hands_maleblack
    HT_Entity_Bond_hands_femwhite, // Bond_hands_femwhite
    HT_Entity_Bond_hands_femblack, // Bond_hands_femblack
};

// The first sleeve entity that is loaded, from idx on and round to the start: 0 if none is
HASHCODE AnimSleeveGetEntity(uint idx) {
    for (uint tries = 0; tries < ARRAY_SIZE(SleeveEnts_SleeveTable); tries++, idx++) {
        if (idx >= ARRAY_SIZE(SleeveEnts_SleeveTable))
            idx = 0;
        HASHCODE hashcode = SleeveEnts_SleeveTable[idx];
        if (hashtable_getitem(hashcode) != NULL)
            return hashcode;
    }
    return (HASHCODE)0;
}

// AUTOINJECT
void AnimObjectSetSleeveType(obj_tag *obj, int sleeveNum) {
    HASHCODE sleeve = AnimSleeveGetEntity(sleeveNum);
    if (sleeve != HASHCODE_NONE) {
        // If the hashcode has the corresponding celglist, apply it
        celglist_tag *celGlist = hashtable_hashcode_to_celglist(sleeve);
        if (celGlist != NULL)
            obj->animState->animObj.sleeveGlist = celGlist;
    }
}

// Before a level loads: on a cold load the skeletons, the time stamps and the skin scales are reset too
// AUTOINJECT
void AnimPreLoadInit(char keepSkeletons) {
    if (!keepSkeletons) {
        AnimScriptTimeStamp = 1;
        AnimTagTimeStamp = 0x80000001;
        Glb_SkinScale.z = 1.0f;
        Glb_SkinScale.y = 1.0f;
        Glb_SkinScale.x = 1.0f;
        Glb_InvSkinScale.z = 1.0f;
        Glb_InvSkinScale.y = 1.0f;
        Glb_InvSkinScale.x = 1.0f;
        for (int i = 0; i < 128; i++)
            SkeletonTable[i] = NULL;
        nDistanceTables = 0;
    }
    memset(&AnimFileSizes, 0, sizeof(AnimFileSizes));
    Glb_AnimPostLoadDone = 0;
}

// Loads <hashcode>.bin and adds it to the hashtable, unless it is there already
void loadAnimFileCapturingSize(MallocFlags tag, HASHCODE hashcode) {
    char name[0x100];
    sprintf(name, "%s%8.8x.bin", "", hashcode);
    if (tag != MALLOC_ANIM_SKIN_DATA) {
        if (tag == MALLOC_ANIM_SEQ_DATA)
            sprintf(name, "%s%8.8x.%s", "", hashcode, "bin");
        else
            sprintf(name, "%s%8.8x.bin", "", hashcode);
    }
    void *data = psiFileLoad(name, tag, &AnimFileSize);
    if (hashtable_getitem(hashcode) == NULL)
        hashtable_additem(hashcode, data);
    pAnimFileData = data;
}

static uint AnimData_ReadU16(void) {
    const uchar *p = (const uchar *)pAnimData;
    pAnimData += 2;
    return (uint)p[0] | ((uint)p[1] << 8);
}

// Reads a little-endian dword from the anim file cursor and advances it by 4. The file data is not aligned (the
// header is a run of bytes), so the original assembles the value a byte at a time.
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

// FUN_00013880: the next half of the track data, a u32 read whenever the high half of the last is used up
static uint AnimData_ReadHalf(uint *pending, uint *half) {
    uint value;
    if (!(*half & 1)) {
        uint word = AnimData_ReadU32();
        *pending = word >> 16;
        value = word & 0xffff;
    } else {
        value = *pending & 0xffff;
    }
    (*half)++;
    return value;
}

// Measures a sequence's tracks: the file's first three words become the track data, its size and a cursor. The
// walk sequences measured by distance get their axes.
// AUTOINJECT
void AnimProcessSeqData(HASHCODE hashcode) {
    // The hashcode type bytes AnimLoadFile dispatches on
    if ((hashcode & HASHCODE_SECTION_MASK) != HT_AnimSeq_HASHCODE_BASE)
        return;
    sAnimRawSeq_tag *raw = (sAnimRawSeq_tag *)hashtable_getitem(hashcode);
    pAnimData = (char *)raw;
    if (raw == NULL)
        return;

    pAnimData += offsetof(sAnimRawSeq_tag, flags);
    raw->flags = AnimData_ReadU32() & 0xf;
    // The bound sphere and the root box are read and dropped
    for (int i = 0; i < 10; i++)
        AnimData_ReadFloat();
    short numFrames = (short)AnimData_ReadU16();
    short numTracks = (short)AnimData_ReadU16();
    pAnimData += 4;
    uint *trackData = (uint *)pAnimData;

    uint pending = 0;
    uint half = 0;
    for (int track = 0; track < numTracks; track++) {
        AnimData_ReadHalf(&pending, &half);     // the track's size
        uint frames = 0;
        do {
            uint key = AnimData_ReadHalf(&pending, &half);
            frames += key >> 6;
            for (uint mask = key & 0x3f; mask != 0; mask >>= 1) {
                if (mask & 1)
                    pAnimData += sizeof(float);
            }
        } while (frames < (uint)(numFrames - 1));
        // The next track starts on a new word
        if (half & 1)
            half++;
    }

    raw = (sAnimRawSeq_tag *)hashtable_getitem(hashcode);
    switch ((uint)hashcode) {
    case HT_AnimSeq_0000e9: case HT_AnimSeq_Walk: case HT_AnimSeq_MpMagnumRun: case HT_AnimSeq_MpMagnumWalk:
    case HT_AnimSeq_MpRifleRun: case HT_AnimSeq_MpRifleWalk: case HT_AnimSeq_MpRifleIdle2:
    case HT_AnimSeq_MpRifleCrouch: case HT_AnimSeq_MpLauncherRun: case HT_AnimSeq_MpLauncherWalk:
    case HT_AnimSeq_MpUnarmedRun: case HT_AnimSeq_MpUnarmedWalk: case HT_AnimSeq_MpRifleCrouchWalk:
    case HT_AnimSeq_0007b0: case HT_AnimSeq_MpLauncherCrouchWalk: case HT_AnimSeq_MpUnarmedCrouchWalk:
    case HT_AnimSeq_MpRifleStrafeRight: case HT_AnimSeq_MpRifleStrafeLeft: case HT_AnimSeq_MpSmgCrouchWalk:
    case HT_AnimSeq_MpSmgRun: case HT_AnimSeq_MpSmgWalk: case HT_AnimSeq_MpDualWalk: case HT_AnimSeq_MpDualRun:
    case HT_AnimSeq_MpMagnumCrouchWalk: case HT_AnimSeq_MpDualCrouchWalk: case HT_AnimSeq_MpPistolStrafeRight:
    case HT_AnimSeq_MpPistolStrafeLeft: case HT_AnimSeq_MpPistolRun: case HT_AnimSeq_MpPistolWalk:
    case HT_AnimSeq_MpPistolCrouchWalk: case HT_AnimSeq_StartCrouch: case HT_AnimSeq_Start:
        raw->flags |= ANIMSEQ_DISTANCE_AXES;
        break;
    }
    uint size = (uint)(pAnimData - (char *)trackData);
    raw = (sAnimRawSeq_tag *)hashtable_getitem(hashcode);
    raw->trackDataSize = size;
    raw->trackData = trackData;
    raw->cursor = NULL;
}

// Steps over a script's commands; nothing is kept but the cursor
void AnimProcessScriptData(HASHCODE hashcode) {
    if ((hashcode & HASHCODE_SECTION_MASK) != HT_Script_HASHCODE_BASE)
        return;
    pAnimData = (char *)hashtable_getitem(hashcode);
    if (pAnimData == NULL)
        return;
    pAnimData += 6;
    uint numCmds = AnimData_ReadU16() & 0x1ff;
    for (; numCmds != 0; numCmds--) {
        uint cmd = AnimData_ReadU16();
        uint type = cmd & 0xff;
        uint words = cmd >> 8;
        if (type == 0)
            words += 3;
        else if (type <= 4)
            words += 2;
        else
            NF_WARN("Script 0x%x invalid command 0x%x data 0x%x", hashcode, type, words); // GC check (0x80044310)
        pAnimData += words * 2;
    }
}

// AUTOINJECT
void AnimLoadFile(HASHCODE hashcode, char noProcess) {
    switch (hashcode & HASHCODE_SECTION_MASK) {
    case HT_AnimSeq_HASHCODE_BASE:
        loadAnimFileCapturingSize(MALLOC_ANIM_SEQ_DATA, hashcode);
        AnimFileSizes.seqBytes += AnimFileSize;
        AnimFileSizes.numSeqs++;
        if (!noProcess)
            AnimProcessSeqData(hashcode);
        AnimFileSizes.seqFrames += ((sAnimRawSeq_tag *)hashtable_getitem(hashcode))->numFrames;
        break;
    case HT_Skin_HASHCODE_BASE:
        loadAnimFileCapturingSize(MALLOC_ANIM_SKIN_DATA, hashcode);
        AnimFileSizes.skinBytes += AnimFileSize;
        AnimFileSizes.numSkins++;
        AnimProcessSkinData(hashcode);
        break;
    case HT_Script_HASHCODE_BASE:
        loadAnimFileCapturingSize(MALLOC_ANIM_SCRIPT_DATA, hashcode);
        AnimFileSizes.scriptBytes += AnimFileSize;
        AnimFileSizes.numScripts++;
        AnimProcessScriptData(hashcode);
        break;
    default:
        NF_WARN("Invalid hash code %p", (void *)hashcode); // GC check (0x80044830)
        break;
    }
}

// Once a load: the human skins share the Bond skin's datums
// AUTOINJECT
void AnimPostLoadInit(void) {
    HASHCODE skins[128];

    if (Glb_AnimPostLoadDone)
        return;
    Glb_AnimPostLoadDone = 1;
    uint count = hashtable_get_hashtype_count(HT_Skin_HASHCODE_BASE);
    if (count == 0)
        return;
    sAnimSkin_tag *bond = (sAnimSkin_tag *)hashtable_getitem(HT_Skin_Bond_Combat);
    if (bond == NULL) {
        NF_WARN("***** FATAL : Bondcombat skin must be loaded on all levels! *****"); // GC check (0x80044a38)
        return;
    }
    hashtable_get_hashtype_data(HT_Skin_HASHCODE_BASE, skins);
    for (uint i = 0; i < count; i++) {
        sAnimSkin_tag *skin = (sAnimSkin_tag *)hashtable_getitem(skins[i]);
        if (skin->skeletonNum > 1)
            continue;
        if (skins[i] == HT_Skin_SnowGuard_Camera) {
            AnimDatum *own = skin->datums;
            uint numOwn = skin->numDatums;
            skin->numDatums = bond->numDatums + numOwn;
            skin->datums = (AnimDatum *)Mem_Malloc(skin->numDatums * sizeof(AnimDatum), MALLOC_ANIM_SKIN_STRUCTS, 0);
            memcpy(skin->datums, bond->datums, bond->numDatums * sizeof(AnimDatum));
            memcpy(&skin->datums[bond->numDatums], own, numOwn * sizeof(AnimDatum));
        } else {
            skin->numDatums = bond->numDatums;
            skin->datums = bond->datums;
        }
    }
}

// AUTOINJECT
void AnimSkeletonProcess(char *data) {

    SkeletonInfo *skel = (SkeletonInfo*)data;

    printf("Processing a skeleton, idx: %i, num bones: %i, with translation tracks: %i\n", skel->skeletonIdx,
           skel->numBones, skel->numTransBones);

    // Copy to SkeletonTable
    SkeletonTable[skel->skeletonIdx] = skel;

    // Consume some (variable-length) header based on the number of bones - bone displacement data?
    // This is simplified vs the original code, which iterated over each one?
    pAnimData = (char *)data + 0x10 + skel->numBones * 12;
}

// A rigged body hashcode with this bit set is not a real entity: it stands for "the player's sleeve", and is
// replaced with the first sleeve entity that is loaded (AnimSleeveGetEntity(0)). The rest of the value is ignored.
#define ANIM_RIGGED_IS_SLEEVE 0x00100000

// Converts a loaded skin file in place: the file stays in memory (its bone/discrete-bone bytes, the hashcode
// arrays - overwritten with celglist pointers - and the datums are pointed at by the new sAnimSkin_tag), and the
// hashtable entry for the file is repointed from the raw file to that sAnimSkin_tag.
// Called only by AnimLoadFile, straight after loading a 0x05xxxxxx file.
void AnimProcessSkinData(HASHCODE hashcode) {

    if ((hashcode & HASHCODE_SECTION_MASK) != HT_Skin_HASHCODE_BASE)
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
        skin->discreteObj = (Ptr32<celglist_tag> *)pAnimData;
        for (uint i = 0; i < skin->numDiscrete; i++) {
            HASHCODE hc = (HASHCODE)AnimData_ReadU32();
            skin->discreteObj[i] = (hc == (HASHCODE)0xffffffff) ? NULL : hashtable_hashcode_to_celglist(hc);
        }
    }

    if (skin->numRigged != 0) {
        AnimData_AlignTo4();

        // Converted in place like discreteObj
        skin->riggedBody = (Ptr32<celglist_tag> *)pAnimData;
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

// ---------------------------------------------------------------------------------------------------------------
// The list of an object's scripts

// Gathers into GlbAnimList (up to 32) the object's scripts that pass any of the criteria with value; with
// ANIMLIST_REMOVE those that pass leave it instead. The callback is called for each. The first in the list, NULL
// for none.
// AUTOINJECT
sAnimScript_tag* AnimListBuild(obj_tag *obj, uint criteria, uint value, AnimListFunc callback, uint mode) {
    if (obj == NULL || obj->animState == NULL)
        return NULL;
    AnimObj *animObj = &obj->animState->animObj;

    if (mode & ANIMLIST_RESTART)
        GlbAnimListCount = 0;
    int count = GlbAnimListCount;
    for (sAnimScript_tag *script = animObj->firstScript; script != NULL; script = script->nextScript) {
        uint left = criteria;
        uint bit = 1;
        while (left != 0) {
            while (!(left & bit))
                bit <<= 1;
            left ^= bit;

            bool match;
            switch (bit) {
            case ANIMLIST_ALL:          match = true; break;
            case ANIMLIST_NOT:          match = (uintptr_t)script != value; break;
            case ANIMLIST_FIRST:        match = script->prevScript == NULL; break;
            case ANIMLIST_LAST:         match = script->nextScript == NULL; break;
            case ANIMLIST_STOPPED:      match = (script->flags & ANIMSCRIPT_STOPPED) != 0; break;
            case ANIMLIST_HASHCODE:     match = script->hashcode == value; break;
            case ANIMLIST_IS_OR_SYNCED: match = (uintptr_t)script == value || (uintptr_t)script->syncMaster == value; break;
            case ANIMLIST_SYNCED_TO:    match = (uintptr_t)script->syncMaster == value; break;
            case ANIMLIST_SYNCED:       match = script->syncMaster != NULL; break;
            case ANIMLIST_TAG:          match = script->groupTag == value; break;
            case ANIMLIST_TAG_BITS:     match = (script->groupTag & value) != 0; break;
            case ANIMLIST_MUTE_MORPH:   match = (script->flags & ANIMSCRIPT_MUTE_MORPH) != 0; break;
            case ANIMLIST_MORPH:
                match = animObj->skin->numMorphs != 0 && (script->morphAnimSeq->flags & ANIMSEQ_MORPH);
                break;
            case ANIMLIST_MASKED:       match = script->boneMask != NULL; break;
            case ANIMLIST_OVERLAY:      match = (script->flags & ANIMSCRIPT_OVERLAY) != 0; break;
            default:                    match = false; break;
            }
            bit <<= 1;
            if (!match)
                continue;

            int i = 0;
            while (i < count && GlbAnimList[i] != script)
                i++;
            if (mode & ANIMLIST_REMOVE) {
                if (i != count) {
                    GlbAnimListCount = --count;
                    for (; i < GlbAnimListCount; i++)
                        GlbAnimList[i] = GlbAnimList[i + 1];
                }
            } else if (i == count && i < 32) {
                GlbAnimList[count] = script;
                GlbAnimListCount = ++count;
            }
            count = GlbAnimListCount;
        }
    }

    if (count == 0)
        return NULL;
    if (callback != NULL) {
        for (int i = 0; i < GlbAnimListCount; i++)
            callback(obj, GlbAnimList[i]);
    }
    return GlbAnimList[0];
}

// Deletes the scripts in the list, and empties it
// AUTOINJECT
void AnimListDelete(obj_tag *obj) {
    for (int i = 0; i < GlbAnimListCount; i++)
        AnimScriptDelete(obj, &GlbAnimList[i]);
    GlbAnimListCount = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Sequences and scripts

// A sequence with a pose buffer of size bytes, both zeroed; NULL if either allocation fails
sAnimSeq_tag* AnimSeqNew(uint size, uint tag) {
    sAnimSeq_tag *seq = (sAnimSeq_tag *)Mem_Malloc(sizeof(sAnimSeq_tag), MALLOC_ANIM_SEQ_STRUCTS, 0);
    if (seq == NULL)
        return NULL;
    memset(seq, 0, sizeof(sAnimSeq_tag));
    seq->pose = (float *)Mem_Malloc(size, (MallocFlags)tag, 0);
    if (seq->pose == NULL) {
        Mem_Free((void **)&seq);
        return NULL;
    }
    memset(seq->pose, 0, size);
    return seq;
}

// Inline on the Xbox (their AnimSeqDelete)
static void AnimSeqDelete(sAnimSeq_tag **slot) {
    sAnimSeq_tag *seq = *slot;
    if (seq == NULL)
        return;
    if (seq->unpakCache != NULL)
        Mem_Free((void **)&seq->unpakCache);
    if (seq->pose != NULL)
        Mem_Free((void **)&seq->pose);
    Mem_Free((void **)slot);
}

// Points seq at its sequence file (raw, or the one the hashtable has) and copies what sampling needs from its
// header. NULL when hashcode is not a loaded sequence.
sAnimRawSeq_tag* AnimSeqGetInfo(sAnimRawSeq_tag *raw, HASHCODE hashcode, sAnimSeq_tag *seq) {
    seq->hashcode = hashcode;
    if ((hashcode & HASHCODE_SECTION_MASK) != HT_AnimSeq_HASHCODE_BASE) {
        NF_WARN("Can't use animation hash 0x%x", hashcode); // GC check (0x8004dda8)
        return NULL;
    }
    if (raw == NULL) {
        raw = (sAnimRawSeq_tag *)hashtable_getitem(hashcode);
        if (raw == NULL) {
            NF_WARN("Can't use animation hash 0x%x", hashcode); // GC check (0x8004dda8)
            return NULL;
        }
    }
    seq->rawSeq = raw;
    memcpy(seq->boundSphere, raw->boundSphere, sizeof(seq->boundSphere));
    memcpy(seq->rootBox, raw->rootBox, sizeof(seq->rootBox));
    seq->numFrames = raw->numFrames;
    seq->skeletonNum = raw->skeletonNum;
    seq->raw3e = raw->raw3e;
    seq->numElems = raw->numElems;
    seq->flags ^= (raw->flags ^ seq->flags) & 0xf;
    return raw;
}

// The distance the root walks, over the axes asked for, by the end of each frame of a sequence: frame 2 against 1
// on to the last against the one before, then, unless noWrap, frame 2 against 1 again. A step is at least
// DISTANCE_MIN_STEP.
AnimDistanceTable* AnimDistanceTableCreate(uint axes, HASHCODE hashcode, int noWrap) {
    sAnimSeq_tag seq;
    memset(&seq, 0, sizeof(seq));
    if ((hashcode & HASHCODE_SECTION_MASK) != HT_AnimSeq_HASHCODE_BASE)
        return NULL;
    AnimSeqGetInfo(NULL, hashcode, &seq);

    short numFrames = seq.numFrames;
    seq.firstFrame = 1;
    seq.lastFrame = numFrames;
    int count = numFrames;
    if (noWrap)
        count--;

    AnimDistanceTable *table = (AnimDistanceTable *)Mem_Malloc(count * sizeof(float) + offsetof(AnimDistanceTable, edge),
                                                               MALLOC_ANIM_DISTANCE_TABLE, 0);
    if (table == NULL)
        return NULL;

    float length = 0.0f;
    float pose[DISTANCE_POSE_FLOATS];
    float prevPose[DISTANCE_POSE_FLOATS];
    for (int i = 0, next = 2; i < count; i++, next++) {
        int frame = next;
        if (!noWrap && frame > count)
            frame = 2;
        AnimFrameCopy(pose, &seq, NULL, (float)frame);
        _VECTOR at = *(_VECTOR *)pose;
        AnimFrameCopy(prevPose, &seq, NULL, (float)(frame - 1));
        _VECTOR before = *(_VECTOR *)prevPose;
        _VECTOR step;
        Vec_Subtract(&at, &before, &step);
        if (!(axes & DISTANCE_AXIS_X))
            step.x = 0.0f;
        if (!(axes & DISTANCE_AXIS_Y))
            step.y = 0.0f;
        if (!(axes & DISTANCE_AXIS_Z))
            step.z = 0.0f;
        double stepLength = Vec_Magnitude(&step);
        if (stepLength < DISTANCE_STEP_EPSILON)
            stepLength = DISTANCE_MIN_STEP;
        length = (float)(stepLength + length);
        table->edge[i] = length;
    }

    table->length = length;
    table->count = count;
    table->unknown08 = 0.0f;
    return table;
}

// The distance table of a sequence, made the first time; NULL once 32 are made
AnimDistanceTable* AnimDistanceTableGet(HASHCODE hashcode) {
    for (int i = 0; i < nDistanceTables; i++) {
        if (DistanceTables[i].hashcode == hashcode)
            return DistanceTables[i].table;
    }
    if (nDistanceTables >= NUM_DISTANCE_TABLES)
        return NULL;
    AnimDistanceTable *table = AnimDistanceTableCreate(DISTANCE_AXIS_Z, hashcode, 0);
    DistanceTables[nDistanceTables].hashcode = hashcode;
    DistanceTables[nDistanceTables].table = table;
    nDistanceTables++;
    return table;
}

// Starts the sequence hashcode in the script's slot (0 the body, 1 the face) at frame start: its root motion and
// its first translation start afresh. The body's sequence gives the object its bound sphere.
sAnimSeq_tag* AnimSeqSet(sAnimScript_tag *script, HASHCODE hashcode, obj_tag *obj, AnimObj *animObj,
                         sAnimRawSeq_tag *raw, int start, int slot) {
    if ((hashcode & HASHCODE_SECTION_MASK) != HT_AnimSeq_HASHCODE_BASE)
        return NULL;
    sAnimSeq_tag *seq = (&script->boneAnimSeq)[slot];
    if (seq == NULL)
        return NULL;
    sAnimSkin_tag *skin = animObj->skin;
    if (skin == NULL || AnimSeqGetInfo(raw, hashcode, seq) == NULL) {
        NF_WARN("Anim sequence 0x%x not loaded", hashcode); // GC check (0x80049d90)
        return NULL;
    }

    seq->frame = (float)start;
    seq->firstFrame = 1;
    seq->lastFrame = seq->numFrames;
    Vec_Zero(&seq->rootDelta);
    Vec_Copy(&obj->position, &seq->objPosAtSet);
    Vec_Zero(&seq->firstTrans);
    Vec_Zero(&seq->transRelFirst);
    seq->flags = (seq->flags & 0x1efffff) | ANIMSEQ_STARTED;
    if (seq->flags & ANIMSEQ_DISTANCE_AXES)
        seq->distanceTable = AnimDistanceTableGet(hashcode);
    if (slot == 0 && obj->objGraphics != NULL)
        memcpy(animObj->boundSphere, seq->boundSphere, sizeof(animObj->boundSphere));
    seq->skeletonNum = skin->skeletonNum;
    return seq;
}

// An empty script with a body sequence (and a face sequence if the skin has morphs), linked in after after, or on
// its own without after. NULL if an allocation fails.
sAnimScript_tag* AnimScriptNew(obj_tag *obj, sAnimScript_tag *after) {
    if (obj == NULL)
        return NULL;
    sAnimScript_tag *script = (sAnimScript_tag *)Mem_Malloc(sizeof(sAnimScript_tag), MALLOC_ANIM_SCRIPT_STRUCTS, 0);
    if (script == NULL)
        return NULL;
    memset(script, 0, sizeof(sAnimScript_tag));
    script->magicStateIndicator = ANIM_SCRIPT_STATE_MAYBE_INITIALISED;

    uint numBones = SkeletonTable[obj->animState->animObj.skin->skeletonNum]->numBones;
    script->boneAnimSeq = AnimSeqNew(numBones * (sizeof(_VECTOR) + sizeof(quaternion_tag)),
                                     MALLOC_ANIM_SEQ_DATA_BUFFERS);
    if (script->boneAnimSeq != NULL
        && (obj->animState->animObj.skin->numMorphs == 0
            || (script->morphAnimSeq = AnimSeqNew(ANIM_MORPH_POSE_SIZE, MALLOC_ANIM_MORPH_DATA_BUFFERS)) != NULL)) {
        if (after == NULL) {
            script->prevScript = NULL;
            script->nextScript = NULL;
        } else {
            sAnimScript_tag *next = after->nextScript;
            script->prevScript = after;
            script->nextScript = next;
            after->nextScript = script;
            if (next != NULL)
                next->prevScript = script;
        }
        return script;
    }

    AnimSeqDelete(&script->morphAnimSeq);
    AnimSeqDelete(&script->boneAnimSeq);
    Mem_Free((void **)&script);
    return NULL;
}

// Starts the 0x06 script hashcode in a new script: its frame count, end and play rate, playing from the start, or
// backwards from the end after AnimScriptAddReverse
void AnimScriptInit(obj_tag *obj, sAnimScript_tag *script, HASHCODE hashcode) {
    char reverse = ReverseFlag;
    ReverseFlag = 0;
    if (obj == NULL || obj->animState == NULL || script == NULL ||
        (hashcode & HASHCODE_SECTION_MASK) != HT_Script_HASHCODE_BASE)
        return;
    AnimObj *animObj = &obj->animState->animObj;
    uchar *data = (uchar *)hashtable_getitem(hashcode);
    script->scriptData = data;
    if (data == NULL) {
        NF_WARN("Anim script 0x%x not loaded", hashcode); // GC check (0x8004858c)
        return;
    }

    script->hashcode = hashcode;
    script->firedMask = 0;
    script->flags = (script->flags & 0x80fffff) | ANIMSCRIPT_STARTED;
    script->numFrames = *(short *)(data + 4);
    ushort info = *(ushort *)(data + 6);
    if (info & 0x8000)
        script->flags |= ANIMSCRIPT_LOOP;
    script->numCmds = (uchar)info;
    script->rate = (info >> 9) & 0x3f;
    // GC checks (0x8004858c)
    NF_WARN_IF(script->numCmds == 0, "Script 0x%x has no commands", hashcode);
    NF_WARN_IF(script->numCmds > ANIM_SCRIPT_MAX_CMDS, "Script 0x%x has too many commands", hashcode);
    if (!reverse) {
        script->curFrame = 1.0f;
        script->speed = 1.0f;
    } else {
        script->speed = -1.0f;
        script->curFrame = script->numFrames;
    }
    script->prevFrame = script->curFrame;
    script->speedMode = ANIMSPEED_TIME;
    script->playMode = ANIMPLAY_ONCE;
    script->onDelete = NULL;
    script->ownerSlot = NULL;
    script->onStop = NULL;
    script->doneFlag = NULL;
    script->onEvent = NULL;
    if (AnimScriptTimeStamp == 0)
        AnimScriptTimeStamp = 1;
    script->timestamp = AnimScriptTimeStamp++;
    script->groupTag = 0;
    if (ScriptCam != 0 && animObj->skin->numMorphs != 0)
        script->groupTag = ANIMTAG_FACE_SPEECH;
    script->fadeLen = 1.0f;
    script->fadeTime = 1.0f;
    script->fadeDir = 1;
    script->syncBlend = 0.0f;
    script->targetPos = 0.0f;
    script->boneMask = NULL;
}

// Replaces all the object's scripts with hashcode
// AUTOINJECT
sAnimScript_tag* AnimScriptAdd(obj_tag *obj, HASHCODE hashcode) {
    if (obj == NULL || obj->animState == NULL)
        return NULL;
    AnimState *animState = obj->animState;
    AnimListBuild(obj, ANIMLIST_ALL, 0, NULL, ANIMLIST_RESTART);
    AnimListDelete(obj);
    sAnimScript_tag *script = AnimScriptNew(obj, NULL);
    animState->animObj.firstScript = script;
    AnimScriptInit(obj, script, hashcode);
    return script;
}

// AUTOINJECT
sAnimScript_tag* AnimScriptAddSpeed(obj_tag *obj, HASHCODE hashcode, float speed) {
    sAnimScript_tag *script = AnimScriptAdd(obj, hashcode);
    if (script != NULL)
        script->speed = speed;
    return script;
}

// AUTOINJECT
sAnimScript_tag* AnimScriptAddReverse(obj_tag *obj, HASHCODE hashcode) {
    ReverseFlag = 1;
    return AnimScriptAdd(obj, hashcode);
}

// The info word of a script that does not loop and plays at 60 frames a second
#define ANIMSCRIPT_INFO_ONCE_60 0x7800

// The sequence a script's first command plays, without starting the script; NULL unless the script is loaded and
// plays once at 60 frames a second
// AUTOINJECT
sAnimRawSeq_tag* AnimScriptPeekSeq(HASHCODE script) {
    uchar *data = (uchar *)hashtable_getitem(script);
    if (data == NULL)
        return NULL;
    // The data is not aligned: the hashcode a byte at a time
    uint hashcode = data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24;
    if ((hashcode & HASHCODE_SECTION_MASK) != HT_Script_HASHCODE_BASE)
        return NULL;
    if ((*(ushort *)(data + 6) & 0xff00) != ANIMSCRIPT_INFO_ONCE_60)
        return NULL;
    ushort *cmd = (ushort *)(data + 8);
    return (sAnimRawSeq_tag *)hashtable_getitem((HASHCODE)(cmd[3] | HT_AnimSeq_HASHCODE_BASE));
}

// Adds hashcode after the object's other scripts, up to 32 of them
// AUTOINJECT
sAnimScript_tag* AnimScriptAppend(obj_tag *obj, HASHCODE hashcode) {
    if (obj == NULL || obj->animState == NULL)
        return NULL;
    AnimObj *animObj = &obj->animState->animObj;
    sAnimScript_tag *script;
    if (animObj->firstScript == NULL) {
        script = AnimScriptNew(obj, NULL);
        animObj->firstScript = script;
    } else {
        sAnimScript_tag *last = animObj->firstScript;
        int count = 1;
        while (last->nextScript != NULL) {
            last = last->nextScript;
            count++;
        }
        if (count >= 32)
            return NULL;
        script = AnimScriptNew(obj, last);
    }
    AnimScriptInit(obj, script, hashcode);
    return script;
}

// As AnimScriptAppend, with the scripts playing faded out over eight frames
// AUTOINJECT
sAnimScript_tag* AnimScriptAppendBlend(obj_tag *obj, HASHCODE hashcode) {
    AnimListBuild(obj, ANIMLIST_ALL, 0, NULL, ANIMLIST_RESTART);
    AnimListBuild(obj, ANIMLIST_MUTE_MORPH, 0, NULL, ANIMLIST_REMOVE);
    for (int i = 0; i < GlbAnimListCount; i++) {
        sAnimScript_tag *script = GlbAnimList[i];
        if (!(script->flags & ANIMSCRIPT_FADING)) {
            script->flags |= ANIMSCRIPT_FADING | ANIMSCRIPT_FADE_OUT;
            script->fadeLen = 8.0f;
            script->fadeTime = 8.0f;
            script->fadeDir = -1;
        }
    }
    return AnimScriptAppend(obj, hashcode);
}

// 0 plays the first script forwards, 1 backwards, 2 the other way
// AUTOINJECT
void AnimScriptSetPlayDirection(obj_tag *obj, int direction) {
    if (obj == NULL || obj->animState == NULL)
        return;
    sAnimScript_tag *script = obj->animState->animObj.firstScript;
    if (script == NULL || (script->flags & ANIMSCRIPT_STOPPED))
        return;
    switch (direction) {
    case 0:
        if (!(script->speed >= 0.0f))
            script->speed = -script->speed;
        break;
    case 1:
        if (script->speed >= 0.0f)
            script->speed = -script->speed;
        break;
    case 2:
        script->speed = -script->speed;
        break;
    }
}

// AUTOINJECT
void AnimScriptDelete(obj_tag *obj, sAnimScript_tag **slot) {
    sAnimScript_tag *script = *slot;
    if (obj == NULL || obj->animState == NULL || script == NULL)
        return;
    AnimObj *animObj = &obj->animState->animObj;

    if (script == Glb_pCurrentScript)
        Glb_bScriptDeleted = 1;
    if (script->onDelete != NULL)
        script->onDelete(obj, animObj, script);
    if (script->ownerSlot != NULL && script->ownerSlot != slot)
        *script->ownerSlot = NULL;
    AnimSeqDelete(&script->morphAnimSeq);
    AnimSeqDelete(&script->boneAnimSeq);

    sAnimScript_tag *prev = script->prevScript;
    sAnimScript_tag *next = script->nextScript;
    if (prev == NULL)
        animObj->firstScript = next;
    else
        prev->nextScript = next;
    if (next != NULL)
        next->prevScript = prev;
    script->magicStateIndicator = ANIM_SCRIPT_STATE_MAYBE_DELETED;
    Mem_Free((void **)slot);
}

// Whether any of the object's scripts has stopped
// AUTOINJECT
int AnimScriptIsStopped(obj_tag *obj) {
    AnimListBuild(obj, ANIMLIST_ALL, 0, NULL, ANIMLIST_RESTART);
    int count = GlbAnimListCount;
    int running = 0;
    for (int i = 0; i < count; i++) {
        if (!(GlbAnimList[i]->flags & ANIMSCRIPT_STOPPED))
            running++;
    }
    return running != count;
}

// AUTOINJECT
void AnimScriptCallOnEvent(sAnimScript_tag *script, AnimScriptEventFunc onEvent) {
    script->onEvent = onEvent;
}

// AUTOINJECT
void AnimScriptCallOnStop(sAnimScript_tag *script, AnimScriptStopFunc onStop) {
    script->onStop = onStop;
}

// Whether the root's turn turns the object; switching it on takes the root's yaw afresh.
// AUTOINJECT
void AnimScriptSetAnimRotation(sAnimScript_tag *script, char on) {
    if (on) {
        script->flags |= ANIMSCRIPT_ANIM_ROTATION;
        script->rotBaseYaw = 8.0f;
    } else {
        script->flags &= ~ANIMSCRIPT_ANIM_ROTATION;
    }
}

// Brings a frame past either end back in, by the script's end: stopped (its done flag set, onStop called once),
// looped or bounced. The frame, also the script's; 1.0 without it when onStop deleted the script.
double AnimScriptEnd(sAnimScript_tag *script, obj_tag *obj, AnimObj *animObj, float frameIn) {
    uint flags = script->flags;
    if (flags & ANIMSCRIPT_LOOP)
        script->playMode = ANIMPLAY_LOOP;

    double frame = frameIn;
    if (!(frame < 1.0) && !(frame > script->numFrames)) {
        script->curFrame = (float)frame;
        return frame;
    }

    bool backwards = script->speed < 0.0f;
    switch (script->playMode) {
    case ANIMPLAY_ONCE:
    case ANIMPLAY_DELETE: {
        script->flags = flags | ANIMSCRIPT_STOPPED;
        frame = backwards ? 1.0 : script->numFrames;
        float stoppedAt = (float)frame;
        if (script->doneFlag != NULL) {
            *script->doneFlag = 1;
            script->doneFlag = NULL;
        }
        if (script->onStop != NULL) {
            script->onStop(obj, animObj, script);
            if (Glb_bScriptDeleted)
                return 1.0;
            frame = stoppedAt;
            script->onStop = NULL;
        }
        break;
    }
    case ANIMPLAY_LOOP:
        if (backwards)
            frame += script->numFrames;
        else
            frame -= script->numFrames;
        script->flags = flags | ANIMSCRIPT_WRAPPED;
        break;
    case ANIMPLAY_PINGPONG:
        frame = backwards ? 1.0 : script->numFrames;
        script->speed = -script->speed;
        frame += script->speed;
        break;
    }

    if (frame < 1.0) {
        script->curFrame = 1.0f;
        return 1.0;
    }
    float last = script->numFrames;
    if (frame > last)
        frame = last;
    script->curFrame = (float)frame;
    return frame;
}

// How many frames the walk script must move for the distance the object walked this tick, by its sequence's
// distance table (cumulative distance per frame). Sets the object's distance phase.
double AnimDistanceTableDistanceToFrame2(obj_tag *obj, sAnimScript_tag *script, AnimObj *animObj) {
    if (obj == NULL || obj->animState == NULL || script->boneAnimSeq == NULL)
        return 0.0;
    AnimDistanceTable *table = script->boneAnimSeq->distanceTable;
    if (table == NULL)
        return 0.0;

    double distance = (double)animObj->distancePhase + animObj->distanceThisTick;
    double length = table->length;
    while (distance >= length)
        distance -= length;
    while (distance < 0.0)
        distance += length;

    float count = (float)table->count;
    int frame = Ftol((double)table->count / length * distance);
    if (distance < table->edge[frame]) {
        while (frame != 0 && distance < table->edge[frame - 1])
            frame--;
    } else {
        do {
            frame++;
        } while (distance >= table->edge[frame]);
    }

    float distanceF = (float)distance;
    double along, span;
    if (frame > 0) {
        along = distance - table->edge[frame - 1];
        span = (double)table->edge[frame] - table->edge[frame - 1];
    } else {
        along = distanceF;
        span = table->edge[0];
    }
    animObj->distancePhase = distanceF;
    float target = (float)(along / span + (frame + 1));
    float current = script->curFrame;
    double frames = (double)target - current;
    if (!(animObj->distanceThisTick >= 0.0f)) {
        if (!(frames >= 0.0))
            return frames;
        return -(((double)count - target) + current);
    }
    if (!(frames < 0.0))
        return frames;
    return ((double)count - current) + target;
}

// Whether frame f was passed going from from to to (going forwards or not, round the end): FUN_00012a20, which takes
// its arguments in registers; only AnimProcessScriptCmds calls it. INVENTED NAME.
static bool AnimFramesPassed(int from, int to, int start, int end, bool forward) {
    if (from >= start && from <= end)
        return true;
    if (to >= start && to <= end)
        return true;
    if (forward) {
        if (from <= to)
            return (start >= from && start <= to) || (end >= from && end <= to);
        return start >= from || start <= to || end >= from || end <= to;
    }
    if (from >= to)
        return (start <= from && start >= to) || (end <= from && end >= to);
    return start <= from || start >= to || end <= from || end >= to;
}

// A script's commands
enum {
    ANIMCMD_SEQUENCE = 0,   // the sequence 0x04000000 | [3] from frame [1] to [2]
    ANIMCMD_SOUND = 1,      // the sound [2] at frame [1]
    ANIMCMD_UNUSED_2 = 2,
    ANIMCMD_UNUSED_3 = 3,
    ANIMCMD_EVENT = 4,      // the event [2] (with [3]) at frame [1]
};

// ANIMCMD_EVENT's events
enum {
    ANIMEVENT_STEP = 0,             // the other foot
    ANIMEVENT_LASER = 1,            // the holder's laser sight on or off
    ANIMEVENT_CALLBACK = 2,
    ANIMEVENT_STOP_SOUND = 3,
    ANIMEVENT_STEP_LEFT = 4,
    ANIMEVENT_STEP_RIGHT = 5,
    ANIMEVENT_EFFECT = 6,
    ANIMEVENT_FIRE = 7,             // the holder's weapon fires
};

// Carries out the commands whose frames the script passed between from and to, once each until it leaves them
// AUTOINJECT
void AnimProcessScriptCmds(obj_tag *obj, AnimObj *animObj, sAnimScript_tag *script, int from, int to) {
    bool forward = script->speed >= 0.0f;
    bool passed = false;
    int seqsSet = 0;
    bool muted = (animObj->flags & ANIMOBJ_MUTE) != 0;
    ushort *cmd = (ushort *)(script->scriptData + 8);

    for (int i = script->numCmds - 1; i >= 0; i--) {
        uint bit = 1u << (i & 31);
        uint length = cmd[0] >> 8;
        uint fired = script->firedMask & bit;

        switch (cmd[0] & 0xff) {
        case ANIMCMD_SEQUENCE: {
            passed = AnimFramesPassed(from, to, cmd[1], cmd[2], forward);
            if (passed && !fired) {
                HASHCODE hashcode = (HASHCODE)(cmd[3] | HT_AnimSeq_HASHCODE_BASE);
                sAnimRawSeq_tag *raw = (sAnimRawSeq_tag *)hashtable_getitem(hashcode);
                if (raw == NULL) {
                    NF_WARN("Anim Seq 0x%x not loaded", hashcode); // GC check (0x80048f98)
                    return;
                }
                bool morph = (raw->flags & ANIMSEQ_MORPH) != 0;
                if (!morph || !(script->flags & ANIMSCRIPT_NO_MORPH)) {
                    int start = (forward ? to : from) - cmd[1] + 1;
                    sAnimSeq_tag *seq = AnimSeqSet(script, hashcode, obj, animObj, raw, start, morph ? 1 : 0);
                    seq->startTime = cmd[1];
                    script->flags &= ~ANIMSCRIPT_MUTE_MORPH;
                    if (seqsSet == 0 && morph)
                        script->flags |= ANIMSCRIPT_MUTE_MORPH;
                    seq->flags &= ~ANIMSEQ_UNPAK_CACHE;
                    if ((script->flags & ANIMSCRIPT_SCRIPT_DRIVEN) && !morph && seq->numFrames > 0x100) {
                        seq->flags |= ANIMSEQ_UNPAK_CACHE;
                        if (seq->unpakCache == NULL) {
                            uint size = seq->numElems * sizeof(AnimUnpakEntry);
                            seq->unpakCache = (AnimUnpakEntry *)Mem_Malloc(size, MALLOC_ANIM_UNPAK_OPT, 0);
                            if (seq->unpakCache != NULL)
                                memset(seq->unpakCache, 0, size);
                        }
                    }
                    seqsSet++;
                }
            }
            length += 4;
            break;
        }

        case ANIMCMD_SOUND: {
            passed = AnimFramesPassed(from, to, cmd[1], cmd[1], forward);
            if (passed && !muted && !fired) {
                // Some sounds have another for the holder's weapon
                ushort sound = cmd[2];
                obj_tag *holder = obj->parent;
                if (sound == 1) {
                    if (holder != NULL && (holder->animState->currentWeaponId == 7
                                           || holder->animState->currentWeaponId == 9))
                        sound = 2;
                } else if (sound == 0x403) {
                    if (holder != NULL && (holder->animState->currentWeaponId == 3
                                           || holder->animState->currentWeaponId == 5))
                        sound = 2;
                } else if (sound == 0xfa) {
                    if (holder != NULL && holder->animState->currentWeaponId == 0x14)
                        sound = 0x1fb;
                } else if (sound == 0x106) {
                    if (holder != NULL && holder->animState->currentWeaponId >= 0x1e
                        && holder->animState->currentWeaponId <= 0x23)
                        sound = 0x284;
                } else if (sound == 0x1e1) {
                    if (holder != NULL && holder->animState->currentWeaponId == 0x10)
                        sound = 0x602;
                } else if (sound == 4) {
                    if (holder != NULL && (holder->animState->currentWeaponId == 0x16
                                           || holder->animState->currentWeaponId == 0x17))
                        sound = 0x604;
                }

                _VECTOR pos;
                holder = obj->parent;
                if (holder == NULL) {
                    Vec_Copy(&obj->position, &pos);
                } else {
                    ApplyMatrixLV(&holder->transformMatrix, &obj->position, &pos);
                    Vec_Add2(&holder->position, &pos, &pos);
                    // The player's gun sounds from its muzzle
                    if (holder->objectType == OBJECTTYPE_PLAYER
                        && obj == ((BLData *)holder->extraObjectData)->weaponObject) {
                        if (holder->animState->currentWeaponId == 1 || AnimDatumGetIndex(obj, 0) < 0) {
                            _VECTOR ahead = {0.0f, 0.0f, 1.5f};
                            _VECTOR offset;
                            ApplyMatrixLV(&obj->transformMatrix, &ahead, &offset);
                            Vec_Add2(&offset, &pos, &pos);
                        } else {
                            AnimGetBoneWorldTrans(obj, 0, 0, &pos, NULL);
                        }
                    }
                }
                ushort pitch = (ushort)(Rand_Rand(500) - 250);
                Sound_Play3D((Action_SFX)sound, &pos, 100.0f, 100.0f, 0.0f, pitch, 0, 0);
            }
            length += 3;
            break;
        }

        case ANIMCMD_UNUSED_2:
            length += 3;
            break;

        case ANIMCMD_EVENT: {
            passed = AnimFramesPassed(from, to, cmd[1], cmd[1], forward);
            if (passed && !muted && !fired) {
                switch (cmd[2]) {
                case ANIMEVENT_STEP:
                case ANIMEVENT_STEP_LEFT:
                case ANIMEVENT_STEP_RIGHT:
                    if (cmd[2] == ANIMEVENT_STEP)
                        FootstepFoot = !FootstepFoot;
                    else
                        FootstepFoot = cmd[2] == ANIMEVENT_STEP_RIGHT;
                    // The two scripts whose footsteps are of type 1
                    if (script->hashcode == HT_Script_AnimScriptFootstep1
                        || script->hashcode == HT_Script_AnimScriptFootstep1B)
                        FootstepType = 1;
                    else
                        FootstepType = 2;
                    break;
                case ANIMEVENT_LASER:
                    if (obj->parent != NULL) {
                        BLData *blData = (BLData *)obj->parent->extraObjectData;
                        blData->laserSight = blData->laserSight == 0;
                    }
                    break;
                case ANIMEVENT_CALLBACK:
                    if (script->onEvent != NULL)
                        script->onEvent(obj, animObj, script, cmd[3]);
                    break;
                case ANIMEVENT_STOP_SOUND:
                    Sound_StopAllWithId((Action_SFX)cmd[3]);
                    break;
                case ANIMEVENT_EFFECT:
                    Effect_Create(&obj->animState->floor, obj, 0xc0, 1.0f);
                    break;
                case ANIMEVENT_FIRE: {
                    obj_tag *holder = obj->parent != NULL ? obj->parent : obj;
                    weapon_definition_tag *weapon = &weapon_data[holder->animState->currentWeaponId];
                    HASHCODE flash;
                    ushort weaponSound;
                    int datum = AnimDatumGetWeaponInfo(holder, NULL, &flash, &weaponSound);
                    if (datum < 0)
                        break;
                    // Without a pose or the datum, Mat_Identity leaves the matrix's translation, uninitialised stack
                    // in the original and 0 in ours (docs/nightfire-port-lifts.md, Departures)
                    _VECTOR muzzle;
                    _MATRIX muzzleMtx = {};
                    AnimGetBoneWorldTrans(holder, datum, 1, &muzzle, &muzzleMtx);
                    _VECTOR ahead = {0.0f, 0.0f, 1.0f};
                    _VECTOR dir;
                    MatrixMultiplyVector(&muzzleMtx, &ahead, &dir);
                    Bullet_init(-1, holder, holder, weapon, &muzzle, &dir, weaponSound);
                    if (flash == 0)
                        break;
                    AnimDatumSetEntity(holder, datum + 1, flash, 3);
                    Light_Create(&muzzle, weapon->muzzleFlash_r, weapon->muzzleFlash_g, weapon->muzzleFlash_b,
                                 weapon->muzzleLightRadius, 0, 3, LIGHT_TRANSIENT, 2.0f, 0, 0, 0, SFX_NONE);
                    if (!(weapon->weaponFlags & WPNFLAG_NO_SMOKE)) {
                        _VECTOR spin;
                        spin.x = 0.0f;
                        spin.y = 0.0f;
                        spin.z = 0.0f;
                        spin.z = (float)Float_FRand(TWO_PI);
                        Gas_Init(obj->inCel, &muzzle, &spin, NULL, 0.05f, 0.1f, HT_Entity_smoke_1_dust, GAS_WIND,
                                 0x48403080,
                                 1, 0x80);
                    }
                    break;
                }
                }
            }
            length += 3;
            break;
        }

        default:
            NF_WARN("Script 0x%x invalid command 0x%x data 0x%x", script->hashcode, cmd[0] & 0xff, length); // GC check (0x80048f98)
            break;
        }

        cmd += length;
        if (passed)
            script->firedMask |= bit;
        else
            script->firedMask &= ~bit;
    }
}

// Moves a script on a tick: its first frame's commands the first time, its speed by its speed mode, its end, and the
// commands it passed. A script played once with ANIMPLAY_DELETE goes when it stops.
void AnimScriptTick(AnimObj *animObj, obj_tag *obj, sAnimScript_tag *script) {
    float speed = script->speed;
    script->flags &= ~ANIMSCRIPT_WRAPPED;
    script->flags |= ANIMSCRIPT_HAS_ROOT_TRANS;
    bool first = (script->flags & ANIMSCRIPT_STARTED) != 0;
    if (first)
        AnimProcessScriptCmds(obj, animObj, script, Ftol(script->prevFrame), Ftol(script->curFrame));

    double step;
    switch (script->speedMode) {
    case ANIMSPEED_TIME:
        step = (double)speed * FRAME_RATE_MUL;
        break;
    case ANIMSPEED_DISTANCE:
        script->flags |= ANIMSCRIPT_LOOP;
        script->speed = (float)AnimDistanceTableDistanceToFrame2(obj, script, animObj);
        step = script->speed;
        break;
    case ANIMSPEED_SYNC: {
        // As far through as the master, the short way round
        sAnimScript_tag *master = script->syncMaster;
        float masterPos = (float)(((double)master->curFrame - 1.0) / (master->numFrames - 1));
        double span = script->numFrames - 1;
        float pos = (float)(((double)script->curFrame - 1.0) / span);
        double gap = (double)masterPos - pos;
        if (!(master->speed >= 0.0f)) {
            if (gap >= 1.0)
                gap = ((double)pos + 1.0) - masterPos;
        } else if (gap < 0.0) {
            gap = (1.0 - pos) + masterPos;
        }
        script->speed = (float)(span * gap + 1.0);
        first = false;
        step = script->speed;
        break;
    }
    case ANIMSPEED_SEEK:
        script->speed = (float)((double)(script->numFrames + 1) * script->targetPos - script->curFrame);
        first = false;
        step = script->speed;
        break;
    default:
        step = speed;
        break;
    }

    int prevFrame = Ftol(script->prevFrame);
    float frame = script->curFrame;
    if (!first)
        frame = (float)(frame + step);
    double newFrame = AnimScriptEnd(script, obj, animObj, frame);
    if (Glb_bScriptDeleted)
        return;
    AnimProcessScriptCmds(obj, animObj, script, prevFrame, Ftol(newFrame));
    if (script->playMode == ANIMPLAY_DELETE && (script->flags & ANIMSCRIPT_STOPPED))
        AnimScriptDelete(obj, &script);
}

// ---------------------------------------------------------------------------------------------------------------
// Datums

// The index in the skin's datums of the one with id, -1 for none
// AUTOINJECT
int AnimDatumGetIndex(obj_tag *obj, int id) {
    if (obj == NULL || obj->animState == NULL)
        return -1;
    sAnimSkin_tag *skin = obj->animState->animObj.skin;
    for (int i = 0; i < skin->numDatums; i++) {
        if (skin->datums[i].id == id)
            return i;
    }
    return -1;
}

// Draws entity at the datum id for frames frames (0xff for ever); with no entity, nothing
// AUTOINJECT
void AnimDatumSetEntity(obj_tag *obj, int id, uint entity, uchar frames) {
    if (obj == NULL || obj->animState == NULL)
        return;
    AnimObj *animObj = &obj->animState->animObj;
    int index = AnimDatumGetIndex(obj, id);
    if (index < 0)
        return;
    if (entity != 0) {
        animObj->datumGlist[index] = hashtable_hashcode_to_celglist((HASHCODE)entity);
        animObj->datumTimer[index] = frames;
    } else {
        animObj->datumTimer[index] = 0;
    }
}

// The held weapon's model, its third-person flash and sound; its first datum, -1 for none
// AUTOINJECT
int AnimDatumGetWeaponInfo(obj_tag *obj, HASHCODE *gfx, HASHCODE *flash, ushort *sound) {
    uint weapon = (uint)(int)obj->animState->currentWeaponId;
    if (weapon > 0x72)
        return -1;
    if (gfx != NULL)
        *gfx = weapon_data[weapon].worldModelGfx;
    if (flash != NULL)
        *flash = weapon_data[weapon].muzzleFlash3rdPerson;
    if (sound != NULL)
        *sound = weapon_data[weapon].fireSound3rdPerson;
    if (weapon_data[weapon].weaponDatum == 0xff)
        return -1;
    return weapon_data[weapon].weaponDatum;
}

// ---------------------------------------------------------------------------------------------------------------
// Faces

// A speech script on the face: deleted when it ends, and the face talks till then
// AUTOINJECT
sAnimScript_tag* AnimFacialAppend(obj_tag *obj, HASHCODE hashcode) {
    AnimObj *animObj = &obj->animState->animObj;
    sAnimScript_tag *script = AnimScriptAppend(obj, hashcode);
    script->playMode = ANIMPLAY_DELETE;
    script->groupTag = ANIMTAG_FACE_SPEECH;
    animObj->facialState = FACIAL_TALKING;
    return script;
}

// The face between speeches: an expression for the character's alert status, and blinks, some quick, some slow
void AnimFacialUpdate(AnimObj *animObj, obj_tag *obj) {
    if ((animObj->flags & ANIMOBJ_NO_FACE) || ScriptCam != 0)
        return;
    bool altSkeleton = (animObj->flags & ANIMOBJ_ALT_SKELETON) != 0;
    const HASHCODE *anims = altSkeleton ? FacialAnimsAltSkeleton : FacialAnims;

    switch (animObj->facialState & ~FACIAL_QUICK_BLINK) {
    case FACIAL_START: {
        sAnimScript_tag *mood = AnimScriptAppend(obj, anims[0]);
        mood->playMode = ANIMPLAY_DELETE;
        mood->speed = 0.0f;
        mood->groupTag = ANIMTAG_FACE_MOOD;
        animObj->facialState = FACIAL_IDLE;
        animObj->facialTimer = Rand_Rand(FRAME_RATE_INT * 3) + FRAME_RATE_INT * 3 / 2;
        break;
    }

    case FACIAL_IDLE: {
        bool blinks = true;
        bool alerted = true;
        uchar *alert = Drone_AlertStatus(obj);
        if (alert != NULL) {
            if (alert[2]) {
                alert[2] = 0;
                AnimListBuild(obj, ANIMLIST_MUTE_MORPH, 0, NULL, ANIMLIST_RESTART);
                AnimListDelete(obj);
                sAnimScript_tag *mood = AnimScriptAppend(obj, anims[alert[0]]);
                mood->playMode = ANIMPLAY_DELETE;
                mood->speed = 0.0f;
                mood->groupTag = ANIMTAG_FACE_MOOD;
                return;
            }
            if (alert[0] == 3)
                blinks = false;
            if (alert[0] == 0)
                alerted = false;
            if (!blinks)
                return;
        }

        if ((short)--animObj->facialTimer >= 0)
            return;
        if (alerted)
            animObj->facialTimer = Rand_Rand(FRAME_RATE_INT * 2) + FRAME_RATE_INT;
        else
            animObj->facialTimer = Rand_Rand(FRAME_RATE_INT * 2) + FRAME_RATE_INT * 2;
        sAnimScript_tag *blink = AnimScriptAppend(obj, anims[4]);
        blink->playMode = ANIMPLAY_DELETE;
        blink->groupTag = ANIMTAG_FACE_BLINK;
        if (altSkeleton)
            return;
        if (animObj->facialState & FACIAL_QUICK_BLINK) {
            animObj->facialState &= ~FACIAL_QUICK_BLINK;
            return;
        }
        uint chance = Rand_Rand(100);
        if (chance <= 50)
            return;
        if (chance > 75) {
            blink->speed = (float)(Float_FRand(0.2f) + 0.2f);
            return;
        }
        animObj->facialTimer = Rand_Rand(FRAME_RATE_INT / 10) + FRAME_RATE_INT / 10;
        animObj->facialState |= FACIAL_QUICK_BLINK;
        break;
    }

    case FACIAL_TALKING:
        if (AnimListBuild(obj, ANIMLIST_TAG, ANIMTAG_FACE_SPEECH, NULL, ANIMLIST_RESTART) == NULL) {
            animObj->facialState = FACIAL_IDLE;
            uchar *alert = Drone_AlertStatus(obj);
            if (alert != NULL)
                alert[2] = 1;
        }
        break;
    }
}

static constexpr float kByteToUnit = 1.0f / 255.0f;
static_assert(std::bit_cast<uint32_t>(kByteToUnit) == 0x3b808081, "1/255");

// The morph weights from the facial scripts' poses: the expression, and over it the blink and the speech where they
// move a morph by more than 0.01; then the eyes. In a scripted scene, the speech alone.
void AnimFacialBlend(obj_tag *obj, AnimObj *animObj, sAnimSkin_tag *skin) {
    sAnimScript_tag *mood = AnimListBuild(obj, ANIMLIST_TAG, ANIMTAG_FACE_MOOD, NULL, ANIMLIST_RESTART);
    sAnimScript_tag *blink = AnimListBuild(obj, ANIMLIST_TAG, ANIMTAG_FACE_BLINK, NULL, ANIMLIST_RESTART);
    sAnimScript_tag *speech = AnimListBuild(obj, ANIMLIST_TAG, ANIMTAG_FACE_SPEECH, NULL, ANIMLIST_RESTART);
    uint numMorphs = skin->numMorphs;

    if (ScriptCam != 0) {
        if (speech != NULL) {
            for (uint i = 0; i < numMorphs; i++)
                animObj->morphWeights[i] = speech->morphAnimSeq->pose[i];
        }
        return;
    }

    if (mood != NULL) {
        for (uint i = 0; i < numMorphs; i++)
            animObj->morphWeights[i] = mood->morphAnimSeq->pose[i];
    }
    if (blink != NULL) {
        for (uint i = 0; i < numMorphs; i++) {
            float weight = blink->morphAnimSeq->pose[i];
            if (Magnitude(weight) > 0.01f)
                animObj->morphWeights[i] = weight;
        }
    }
    if (speech != NULL) {
        for (uint i = 0; i < numMorphs; i++) {
            float weight = speech->morphAnimSeq->pose[i];
            if (Magnitude(weight) > 0.01f)
                animObj->morphWeights[i] = weight;
        }
    }

    double eyes = animObj->lookMorphX * (double)kByteToUnit;
    if (eyes > 0.01f)
        animObj->morphWeights[MORPH_EYES_X] = (float)eyes;
    eyes = (255 - animObj->lookMorphX) * (double)kByteToUnit;
    if (eyes > 0.01f)
        animObj->morphWeights[MORPH_EYES_X_INV] = (float)eyes;
    eyes = animObj->lookMorphY * (double)kByteToUnit;
    if (eyes > 0.01f)
        animObj->morphWeights[MORPH_EYES_Y] = (float)eyes;
    eyes = (255 - animObj->lookMorphY) * (double)kByteToUnit;
    if (eyes > 0.01f)
        animObj->morphWeights[MORPH_EYES_Y_INV] = (float)eyes;
}

// Whether any view has the object at lod limit or nearer; always in a scripted scene
// AUTOINJECT
bool AnimObjectIsClose(obj_tag *obj, uint limit) {
    if (ScriptCam != 0)
        return true;
    return limit >= AnimObjectNearestLod(obj);
}

// ---------------------------------------------------------------------------------------------------------------
// Sampling: from the sequences to the object's pose

// AnimFrameCopy's channels, by what each evaluates
enum {
    ANIMCH_QUAT_Z = 0,  // and w
    ANIMCH_QUAT_Y = 1,
    ANIMCH_QUAT_X = 2,
    ANIMCH_TRANS_Z = 3,
    ANIMCH_TRANS_Y = 4,
    ANIMCH_TRANS_X = 5,
    ANIMCH_MORPH = 6,
};

// FUN_00013c70: evaluates the channels gathered (four lanes at a time, each acc += t^i * c_i) and stores them
static void AnimFrameEmitChannels(uint count, float **morphOut, _VECTOR **transOut, quaternion_tag **quatOut) {
    if (count != 0) {
        for (int i = 1; i < 6; i++)
            Vec4_MulAdd(AnimFramePoly[i], AnimChannelCoef[i], AnimChannelCoef[0]);
    }

    for (uint lane = 0; lane < count; lane++) {
        float value = AnimChannelCoef[0][lane];
        switch (AnimChannelType[lane]) {
        case ANIMCH_MORPH:
            *(*morphOut)++ = value;
            break;
        case ANIMCH_TRANS_X:
            (*transOut)->x = Glb_SkinScale.x * value;
            break;
        case ANIMCH_TRANS_Y:
            (*transOut)->y = Glb_SkinScale.y * value;
            break;
        case ANIMCH_TRANS_Z:
            (*transOut)->z = Glb_SkinScale.z * value;
            (*transOut)++;
            break;
        case ANIMCH_QUAT_X:
            (*quatOut)->q[0] = value;
            break;
        case ANIMCH_QUAT_Y:
            (*quatOut)->q[1] = value;
            break;
        default: {
            // z, and w from the unit length; a z of 2 or more stands for z - 4 with a negative w
            quaternion_tag *q = *quatOut;
            bool negativeW = !(value < 2.0f);
            q->q[2] = negativeW ? (float)((double)value - 4.0) : value;
            double square = 1.0 - (((double)q->q[2] * q->q[2] + (double)q->q[1] * q->q[1]) + (double)q->q[0] * q->q[0]);
            if (square > 0.0)
                q->q[3] = (float)(negativeW ? -sqrt(square) : sqrt(square));
            else
                q->q[3] = 0.0f;
            (*quatOut)++;
            break;
        }
        }
    }
}

// Decodes one frame of a sequence into a pose (or a morph sequence's weights). Each channel is a polynomial of
// degree up to five in frame - 1, in pieces (keys) of so many frames. The bones the skin does not use are left as
// they are. The root's translation is put back into raw units; firstTrans is taken one past the last translation.
// AUTOINJECT
void AnimFrameCopy(float *out, sAnimSeq_tag *seq, sAnimSkin_tag *skin, float frameIn) {
    double frame = Ftol(frameIn);
    if (frame == -1.0)
        frame = seq->frame;
    float first = seq->firstFrame;
    if (frame < first)
        frame = first;
    float last = seq->lastFrame;
    if (frame > last)
        frame = last;

    double t = frame - 1.0;
    double power = t;
    AnimFramePoly[0] = 1.0f;
    for (int i = 1; i < 6; i++) {
        AnimFramePoly[i] = (float)power;
        power *= t;
    }
    uint keyFrame = Ftol(t);

    SkeletonInfo *skel = SkeletonTable[seq->skeletonNum];
    bool morph = (seq->flags & ANIMSEQ_MORPH) != 0;
    int numElems = morph ? seq->numElems : skel->numBones;
    sAnimRawSeq_tag *raw = seq->rawSeq;
    raw->cursor = raw->trackData;
    uint *trackBase = raw->cursor;
    uint *track = trackBase;

    float *morphOut = out;
    _VECTOR *transOut = (_VECTOR *)out;
    quaternion_tag *quatOut = (quaternion_tag *)(transOut + numElems);
    bool useCache = (seq->flags & ANIMSEQ_UNPAK_CACHE) != 0;
    AnimUnpakEntry *cache = seq->unpakCache - 1;
    uint count = 0;

    for (int b = 0; b < numElems; b++) {
        bool skip = false;
        int channel;
        if (morph) {
            channel = ANIMCH_MORPH;
        } else {
            // Never four channels across two bones
            if (count > 0) {
                AnimFrameEmitChannels(count, &morphOut, &transOut, &quatOut);
                count = 0;
            }
            if (skin != NULL && !(skin->bones[b] & ANIM_BONE_USED)) {
                transOut++;
                quatOut++;
                skip = true;
            }
            if (skel->transMask[b >> 5] & (1u << (b & 31))) {
                channel = ANIMCH_TRANS_X;
            } else {
                if (!skip) {
                    Vec_Copy(&skel->boneOffset[b], transOut);
                    Vec_Mul(transOut, &Glb_SkinScale);
                    transOut++;
                }
                channel = ANIMCH_QUAT_X;
            }
        }

        for (;;) {
            uint head = *track;
            uint *next = (uint *)((uchar *)track + (head & 0xffff));
            track++;

            if (!skip) {
                // The key the frame is in; the head's high half is the first key word
                uint pending = head;
                bool havePending = true;
                uint frames = 0;
                if (useCache) {
                    cache++;
                    if (cache->keyOffset != 0) {
                        track = (uint *)(((uintptr_t)trackBase + cache->keyOffset) & ~(uintptr_t)1);
                        pending = (uint)cache->pendingHigh << 16;
                        havePending = cache->keyOffset & 1;
                        frames = cache->accBefore;
                    }
                }
                uint mask;
                for (;;) {
                    if (useCache) {
                        cache->keyOffset = (uint)((uchar *)track - (uchar *)trackBase) | (havePending ? 1 : 0);
                        cache->pendingHigh = (ushort)(pending >> 16);
                        cache->accBefore = (ushort)frames;
                    }
                    uint key;
                    if (havePending) {
                        key = pending >> 16;
                        havePending = false;
                    } else {
                        pending = *track++;
                        key = pending & 0xffff;
                        havePending = true;
                    }
                    frames += key >> 6;
                    mask = key & 0x3f;
                    if (keyFrame <= frames)
                        break;
                    for (uint m = mask; m != 0; m &= m - 1)
                        track++;
                }

                for (int i = 0; i < 6; i++, mask >>= 1)
                    AnimChannelCoef[i][count] = (mask & 1) ? *(float *)track++ : 0.0f;
                AnimChannelType[count] = channel;
                if (++count == 4) {
                    AnimFrameEmitChannels(count, &morphOut, &transOut, &quatOut);
                    count = 0;
                }
            }

            if (channel == ANIMCH_MORPH)
                channel = ANIMCH_QUAT_Z;
            track = next;
            if (channel-- == 0)
                break;
        }
    }
    if (count > 0)
        AnimFrameEmitChannels(count, &morphOut, &transOut, &quatOut);

    Vec_Mul((_VECTOR *)out, &Glb_InvSkinScale);
    if (!(seq->flags & ANIMSEQ_FIRST_TRANS)) {
        seq->flags |= ANIMSEQ_FIRST_TRANS;
        Vec_Copy(transOut, &seq->firstTrans);
    }
    Vec_Subtract(transOut, &seq->firstTrans, &seq->transRelFirst);
}

// Blends pose b into pose a by t into out (any may be the same): translations moved towards b, quaternions slerped,
// the accurate slerp near a view. The bones in mask are b's whole.
void AnimFrameBlend(float *out, float *a, float *b, obj_tag *obj, int numBones, float t, uchar *mask) {
    _VECTOR *outTrans = (_VECTOR *)out;
    _VECTOR *aTrans = (_VECTOR *)a;
    _VECTOR *bTrans = (_VECTOR *)b;
    quaternion_tag *outQuats = (quaternion_tag *)(outTrans + numBones);
    quaternion_tag *aQuats = (quaternion_tag *)(aTrans + numBones);
    quaternion_tag *bQuats = (quaternion_tag *)(bTrans + numBones);

    int whole = 0;
    if (t == 0.0f)
        whole = -1;
    else if (t == 1.0f)
        whole = 1;
    bool accurate = ScriptCam != 0 || AnimObjectNearestLod(obj) <= 0xf0;

    if (mask == NULL) {
        if (whole < 0) {
            if (out != a)
                memcpy(out, a, numBones * (sizeof(_VECTOR) + sizeof(quaternion_tag)));
            return;
        }
        if (whole > 0) {
            if (out != b)
                memcpy(out, b, numBones * (sizeof(_VECTOR) + sizeof(quaternion_tag)));
            return;
        }
        if (out == a && out == b)
            return;
        for (int i = 0; i < numBones; i++) {
            outTrans[i].x = moveTowardsValue(aTrans[i].x, bTrans[i].x, t);
            outTrans[i].y = moveTowardsValue(aTrans[i].y, bTrans[i].y, t);
            outTrans[i].z = moveTowardsValue(aTrans[i].z, bTrans[i].z, t);
            if (accurate)
                Quat_Slerp_Acc(t, aQuats[i].q, bQuats[i].q, outQuats[i].q);
            else
                Quat_Slerp(t, &aQuats[i], &bQuats[i], &outQuats[i]);
        }
        return;
    }

    for (int i = 0; i < numBones; i++) {
        bool masked = false;
        for (const uchar *bone = mask; *bone != 0xff; bone += 2) {
            if (*bone == i) {
                masked = true;
                break;
            }
        }
        if (masked || whole > 0) {
            outTrans[i] = bTrans[i];
            outQuats[i] = bQuats[i];
        } else if (whole < 0) {
            outTrans[i] = aTrans[i];
            outQuats[i] = aQuats[i];
        } else {
            outTrans[i].x = LerpTo(aTrans[i].x, bTrans[i].x, t);
            outTrans[i].y = LerpTo(aTrans[i].y, bTrans[i].y, t);
            outTrans[i].z = LerpTo(aTrans[i].z, bTrans[i].z, t);
            if (accurate)
                Quat_Slerp_Acc(t, aQuats[i].q, bQuats[i].q, outQuats[i].q);
            else
                Quat_Slerp(t, &aQuats[i], &bQuats[i], &outQuats[i]);
        }
    }
}

// The sequence's frame into its pose, the next frame blended in by fraction
void AnimFrameSet(sAnimSeq_tag *seq, sAnimSkin_tag *skin, obj_tag *obj, float fraction) {
    int numBones = SkeletonTable[skin->skeletonNum]->numBones;
    float frame = seq->frame;
    if (fraction <= 0.0002f && fraction >= -0.0002f) {
        AnimFrameCopy(seq->pose, seq, skin, frame);
        return;
    }

    double next = (double)frame + 1.0;
    float last = seq->lastFrame;
    float nextFrame = next < last ? (float)next : last;
    if (fraction == 1.0f) {
        AnimFrameCopy(seq->pose, seq, skin, nextFrame);
        return;
    }
    // The bones the skin does not use are left as they were in nextPose, uninitialised stack in the original, and
    // blended into the pose all the same; ours are 0 (docs/nightfire-port-lifts.md, Departures)
    float nextPose[ANIM_FRAME_BUFFER_SIZE / sizeof(float)];
    memset(nextPose, 0, sizeof(nextPose));
    AnimFrameCopy(seq->pose, seq, skin, frame);
    AnimFrameCopy(nextPose, seq, skin, nextFrame);
    AnimFrameBlend(seq->pose, seq->pose, nextPose, obj, numBones, fraction, NULL);
}

// A walk script's sequence's frame callback (AnimSetUpdate): as AnimSeqTick samples, whatever the script's flags.
// Their name; FUN_000171a0 on the Xbox.
// AUTOINJECT
void AnimSet_WalkUpdater(obj_tag *obj, sAnimSkin_tag *skin, sAnimSeq_tag *seq, sAnimScript_tag *script) {
    float fraction = (float)(seq->frame - (double)Ftol(seq->frame));
    AnimFrameSet(seq, skin, obj, fraction);
}

// A sequence's tick: its frame from its script's, sampled into its pose, and its root's motion since the last tick
// taken out of the pose (bone 0's translation is left at 0)
void AnimSeqTick(sAnimScript_tag *script, sAnimSeq_tag *seq, obj_tag *obj, AnimObj *animObj) {
    if (script != NULL) {
        seq->flags &= 0x1ffffff;
        if (script->flags & ANIMSCRIPT_STOPPED)
            seq->flags |= ANIMSEQ_STOPPED;
        if (script->flags & ANIMSCRIPT_LOOP)
            seq->flags |= ANIMSEQ_LOOP;
        if (script->flags & ANIMSCRIPT_STARTED) {
            script->flags &= ~ANIMSCRIPT_STARTED;
            seq->flags |= ANIMSEQ_STARTED;
        }
        if (script->flags & ANIMSCRIPT_WRAPPED)
            seq->flags |= ANIMSEQ_WRAPPED;
        if (script->flags & ANIMSCRIPT_ZERO_ROOT_X)
            seq->flags |= ANIMSEQ_ZERO_ROOT_X;
        if (script->flags & ANIMSCRIPT_ZERO_ROOT_Y)
            seq->flags |= ANIMSEQ_ZERO_ROOT_Y;
        if (script->flags & ANIMSCRIPT_ZERO_ROOT_Z)
            seq->flags |= ANIMSEQ_ZERO_ROOT_Z;
        seq->frame = (float)(((double)script->curFrame - seq->startTime) + 1.0);
    }

    if (seq->frameCallback != NULL) {
        seq->frameCallback(obj, animObj->skin, seq, script);
    } else if (seq->flags & ANIMSEQ_MORPH) {
        if (AnimObjectIsClose(obj, 12))
            AnimFrameCopy(seq->pose, seq, NULL, seq->frame);
        return;
    } else {
        float fraction = 0.0f;
        if (!(script->flags & ANIMSCRIPT_SCRIPT_DRIVEN))
            fraction = (float)(seq->frame - (double)Ftol(seq->frame));
        AnimFrameSet(seq, animObj->skin, obj, fraction);
    }

    _VECTOR *root = (_VECTOR *)seq->pose;
    _VECTOR delta;
    if (seq->flags & (ANIMSEQ_STARTED | ANIMSEQ_WRAPPED)) {
        seq->flags &= ~(ANIMSEQ_STARTED | ANIMSEQ_WRAPPED);
        delta = seq->rootDelta;
    } else {
        Vec_Subtract(root, &seq->rootPrev, &delta);
    }
    Vec_Copy(root, &seq->rootPrev);
    if (seq->flags & ANIMSEQ_ZERO_ROOT_X)
        delta.x = 0.0f;
    if (seq->flags & ANIMSEQ_ZERO_ROOT_Y)
        delta.y = 0.0f;
    if (seq->flags & ANIMSEQ_ZERO_ROOT_Z)
        delta.z = 0.0f;
    seq->rootDelta = delta;
    Vec_Zero(root);
}

// A full character's root height in multiplayer, but for the alternative skeletons
static constexpr float kMultiplayerHeight = 0.8627321f;
static_assert(std::bit_cast<uint32_t>(kMultiplayerHeight) == 0x3f5cdc03, "0.8627321");

// Mixes the scripts' sequences into the object's pose: those synced into their masters, then the rest by age, each
// over the last by its weight; a masked script's bones over that, then an overlay by its fade. The root's
// height and motion go to the object (turned by an anim-rotation script's matrix), faded-out scripts are deleted, and
// the face is blended near a view.
// AUTOINJECT
void AnimFrameResolve(obj_tag *obj, AnimObj *animObj) {
    bool posed = false;
    _VECTOR delta, rootTrans;
    Vec_Zero(&delta);
    Vec_Zero(&rootTrans);
    sAnimSkin_tag *skin = animObj->skin;

    if (AnimListBuild(obj, ANIMLIST_SYNCED, 0, NULL, ANIMLIST_RESTART) != NULL) {
        int numBones = SkeletonTable[GlbAnimList[0]->boneAnimSeq->skeletonNum]->numBones;
        for (int i = 0; i < GlbAnimListCount; i++) {
            sAnimScript_tag *script = GlbAnimList[i];
            float *master = script->syncMaster->boneAnimSeq->pose;
            AnimFrameBlend(master, master, script->boneAnimSeq->pose, obj, numBones, script->syncBlend,
                           script->boneMask);
        }
    }

    // The fades
    AnimListBuild(obj, ANIMLIST_ALL, 0, NULL, ANIMLIST_RESTART);
    AnimListBuild(obj, ANIMLIST_OVERLAY | ANIMLIST_MASKED | ANIMLIST_MUTE_MORPH, 0, NULL, ANIMLIST_REMOVE);
    for (int i = 0; i < GlbAnimListCount; i++) {
        sAnimScript_tag *script = GlbAnimList[i];
        script->flags &= ~ANIMSCRIPT_FADED;
        if (script->fadeDir > 0) {
            script->fadeTime = (float)((double)FRAME_RATE_MUL + script->fadeTime);
        } else if (script->fadeDir < 0) {
            double fade = (double)script->fadeTime - FRAME_RATE_MUL;
            script->fadeTime = (float)fade;
            if (fade <= 0.0)
                script->flags |= ANIMSCRIPT_FADED;
        }
        if (script->fadeTime < 0.0f)
            script->fadeTime = 0.0f;
        else if (script->fadeTime > script->fadeLen)
            script->fadeTime = script->fadeLen;
        script->weight = script->fadeTime / script->fadeLen;
    }

    AnimListBuild(obj, ANIMLIST_SYNCED, 0, NULL, ANIMLIST_REMOVE);
    if (GlbAnimListCount != 0) {
        animObj->flags |= ANIMOBJ_POSE_VALID;
        posed = true;
        if (GlbAnimListCount == 1) {
            sAnimSeq_tag *seq = GlbAnimList[0]->boneAnimSeq;
            memcpy(animObj->pose, seq->pose, animObj->poseBufSize);
            delta = seq->rootDelta;
            rootTrans = seq->rootPrev;
        } else {
            // Oldest first
            for (int i = 0; i < GlbAnimListCount - 1; i++) {
                for (int j = i + 1; j < GlbAnimListCount; j++) {
                    if (GlbAnimList[i]->timestamp > GlbAnimList[j]->timestamp) {
                        sAnimScript_tag *swap = GlbAnimList[i];
                        GlbAnimList[i] = GlbAnimList[j];
                        GlbAnimList[j] = swap;
                    }
                }
            }
            int numBones = SkeletonTable[GlbAnimList[0]->boneAnimSeq->skeletonNum]->numBones;
            for (int i = 0; i < GlbAnimListCount - 1; i++) {
                float weight = (float)(1.0 - GlbAnimList[i]->weight);
                bool shareRoot = false;
                float *under;
                if (i == 0) {
                    sAnimSeq_tag *seq = GlbAnimList[0]->boneAnimSeq;
                    under = seq->pose;
                    delta = seq->rootDelta;
                    rootTrans = seq->rootPrev;
                    shareRoot = (GlbAnimList[0]->flags & ANIMSCRIPT_SHARE_ROOT) != 0;
                } else {
                    under = animObj->pose;
                }
                sAnimScript_tag *over = GlbAnimList[i + 1];
                sAnimSeq_tag *overSeq = over->boneAnimSeq;
                _VECTOR overDelta = overSeq->rootDelta;
                _VECTOR overTrans = overSeq->rootPrev;
                quaternion_tag *underRoot = (quaternion_tag *)((_VECTOR *)under + numBones);
                quaternion_tag *overRoot = (quaternion_tag *)((_VECTOR *)overSeq->pose + numBones);
                if (shareRoot)
                    *underRoot = *overRoot;
                if (over->flags & ANIMSCRIPT_SHARE_ROOT)
                    *overRoot = *underRoot;
                AnimFrameBlend(animObj->pose, under, overSeq->pose, obj, numBones, weight, NULL);
                delta.x = LerpTo(delta.x, overDelta.x, weight);
                delta.y = LerpTo(delta.y, overDelta.y, weight);
                delta.z = LerpTo(delta.z, overDelta.z, weight);
                rootTrans.x = LerpTo(rootTrans.x, overTrans.x, weight);
                rootTrans.y = LerpTo(rootTrans.y, overTrans.y, weight);
                rootTrans.z = LerpTo(rootTrans.z, overTrans.z, weight);
            }
        }
    }

    // The first masked script, over the pose
    if (AnimListBuild(obj, ANIMLIST_MASKED, 0, NULL, ANIMLIST_RESTART) != NULL) {
        sAnimScript_tag *masked = GlbAnimList[0];
        int numBones = SkeletonTable[masked->boneAnimSeq->skeletonNum]->numBones;
        float *pose = masked->boneAnimSeq->pose;
        if (posed) {
            AnimFrameBlend(animObj->pose, animObj->pose, pose, obj, numBones, 1.0f, masked->boneMask);
        } else {
            animObj->flags |= ANIMOBJ_POSE_VALID;
            memcpy(animObj->pose, pose, animObj->poseBufSize);
            rootTrans = masked->boneAnimSeq->rootPrev;
            posed = true;
        }
    }

    // The first overlay, by its fade
    bool moved = posed;
    if (AnimListBuild(obj, ANIMLIST_OVERLAY, 0, NULL, ANIMLIST_RESTART) != NULL) {
        sAnimScript_tag *overlay = GlbAnimList[0];
        int numBones = SkeletonTable[overlay->boneAnimSeq->skeletonNum]->numBones;
        float *pose = overlay->boneAnimSeq->pose;
        if (posed) {
            AnimFrameBlend(animObj->pose, animObj->pose, pose, obj, numBones, overlay->fadeTime, NULL);
        } else {
            animObj->flags |= ANIMOBJ_POSE_VALID;
            memcpy(animObj->pose, pose, animObj->poseBufSize);
            rootTrans = overlay->boneAnimSeq->rootPrev;
            moved = true;
        }
    }

    if (moved) {
        float height = (float)((double)rootTrans.y + animObj->heightOffset);
        animObj->rootHeight = height;
        if (!(animObj->flags & ANIMOBJ_ALT_SKELETON) && MPSettings.isMultiplayer)
            animObj->rootHeight = height * kMultiplayerHeight;

        AnimListBuild(obj, ANIMLIST_ALL, 0, NULL, ANIMLIST_RESTART);
        _MATRIX *turn = &obj->transformMatrix;
        for (int i = 0; i < GlbAnimListCount; i++) {
            if (GlbAnimList[i]->flags & ANIMSCRIPT_ANIM_ROTATION) {
                turn = &GlbAnimList[i]->rotMtx;
                break;
            }
        }
        _VECTOR move;
        ApplyMatrixLV(turn, &delta, &move);
        if (animObj->rootMotionCallback != NULL)
            animObj->rootMotionCallback(obj, animObj, &move);
        Vec_Add2(&move, &obj->position, &obj->position);

        // Once a script has faded out, those faded and those held go
        AnimListBuild(obj, ANIMLIST_ALL, 0, NULL, ANIMLIST_RESTART);
        AnimListBuild(obj, ANIMLIST_MUTE_MORPH, 0, NULL, ANIMLIST_REMOVE);
        bool anyFaded = false;
        for (int i = 0; i < GlbAnimListCount; i++) {
            uint flags = GlbAnimList[i]->flags;
            if ((flags & ANIMSCRIPT_FADED) && (flags & ANIMSCRIPT_FADE_OUT)) {
                anyFaded = true;
                break;
            }
        }
        if (anyFaded) {
            int count = GlbAnimListCount;
            for (int i = 0; i < count; i++) {
                sAnimScript_tag *script = GlbAnimList[i];
                bool faded = (script->flags & ANIMSCRIPT_FADED) && (script->flags & ANIMSCRIPT_FADE_OUT);
                if (script->fadeDir == 0 || faded) {
                    AnimScriptDelete(obj, &script);
                    count = GlbAnimListCount;
                }
            }
        }
    }

    if (skin->numMorphs != 0 && (ScriptCam != 0 || AnimObjectNearestLod(obj) <= 12))
        AnimFacialBlend(obj, animObj, skin);

    if (animObj->postPoseCallback != NULL) {
        int numBones = SkeletonTable[skin->skeletonNum]->numBones;
        animObj->postPoseCallback(obj, animObj, numBones, (_VECTOR *)animObj->pose,
                                  (quaternion_tag *)((_VECTOR *)animObj->pose + numBones));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The anim object

// The skin the level uses in place of skin: some levels have their own versions of some characters
HASHCODE FixupSkinHashcode(HASHCODE skin) {
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_HendersonA:
    case HT_Level_HendersonB:
    case HT_Level_HendersonC:
    case HT_Level_Tower2A:
        if (skin == HT_Skin_Yakuza_Trenchcoat)
            return HT_Skin_Yakuza_TrenchcoatSpec;
        if (skin == HT_Skin_Yakuza_RedTrenchcoat)
            return HT_Skin_Yakuza_RedTrenchcoatSpec;
        break;
    case HT_Level_CastleIndoors1:
    case HT_Level_CastleIndoors2:
    case HT_Level_EvilBase:
    case HT_Level_EvilBaseC:
        switch ((uint)skin) {
        case HT_Skin_Dominique: return HT_Skin_Dominique_Spec;
        case HT_Skin_Kiko_Dress: return HT_Skin_Kiko_DressSpec;
        case HT_Skin_Zoe_PartyDress: return HT_Skin_Zoe_PartyDressSpec;
        case HT_Skin_Kiko_Combat: return HT_Skin_Kiko_CombatSpec;
        }
        break;
    default:
        break;
    }
    return skin;
}

// The height under the root of a full character, by its skeleton, before the skin's own (AnimObjectNew)
static constexpr float kHeightAltSkeleton = -1.160398f;
static constexpr float kHeightSkeleton1 = -0.995208f;
static_assert(std::bit_cast<uint32_t>(kHeightAltSkeleton) == 0xbf9487ec, "-1.160398");
static_assert(std::bit_cast<uint32_t>(kHeightSkeleton1) == 0xbf7ec5f4, "-0.995208");

// Animates the object with the skin: its pose, matrices, datums and rigid parts. 0, and no anim object, if the skin is
// not loaded or anything fails.
// AUTOINJECT
int AnimObjectNew(obj_tag *obj, HASHCODE skinHash) {
    if ((skinHash & HASHCODE_SECTION_MASK) != HT_Skin_HASHCODE_BASE || obj == NULL)
        return 0;
#if !NIGHTFIRE_DATA_PS2   // the levels' own skins are the Xbox's; the PS2 has none, nor this (docs/ps2-data.md)
    skinHash = FixupSkinHashcode(skinHash);
#endif
    sAnimSkin_tag *skin = (sAnimSkin_tag *)hashtable_getitem(skinHash);
    if (skin == NULL) {
        NF_WARN("Skin 0x%x not loaded", skinHash); // GC check (0x800452fc)
        return 0;
    }

    AnimState *animState = (AnimState *)Mem_Malloc(sizeof(AnimState), MALLOC_ANIM_OBJECT_STRUCTS, 0);
    if (animState == NULL)
        goto failed;
    obj->animState = animState;
    memset(animState, 0, sizeof(AnimState));
    {
        AnimObj *animObj = &obj->animState->animObj;
        bool rigged;
        animObj->skin = skin;
        int numBones = SkeletonTable[skin->skeletonNum]->numBones;
        obj->transformFlags |= TRANSFORM_ANIMATED;
        animObj->poseBufSize = SkeletonTable[animObj->skin->skeletonNum]->numBones
                               * (sizeof(_VECTOR) + sizeof(quaternion_tag));
        animObj->pose = (float *)Mem_Malloc(animObj->poseBufSize, MALLOC_ANIM_SEQ_DATA_BUFFERS, 0);
        if (animObj->pose == NULL)
            goto freeParts;
        memset(animObj->pose, 0, animObj->poseBufSize);
        if (skin->numMorphs != 0) {
            // Not checked for NULL by the original
            animObj->morphWeights = (float *)Mem_Malloc(skin->numMorphs * sizeof(float),
                                                        MALLOC_ANIM_MORPH_DATA_BUFFERS, 0);
            memset(animObj->morphWeights, 0, skin->numMorphs * sizeof(float));
        }
        if (skin->numDatums != 0) {
            animObj->datumGlist = (celglist_tag **)Mem_Malloc(skin->numDatums * sizeof(celglist_tag *),
                                                              MALLOC_ANIM_OBJECT_STRUCTS, 0);
            if (animObj->datumGlist == NULL)
                goto freeParts;
            animObj->datumTimer = (uchar *)Mem_Malloc(skin->numDatums, MALLOC_ANIM_OBJECT_STRUCTS, 0);
            if (animObj->datumTimer == NULL)
                goto freeParts;
            for (int i = 0; i < skin->numDatums; i++) {
                animObj->datumGlist[i] = NULL;
                animObj->datumTimer[i] = 0;
            }
        }
        animObj->boneMtxs = (_MATRIX *)Mem_Malloc(numBones * sizeof(_MATRIX), MALLOC_ANIM_OBJECT_STRUCTS, 0);
        if (animObj->boneMtxs == NULL)
            goto freeParts;
        animObj->palette = (_MATRIX *)Mem_Malloc(numBones * sizeof(_MATRIX), MALLOC_ANIM_OBJECT_STRUCTS, 0);
        if (animObj->palette == NULL)
            goto freeParts;

        if (numBones > 70) {
            animObj->flags |= ANIMOBJ_FULL_CHARACTER;
            if (animObj->skin->skeletonNum != 1)
                animObj->flags |= ANIMOBJ_ALT_SKELETON;
        }
        if (skin->numRigged != 0)
            hashtable_set_object_to_entity_gfx(obj, hashtable_celglist_to_hashcode(skin->riggedBody[0]));
        rigged = skin->numRigged != 0;
        if (animObj->flags & ANIMOBJ_FULL_CHARACTER)
            obj->flags |= OBJFLAG_FULL_CHARACTER;
        if (rigged)
            obj->flags |= OBJFLAG_RIGGED;

        if (skin->numDiscrete != 0) {
            animObj->discreteObj = (obj_tag **)Mem_Malloc(skin->numDiscrete * sizeof(obj_tag *),
                                                          MALLOC_ANIM_OBJECT_STRUCTS, 0);
            if (animObj->discreteObj == NULL)
                goto freeParts;
            memset(animObj->discreteObj, 0, skin->numDiscrete * sizeof(obj_tag *));
            for (int i = 0; i < skin->numDiscrete; i++) {
                obj_tag *part = Control_CreateObjEx(0, &obj->position, NULL, NULL, skin->discreteObj[i],
                                                    rigged ? obj : NULL, 0, 0xc4, 1.0f, 0, 0xff, 0xff, 0xff);
                if (part == NULL)
                    goto freeParts;
                part->extraObjectData = obj;
                part->objectType = OBJECTTYPE_ANIMOBJECT;
                part->effectFlags = (part->effectFlags & ~EFFECT_NO_COLLISION) | EFFECT_STRADDLES_CELS;
                if (rigged)
                    part->flags |= OBJFLAG_RIGGED;
                animObj->discreteObj[i] = part;
            }
        }

        skin->hashcode = skinHash;
        if (animObj->flags & ANIMOBJ_FULL_CHARACTER) {
            obj->effectFlags &= ~EFFECT_NO_COLLISION;
            float base = (animObj->flags & ANIMOBJ_ALT_SKELETON) ? kHeightAltSkeleton : kHeightSkeleton1;
            animObj->heightOffset = (float)(((double)base - (double)obj->objGraphics->extentMin.y / skin->skinScale.y)
                                            + 0.02f);
        }
        return 1;

    freeParts:
        if (animObj->discreteObj != NULL) {
            for (int i = 0; i < animObj->skin->numDiscrete; i++) {
                if (animObj->discreteObj[i] != NULL)
                    control_delete_object(animObj->discreteObj[i]);
            }
            Mem_Free((void **)&animObj->discreteObj);
        }
        if (animObj->palette != NULL)
            Mem_Free((void **)&animObj->palette);
        if (animObj->boneMtxs != NULL)
            Mem_Free((void **)&animObj->boneMtxs);
        if (animObj->datumTimer != NULL)
            Mem_Free((void **)&animObj->datumTimer);
        if (animObj->datumGlist != NULL)
            Mem_Free((void **)&animObj->datumGlist);
        if (animObj->morphWeights != NULL)
            Mem_Free((void **)&animObj->morphWeights);
        if (animObj->pose != NULL)
            Mem_Free((void **)&animObj->pose);
    }
failed:
    // An anim object the object had already is freed too
    if (obj->animState != NULL)
        Mem_Free((void **)&obj->animState);
    return 0;
}

// AUTOINJECT
void AnimObjectDelete(obj_tag *obj) {
    if (obj == NULL || obj->animState == NULL)
        return;
    AnimObj *animObj = &obj->animState->animObj;
    AnimSetDeleteAll(obj);
    AnimListBuild(obj, ANIMLIST_ALL, 0, NULL, ANIMLIST_RESTART);
    AnimListDelete(obj);
    if (animObj->discreteObj != NULL) {
        for (uint i = 0; i < animObj->skin->numDiscrete; i++) {
            if (animObj->discreteObj[i] != NULL)
                control_delete_object(animObj->discreteObj[i]);
        }
        Mem_Free((void **)&animObj->discreteObj);
    }
    if (animObj->palette != NULL)
        Mem_Free((void **)&animObj->palette);
    if (animObj->boneMtxs != NULL)
        Mem_Free((void **)&animObj->boneMtxs);
    if (animObj->datumTimer != NULL)
        Mem_Free((void **)&animObj->datumTimer);
    if (animObj->datumGlist != NULL)
        Mem_Free((void **)&animObj->datumGlist);
    if (animObj->morphWeights != NULL)
        Mem_Free((void **)&animObj->morphWeights);
    if (animObj->pose != NULL)
        Mem_Free((void **)&animObj->pose);
    Mem_Free((void **)&obj->animState);
}

// A rigid part of an animated object (AnimObjectNew) forgets its owner
// AUTOINJECT
void Discrete_Delete(obj_tag *obj) {
    obj->extraObjectData = NULL;
}

// The player's stride between footsteps, and on a ladder
static constexpr float kStride = 1.65f;
static constexpr float kStrideLadder = 0.4125f;
static_assert(std::bit_cast<uint32_t>(kStride) == 0x3fd33333, "1.65");
static_assert(std::bit_cast<uint32_t>(kStrideLadder) == 0x3ed33333, "0.4125");

// An animated object's tick: the face, the scripts, the footsteps, the sequences and the turns of anim-rotation
// scripts, then the pose; the bones for the player and the rigid parts, which follow them.
// There is also a thunk to this at 0x0002cf00 with the same name, hence FUNC_AT.
// FUNC_AT(0x00018690)
int AnimObjectUpdate(obj_tag *obj) {
    bool turned = false;
    if (obj == NULL || obj->animState == NULL)
        return 0;
    AnimObj *animObj = &obj->animState->animObj;
    sAnimSkin_tag *skin = animObj->skin;
    animObj->flags &= ~(ANIMOBJ_BONES_VALID | ANIMOBJ_PALETTE_VALID);
    Vec_Copy(&skin->skinScale, &Glb_SkinScale);
    animObj->lastUpdateFrame = GameState.NumFrames;
    if (animObj->flags & ANIMOBJ_INV_SKIN_SCALE) {
        Glb_InvSkinScale.x = 1.0f / Glb_SkinScale.x;
        Glb_InvSkinScale.y = 1.0f / Glb_SkinScale.y;
        Glb_InvSkinScale.z = 1.0f / Glb_SkinScale.z;
    } else {
        Glb_InvSkinScale.z = 1.0f;
        Glb_InvSkinScale.y = 1.0f;
        Glb_InvSkinScale.x = 1.0f;
    }
    FootstepType = 0;

    if (skin->numMorphs != 0 && (animObj->flags & ANIMOBJ_FULL_CHARACTER) && AnimObjectIsClose(obj, 12))
        AnimFacialUpdate(animObj, obj);

    // A script that deletes itself is still read after its tick
    for (sAnimScript_tag *script = animObj->firstScript; script != NULL; script = script->nextScript) {
        if (script->scriptData != NULL && !(script->flags & ANIMSCRIPT_STOPPED)) {
            Glb_bScriptDeleted = 0;
            Glb_pCurrentScript = script;
            AnimScriptTick(animObj, obj, script);
            script->prevFrame = script->curFrame;
        }
    }

    // Footsteps
    obj->animState->animFlags &= ~ANIM_FOOTSTEP;
    bool onLadder = false;
    if (obj->objectType == OBJECTTYPE_PLAYER) {
        BLData *blData = (BLData *)obj->extraObjectData;
        blData->stepDistance = (float)((double)Magnitude(animObj->rootTravel) + blData->stepDistance);
        float stride = kStride;
        if (obj->subState == MovementType_Climb) {
            stride = kStrideLadder;
            onLadder = true;
        }
        if (blData->landingStepFrames != 0 && --blData->landingStepFrames == 0)
            blData->stepDistance = (float)((double)stride + 0.0001f);
        if (blData->stepDistance > stride) {
            blData->stepDistance = (float)fmod(blData->stepDistance, stride);
            obj->animState->animFlags |= ANIM_FOOTSTEP;
            FootstepFoot = !FootstepFoot;
        }
    } else if (FootstepType) {
        obj->animState->animFlags |= ANIM_FOOTSTEP;
    }
    if ((obj->animState->animFlags & ANIM_FOOTSTEP) && ScriptCam == 0) {
        uint foot = FootstepFoot ? 8 : 4;
        if (!onLadder)
            Effect_Create(&obj->animState->floor, obj, foot | 0x42, 1.0f);
        else
            Sound_SetAlertness(Sound_Play3D(SFX_FOOTSTEP_LADDER, &obj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0), 0.0f);
    }

    for (sAnimScript_tag *script = animObj->firstScript; script != NULL; script = script->nextScript) {
        for (int slot = 0; slot < 2; slot++) {
            sAnimSeq_tag *seq = (&script->boneAnimSeq)[slot];
            if (seq != NULL && script->scriptData != NULL && seq->hashcode != 0)
                AnimSeqTick(script, seq, obj, animObj);
        }

        if (script->flags & ANIMSCRIPT_ANIM_ROTATION) {
            sAnimSeq_tag *seq = script->boneAnimSeq;
            int numBones = SkeletonTable[seq->skeletonNum]->numBones;
            quaternion_tag *rootQuat = (quaternion_tag *)((_VECTOR *)seq->pose + numBones);
            _VECTOR ahead, facing;
            ahead.x = 0.0f;
            ahead.y = 0.0f;
            ahead.z = 1.0f;
            Quat_Rot_Vec(rootQuat, &ahead, &facing);
            float yaw = (float)FPatan(facing.x, facing.z);
            if (script->rotBaseYaw >= 8.0f) {
                script->rotBaseYaw = yaw;
                Control_GetObjMatrix(obj, &obj->transformMatrix);
                Mat_CopyRot(&obj->transformMatrix, &script->rotMtx);
            } else {
                // The object takes the root's turn; the root keeps the turn from where it started
                obj->rotation.y = (float)((double)Vec_AngleDifference(script->rotLastYaw, yaw) + obj->rotation.y);
                _VECTOR euler;
                euler.y = Vec_AngleDifference(yaw, script->rotBaseYaw);
                euler.x = 0.0f;
                euler.z = 0.0f;
                quaternion_tag back, rotated;
                Quat_EulerToQuat(&euler, &back);
                MultiplyQuaternionAltOrder(&back, rootQuat, &rotated);
                Quat_Copy(rootQuat, &rotated);
                turned = true;
            }
            script->rotLastYaw = yaw;
        }
    }

    AnimFrameResolve(obj, animObj);
    Glb_SkinScale.z = 1.0f;
    Glb_SkinScale.y = 1.0f;
    Glb_SkinScale.x = 1.0f;
    Glb_InvSkinScale.z = 1.0f;
    Glb_InvSkinScale.y = 1.0f;
    Glb_InvSkinScale.x = 1.0f;

    if (turned) {
        obj->transformFlags |= TRANSFORM_MOVED;
        Control_GetObjMatrix(obj, &obj->transformMatrix);
        animObj->flags &= ~ANIMOBJ_BONES_VALID;
        psiBuildMatrixPalette(obj, animObj, 0);
    }
    if (obj->objectType == OBJECTTYPE_PLAYER)
        psiBuildMatrixPalette(obj, animObj, 0);
    if (skin->numDiscrete != 0) {
        psiBuildMatrixPalette(obj, animObj, 0);
        for (uint i = 0; i < skin->numDiscrete; i++) {
            obj_tag *part = animObj->discreteObj[i];
            part->transformFlags |= TRANSFORM_MOVED;
            part->effectFlags |= obj->effectFlags & (EFFECT_HIDDEN | EFFECT_NO_COLLISION | EFFECT_DRAW_LAST | EFFECT_STRADDLES_CELS | EFFECT_SUNLIT);
            part->displayMask = obj->displayMask;
            Mat_Copy(&animObj->boneMtxs[skin->discreteBone[i]], &part->transformMatrix);
        }
    }
    return 1;
}

// How high a full character's foot is when it stands; 0 for others
float AnimObjectInitialPose(obj_tag *obj) {
    _VECTOR foot;
    Vec_Zero(&foot);
    uint flags = obj->animState->animObj.flags;
    if (flags & ANIMOBJ_FULL_CHARACTER) {
        // The scripts a full character is posed by, standing, for the skeletons
        sAnimScript_tag *script = AnimScriptAdd(obj, (flags & ANIMOBJ_ALT_SKELETON) ? HT_Script_StandMale
                                                : HT_Script_StandFemale);
        script->speed = 0.0f;
        script->flags &= ~ANIMSCRIPT_LOOP;
        AnimObjectUpdate(obj);
        AnimGetBoneWorldTrans(obj, ANIM_BONE(BONE_LEFT_FOOT), 0, &foot, NULL);
        AnimScriptDelete(obj, &script);
    }
    return foot.y;
}

// The control functions' thunk to AnimObjectUpdate (a lone JMP)
// FUNC_AT(0x0002cf00)
int AnimObjectUpdateThunk(obj_tag *obj) {
    return AnimObjectUpdate(obj);
}

// The debug levels' displays: AllCharacters puts every character's skin in a ring around the player, facing out;
// RefRoom one character in front of the player, talking
#define ANIMDEBUG_RING_SPACING 0.23873241f  // a skin's share of the ring's radius: 1.5 / 2 pi
#define ANIMDEBUG_FLOOR 0.09f
static_assert(std::bit_cast<uint32_t>(ANIMDEBUG_RING_SPACING) == 0x3e747645, "0.23873241");
static_assert(std::bit_cast<uint32_t>(ANIMDEBUG_FLOOR) == 0x3db851ec, "0.09");

// AUTOINJECT
void AnimDebugCreate(void) {
    if (GameState.CurrentLevelHashcode == HT_Level_AllCharacters ||
        GameState.CurrentLevelHashcode == HT_Level_AllCharacters2) {
        // The skins of skeletons 0 and 1
        HASHCODE skins[256];
        int count = hashtable_get_hashtype_count(HT_Skin_HASHCODE_BASE);
        hashtable_get_hashtype_data(HT_Skin_HASHCODE_BASE, skins);
        for (int i = count - 1; i >= 0; i--) {
            sAnimSkin_tag *skin = (sAnimSkin_tag *)hashtable_getitem(skins[i]);
            if (skin->skeletonNum != 0 && skin->skeletonNum != 1) {
                memmove(&skins[i], &skins[i + 1], (count - i - 1) * sizeof(HASHCODE));
                count--;
            }
        }
        _VECTOR centre;
        Vec_Copy(&glb_players[0]->position, &centre);
        centre.y = 0.0f;
        float countf = (float)count;
        float spacing = (float)(count * (double)ANIMDEBUG_RING_SPACING);
        for (int i = 0; i < count; i++) {
            double angle = i / (double)countf * (double)TWO_PI;
            _VECTOR at;
            at.y = 0.0f;
            at.x = (float)FCos(angle);
            Extended sine = FSin(angle);
            at.z = (float)sine;
            float facingZ = (float)-sine;
            float facingX = X87Negate(at.x);
            Vec_Scalef(&at, spacing, &at);
            Vec_Add2(&centre, &at, &at);
            obj_tag *obj = control_create_object(0, &at, NULL, NULL);
            double yaw = (double)FPatan(facingX, facingZ);
            obj->effectFlags |= EFFECT_NO_COLLISION;
            obj->rotation.y = (float)yaw;
            build_LinkToRoom(obj, 0, glb_world);
            obj->objectType = OBJECTTYPE_ANIMDEBUGHANDLER;
            AnimObjectNew(obj, skins[i]);
            obj->position.y = (float)(ANIMDEBUG_FLOOR - (double)AnimObjectInitialPose(obj));
        }
    }
    if (GameState.CurrentLevelHashcode == HT_Level_RefRoom) {
        float yaw = glb_players[0]->rotation.y - 0.3f;   // 0x3e99999a
        obj_tag *obj = control_create_object(0, &glb_players[0]->position, NULL, NULL);
        Extended sine = FSin(yaw);
        obj->effectFlags |= EFFECT_NO_COLLISION;
        obj->position.x = (float)(sine * Extended(3.3f) + Extended(obj->position.x));
        obj->position.z = (float)(FCos(yaw) * Extended(3.3f) + Extended(obj->position.z));
        obj->rotation.y = yaw + PI;
        build_LinkToRoom(obj, 0, glb_world);
        obj->objectType = OBJECTTYPE_ANIMDEBUGHANDLER;
        AnimObjectNew(obj, HT_Skin_SnowGuard_Head4);
        AnimScriptAdd(obj, HT_Script_Speech_M1_DRIVER_TALK_GUARD02)->flags |= ANIMSCRIPT_NO_MORPH | ANIMSCRIPT_LOOP;
        AnimObj *animObj = &obj->animState->animObj;
        sAnimScript_tag *talk = AnimScriptAppend(obj, HT_Script_Speech_M1_GUARD_CHATTER_SOLDA17);
        talk->playMode = ANIMPLAY_DELETE;
        talk->groupTag = ANIMTAG_FACE_SPEECH;
        animObj->facialState = FACIAL_TALKING;
        talk->flags |= ANIMSCRIPT_LOOP;
        BodyGlow_Create(obj);
    }
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

// ---------------------------------------------------------------------------------------------------------------
// The bones

// psiBuildMatrixPalette: from the local pose to world bone matrices, and from those to the skinning palette.
// Bones are stored parents first, so one pass in index order resolves them; bones the skin does not use keep
// whatever matrices they had. The root's translation is added to the object's position without being turned by the
// object's matrix (RotTransMat adds translations). Head tracking and aiming act on bones 5 and 2 inside the walk, so
// their children follow. ANIMOBJ_BONES_VALID and ANIMOBJ_PALETTE_VALID make a second call in a frame free.
// AUTOINJECT
void psiBuildMatrixPalette(obj_tag *obj, AnimObj *animObj, char buildPalette) {
    sAnimSkin_tag *skin = animObj->skin;
    _MATRIX *bone = animObj->boneMtxs;
    int numBones = SkeletonTable[skin->skeletonNum]->numBones;
    uint flags = animObj->flags;

    if (!(flags & ANIMOBJ_BONES_VALID)) {
        flags |= ANIMOBJ_BONES_VALID;
        bool headTrack = (flags & ANIMOBJ_HEAD_TRACKING) != 0;
        bool aim = (flags & (ANIMOBJ_AIMING | ANIMOBJ_AIMING_2)) != 0;
        animObj->flags = flags;

        _MATRIX objMtx = obj->transformMatrix;
        _VECTOR *trans = (_VECTOR *)animObj->pose;
        quaternion_tag *quats = (quaternion_tag *)(trans + numBones);
        quaternion_tag worldQuats[96];

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
                if (headTrack && b == BONE_HEAD)
                    AnimObjectHeadTrack(obj, animObj, worldQuats, bone, BONE_HEAD);
                if (aim && b == BONE_AIM)
                    AnimObjectAimAt(obj, animObj, worldQuats, bone, BONE_AIM);
            }
        }
    }

    if (buildPalette && skin->numRigged != 0 && !(animObj->flags & ANIMOBJ_PALETTE_VALID)) {
        animObj->flags |= ANIMOBJ_PALETTE_VALID;
        _MATRIX *palette = animObj->palette;
        for (int b = 0; b < numBones; b++) {
            if (!(skin->bones[b] & ANIM_BONE_USED))
                continue;
            // The inverse bind poses have no rotation
            Mat_CopyRot(&bone[b], &palette[b]);
            MatrixMultiplyVector(&bone[b], Mat_Position(skin->invBind[b]), Mat_Position(palette[b]));
        }
    }
}

// Where a bone (which with bit 31) or a datum (which + datumOffset, an id) is in the world this frame, and its
// matrix if mtxOut is given. Without a pose, or for a datum the skin lacks, the origin and the identity.
// AUTOINJECT
void AnimGetBoneWorldTrans(obj_tag *obj, uint which, int datumOffset, _VECTOR *posOut, _MATRIX *mtxOut) {
    if (obj != NULL && obj->animState != NULL && (obj->animState->animObj.flags & ANIMOBJ_POSE_VALID)) {
        AnimObj *animObj = &obj->animState->animObj;
        psiBuildMatrixPalette(obj, animObj, 0);
        sAnimSkin_tag *skin = animObj->skin;

        if (which & ANIM_WHICH_BONE) {
            _MATRIX *m = &animObj->boneMtxs[which & ~ANIM_WHICH_BONE];
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
            Concat(&animObj->boneMtxs[datum->bone], &m);
            *posOut = *Mat_Position(m);
            if (mtxOut != NULL)
                *mtxOut = m;
            return;
        }
        NF_WARN("Bad Datum in Skin 0x%x %d %d", skin->hashcode, which, datumOffset); // GC check (0x8004a6e8)
    }

    Vec_Zero(posOut);
    if (mtxOut != NULL)
        Mat_Identity(mtxOut);
}

// As AnimGetBoneWorldTrans, from the pose rather than the bone matrices: the bone's chain is built from the root
// down. AnimObjectAimAt asks it for the gun's datum while the bones are being built.
// AUTOINJECT
void AnimGetBoneWorldTrans2(obj_tag *obj, uint which, int datumOffset, _VECTOR *posOut, _MATRIX *mtxOut) {
    uchar chain[96];
    _MATRIX objMtx;
    _MATRIX boneMtx[128];

    if (obj != NULL && obj->animState != NULL && (obj->animState->animObj.flags & ANIMOBJ_POSE_VALID)) {
        AnimObj *animObj = &obj->animState->animObj;
        sAnimSkin_tag *skin = animObj->skin;
        _VECTOR *trans = (_VECTOR *)animObj->pose;
        quaternion_tag *quats = (quaternion_tag *)(trans + SkeletonTable[skin->skeletonNum]->numBones);
        bool isBone = (which & ANIM_WHICH_BONE) != 0;
        which &= ~ANIM_WHICH_BONE;

        int datum = 0;
        int bone;
        if (isBone) {
            bone = which;
        } else {
            which += datumOffset;
            while (datum < skin->numDatums && skin->datums[datum].id != (int)which)
                datum++;
            if (datum >= skin->numDatums) {
                NF_WARN("Bad Datum in Skin 0x%x %d %d", skin->hashcode, which - datumOffset, datumOffset); // GC check (0x8004a9e8)
                goto none;
            }
            bone = skin->datums[datum].bone;
        }

        // The chain to the root, then each link from the root down
        int depth = 0;
        do {
            chain[depth++] = (uchar)bone;
            bone = skin->bones[bone] & ANIM_BONE_PARENT_MASK;
        } while (bone != ANIM_BONE_ROOT);
        objMtx = obj->transformMatrix;
        bool root = true;
        for (int i = depth - 1; i >= 0; i--) {
            int b = chain[i];
            Quat_QuatTransToMat(&quats[b], (float *)&trans[b], &boneMtx[b]);
            if (root) {
                RotTransMat(&objMtx, &boneMtx[b]);
                root = false;
            } else {
                Concat(&boneMtx[chain[i + 1]], &boneMtx[b]);
            }
        }

        if (!isBone) {
            AnimDatum *d = &skin->datums[datum];
            _MATRIX m;
            Quat_QuatTransToMat(&d->q, (float *)&d->t, &m);
            Concat(&boneMtx[d->bone], &m);
            *posOut = *Mat_Position(m);
            if (mtxOut != NULL)
                *mtxOut = m;
        } else {
            *posOut = *Mat_Position(boneMtx[which]);
            if (mtxOut != NULL)
                *mtxOut = boneMtx[which];
        }
        return;
    }

none:
    Vec_Zero(posOut);
    if (mtxOut != NULL)
        Mat_Identity(mtxOut);
}

// The dot product of two unit vectors as the original sums it: z first
static double DotZYX(const _VECTOR *a, const _VECTOR *b) {
    return ((double)a->z * b->z + (double)a->y * b->y) + (double)a->x * b->x;
}

// a's acos, clamped to -1..1 first
static float ClampedAcos(float cosine) {
    if (cosine < -1.0f)
        cosine = -1.0f;
    else if (cosine > 1.0f)
        cosine = 1.0f;
    return (float)CIAcos(cosine);
}

// Turns the head (bone 5) towards the head-tracking target: yaw about the line from the spine to the head, pitch
// about the side, each found in its plane through the head, held within limits and turned at the tracking speed.
// The eyes take what the head has still to turn. Turned back to the front when tracking ends.
// AUTOINJECT
void AnimObjectHeadTrack(obj_tag *obj, AnimObj *animObj, quaternion_tag *worldQuats, _MATRIX *boneMtxs, int bone) {
    if (ScriptCam == 0 && AnimObjectNearestLod(obj) > 0x20)
        return;

    if (animObj->flags & (ANIMOBJ_AIMING | ANIMOBJ_AIMING_2))
        animObj->flags &= ~ANIMOBJ_HEAD_TRACK;
    int state = 0;
    if (animObj->flags & ANIMOBJ_HEAD_TRACK)
        state = 1;
    if (animObj->flags & ANIMOBJ_HEAD_TURNED)
        state |= 2;
    animObj->flags |= ANIMOBJ_HEAD_TURNED;

    _MATRIX *head = &boneMtxs[bone];
    _VECTOR *headPos = Mat_Position(*head);
    _VECTOR ahead, up, side;
    Vec_Copy((_VECTOR *)&head->m[8], &ahead);
    Vec_UnitVector(&up, Mat_Position(boneMtxs[BONE_AIM]), headPos);
    Vec_CrossNormalise(&up, &ahead, &side);

    _VECTOR *target;
    _VECTOR straightOn;
    if (!(animObj->flags & ANIMOBJ_HEAD_TARGET_OBJ)) {
        target = (_VECTOR *)animObj->headTrackTarget;
    } else {
        const obj_tag *targetObj = (const obj_tag *)animObj->headTrackTarget;
        if (targetObj != NULL && targetObj->animState != NULL) {
            target = Mat_Position(targetObj->animState->animObj.boneMtxs[animObj->headTrackTargetBone]);
        } else {
            Vec_Add2(headPos, &ahead, &straightOn);
            target = &straightOn;
        }
    }

    uint found = 0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    if (state == 0) {
        animObj->headYaw = 0.0f;
        animObj->headPitch = 0.0f;
    }
    if (state != 2 && Vec_SqDist3D(headPos, target) <= animObj->headTrackRange2) {
        plane_equ_tag plane;
        _VECTOR p1, p2, onPlane, front, toFront, toTarget;

        // Pitch: in the plane of up and ahead
        Vec_Add2(headPos, &up, &p1);
        Vec_Add2(headPos, &ahead, &p2);
        Plane_PlaneEq(&plane, headPos, &p1, &p2);
        Plane_PutPntOnPlane(&onPlane, target, &plane);
        if (Vec_SqDist3D(headPos, &onPlane) >= 0.25f) {
            found = 2;
            Plane_PutPntOnPlane(&front, &p2, &plane);
            Vec_UnitVector(&toFront, headPos, &front);
            Vec_UnitVector(&toTarget, headPos, &onPlane);
            double cosine = DotZYX(&toFront, &toTarget);
            if (cosine >= 0.0) {
                pitch = ClampedAcos((float)cosine);
                if (Vec_ScalarTripleProduct(&toFront, &toTarget, &side) < 0.0f)
                    pitch = -pitch;
            }
        }

        // Yaw: in the plane of side and ahead
        Vec_Add2(headPos, &side, &front);
        Vec_Add2(headPos, &ahead, &onPlane);
        Plane_PlaneEq(&plane, headPos, &front, &onPlane);
        Plane_PutPntOnPlane(&p2, target, &plane);
        if (Vec_SqDist3D(headPos, &p2) >= 0.25f) {
            found |= 1;
            Plane_PutPntOnPlane(&p1, &onPlane, &plane);
            Vec_UnitVector(&toTarget, headPos, &p1);
            Vec_UnitVector(&toFront, headPos, &p2);
            yaw = ClampedAcos((float)DotZYX(&toTarget, &toFront));
            if (Vec_ScalarTripleProduct(&toTarget, &toFront, &up) < 0.0f)
                yaw = -yaw;
        }
        if (found == 1)
            pitch = 0.0f;
        else if (found == 2)
            yaw = 0.0f;
    }

    float wantedYaw = yaw;
    float wantedPitch = pitch;
    if (Magnitude(yaw) > 2.6f)
        yaw = 0.0f;
    else if (yaw < -1.1f)
        yaw = -1.1f;
    else if (yaw > 1.1f)
        yaw = 1.1f;
    if (pitch < -1.0f)
        pitch = -1.0f;
    else if (pitch > 0.6f)
        pitch = 0.6f;
    if (animObj->headTrackDelay != 0) {
        animObj->headTrackDelay--;
        yaw = animObj->headYaw;
        pitch = animObj->headPitch;
    }

    float newYaw = (float)Ang_RotateToBearing(animObj->headYaw, yaw, animObj->headTrackSpeed, PI);
    float newPitch = (float)Ang_RotateToBearing(animObj->headPitch, pitch, animObj->headTrackSpeed, PI);
    animObj->headPitch = newPitch;
    animObj->headYaw = newYaw;

    quaternion_tag yawQuat, pitchQuat, turn, turned;
    yawQuat.q[0] = up.x;
    yawQuat.q[1] = up.y;
    yawQuat.q[2] = up.z;
    yawQuat.q[3] = newYaw;
    Quat_AxisAngleToQuat(yawQuat.q, &yawQuat);
    pitchQuat.q[0] = side.x;
    pitchQuat.q[1] = side.y;
    pitchQuat.q[2] = side.z;
    pitchQuat.q[3] = newPitch;
    Quat_AxisAngleToQuat(pitchQuat.q, &pitchQuat);
    MultiplyQuaternionAltOrder(&yawQuat, &pitchQuat, &turn);
    MultiplyQuaternionAltOrder(&turn, &worldQuats[bone], &turned);
    Quat_Copy(&worldQuats[bone], &turned);
    Quat_QuatToMat(&turned, head);

    sAnimSkin_tag *skin = animObj->skin;
    if (skin->numMorphs != 0) {
        double pitchLeft = (double)wantedPitch - newPitch;
        double eyes = ((double)wantedYaw - newYaw);
        eyes = eyes + eyes + 0.5f;
        if (eyes < 0.0)
            eyes = 0.0;
        else if (eyes > 1.0)
            eyes = 1.0;
        animObj->lookMorphX = (uchar)Ftol(eyes * 255.0f);
        eyes = pitchLeft + pitchLeft + 0.5f;
        if (eyes < 0.0)
            eyes = 0.0;
        else if (eyes > 1.0)
            eyes = 1.0;
        animObj->lookMorphY = (uchar)Ftol((1.0 - eyes) * 255.0f);
    }

    if (state == 2 && Magnitude(newYaw) < 0.01f && Magnitude(newPitch) < 0.01f) {
        animObj->flags &= ~ANIMOBJ_HEAD_TRACKING;
        if (skin->numMorphs != 0) {
            animObj->lookMorphX = 0;
            animObj->lookMorphY = 0;
        }
    }
}

// The aim's limits: the player's pitch, and everyone else's yaw and pitch
static constexpr float kPlayerPitchMin = -HALF_PI;
static constexpr float kPlayerPitchMax = 1.2707963f;
static constexpr float kAimLimit = 0.9f;
static_assert(std::bit_cast<uint32_t>(kPlayerPitchMin) == 0xbfc90fdb, "-pi/2");
static_assert(std::bit_cast<uint32_t>(kPlayerPitchMax) == 0x3fa2a974, "1.2707963");

// Turns the spine (bone 2) so the gun points at the aim target: pitch about the side and (not for the player) yaw
// about the line from the pelvis, each the angle that brings the gun's line through the target in its plane through
// the pelvis, held within limits and turned at the aim rate. Turned back to the front when aiming ends.
// AUTOINJECT
void AnimObjectAimAt(obj_tag *obj, AnimObj *animObj, quaternion_tag *worldQuats, _MATRIX *boneMtxs, int bone) {
    animObj->flags &= ~(ANIMOBJ_AIM_IN_RANGE | ANIMOBJ_AIM_YAW_LIMIT | ANIMOBJ_AIM_PITCH_LIMIT);
    uint flags = animObj->flags;
    if (ScriptCam == 0 && AnimObjectNearestLod(obj) > 0xfe)
        return;
    int state = (flags & ANIMOBJ_AIMING) ? 1 : 0;
    if (flags & ANIMOBJ_AIMING_2)
        state |= 2;
    animObj->flags = flags | ANIMOBJ_AIMING_2;

    // The gun's datum: AnimDatumGetWeaponInfo's, inline
    int weapon = obj->animState->currentWeaponId;
    int datum = -1;
    if ((uint)weapon <= 0x72 && weapon_data[weapon].weaponDatum != 0xff)
        datum = weapon_data[weapon].weaponDatum;
    if (datum < 0)
        return;
    if (MPSettings.isMultiplayer && weapon_data[weapon].animSet == 0)
        return;

    int datumOffset = 3;
    uint which = datum;
    if (AnimDatumGetIndex(obj, datum + 3) < 0) {
        datumOffset = 1;
        if (AnimDatumGetIndex(obj, datum + 1) < 0)
            which = 0x40;
    }
    _VECTOR muzzle;
    _MATRIX muzzleMtx;
    AnimGetBoneWorldTrans2(obj, which, datumOffset, &muzzle, &muzzleMtx);

    _VECTOR *pelvis = Mat_Position(boneMtxs[animObj->skin->bones[bone] & ANIM_BONE_PARENT_MASK]);
    _VECTOR barrel, gunDir, up, side;
    barrel.x = -1.0f;
    barrel.y = 0.0f;
    barrel.z = 0.0f;
    ApplyMatrixLV(&muzzleMtx, &barrel, &gunDir);
    _VECTOR ahead;
    Vec_Copy(&gunDir, &ahead);
    Vec_UnitVector(&up, pelvis, Mat_Position(boneMtxs[bone]));
    Vec_CrossNormalise(&up, &ahead, &side);

    _VECTOR *target;
    _VECTOR straightOn;
    if (!(animObj->flags & ANIMOBJ_AIM_AT_BONE)) {
        target = (_VECTOR *)animObj->aimTarget;
    } else {
        const obj_tag *targetObj = (const obj_tag *)animObj->aimTarget;
        if (targetObj != NULL && targetObj->animState != NULL) {
            target = Mat_Position(targetObj->animState->animObj.boneMtxs[animObj->aimTargetBone]);
        } else {
            Vec_Add2(pelvis, &ahead, &straightOn);
            target = &straightOn;
        }
    }

    uint found = 0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    if (state == 0) {
        animObj->aimYaw = 0.0f;
        animObj->aimPitch = 0.0f;
    }
    bool isPlayer = obj->objectType == OBJECTTYPE_PLAYER;
    if (state != 2 && Vec_SqDist3D(pelvis, target) <= animObj->aimRange2) {
        animObj->flags |= ANIMOBJ_AIM_IN_RANGE;
        // A point well along the gun's line
        Vec_Scalef(&gunDir, 100.0f, &gunDir);
        Vec_Add2(&muzzle, &gunDir, &gunDir);

        plane_equ_tag plane;
        _VECTOR p1, p2, muzzleOn, farOn, targetOn, hit, toTarget, toHit;

        // Pitch: in the plane of up and ahead
        Vec_Add2(pelvis, &up, &p1);
        Vec_Add2(pelvis, &ahead, &p2);
        Plane_PlaneEq(&plane, pelvis, &p1, &p2);
        Plane_PutPntOnPlane(&muzzleOn, &muzzle, &plane);
        Plane_PutPntOnPlane(&farOn, &gunDir, &plane);
        Plane_PutPntOnPlane(&targetOn, target, &plane);
        if (Vec_SqDist3D(pelvis, &targetOn) >= 0.25f)
            found = 2;
        float muzzleDist = Vec_Dist3D(pelvis, &muzzleOn);
        double targetDist = Vec_Dist3D(pelvis, &targetOn);
        if (targetDist >= muzzleDist) {
            if (Vec_LineSphereIntersect(&muzzleOn, &farOn, pelvis, (float)targetDist, &hit)) {
                Vec_UnitVector(&toTarget, pelvis, &targetOn);
                Vec_UnitVector(&toHit, pelvis, &hit);
                float cosine = (float)DotZYX(&toTarget, &toHit);
                if (isPlayer || cosine >= 0.0f) {
                    pitch = ClampedAcos(cosine);
                    if (Vec_ScalarTripleProduct(&toTarget, &toHit, &side) >= 0.0f)
                        pitch = -pitch;
                }
            }
            float low = isPlayer ? kPlayerPitchMin : -kAimLimit;
            float high = isPlayer ? kPlayerPitchMax : kAimLimit;
            if (pitch < low) {
                pitch = low;
                animObj->flags |= ANIMOBJ_AIM_PITCH_LIMIT;
            } else if (pitch > high) {
                pitch = high;
                animObj->flags |= ANIMOBJ_AIM_PITCH_LIMIT;
            }
        }

        // Yaw: in the plane of side and ahead, for all but the player
        if (obj->objectType != OBJECTTYPE_PLAYER) {
            Vec_Add2(pelvis, &side, &p1);
            Vec_Add2(pelvis, &ahead, &p2);
            Plane_PlaneEq(&plane, pelvis, &p1, &p2);
            Plane_PutPntOnPlane(&muzzleOn, &muzzle, &plane);
            Plane_PutPntOnPlane(&farOn, &gunDir, &plane);
            Plane_PutPntOnPlane(&targetOn, target, &plane);
            if (Vec_SqDist3D(pelvis, &targetOn) >= 0.25f)
                found |= 1;
            muzzleDist = Vec_Dist3D(pelvis, &muzzleOn);
            targetDist = Vec_Dist3D(pelvis, &targetOn);
            if (targetDist >= muzzleDist
                && Vec_LineSphereIntersect(&muzzleOn, &farOn, pelvis, (float)targetDist, &hit)) {
                Vec_UnitVector(&toTarget, pelvis, &targetOn);
                Vec_UnitVector(&toHit, pelvis, &hit);
                float cosine = (float)DotZYX(&toTarget, &toHit);
                bool limited = false;
                if (isPlayer || cosine >= 0.0f) {
                    yaw = ClampedAcos(cosine);
                    if (Vec_ScalarTripleProduct(&toTarget, &toHit, &up) >= 0.0f)
                        yaw = -yaw;
                    if (yaw < -kAimLimit) {
                        yaw = -kAimLimit;
                        limited = true;
                    }
                }
                // The upper limit tests the pitch, not the yaw (kept)
                if (!limited && pitch > kAimLimit) {
                    yaw = kAimLimit;
                    limited = true;
                }
                if (limited)
                    animObj->flags |= ANIMOBJ_AIM_YAW_LIMIT;
            }
        }
        if (found == 1)
            pitch = 0.0f;
        else if (found == 2)
            yaw = 0.0f;
    }

    float newYaw = (float)Ang_RotateToBearing(animObj->aimYaw, yaw, animObj->aimRate, PI);
    float newPitch = (float)Ang_RotateToBearing(animObj->aimPitch, pitch, animObj->aimRate, PI);
    animObj->aimPitch = newPitch;
    animObj->aimYaw = newYaw;

    quaternion_tag yawQuat, pitchQuat, turn, turned;
    yawQuat.q[0] = up.x;
    yawQuat.q[1] = up.y;
    yawQuat.q[2] = up.z;
    yawQuat.q[3] = newYaw;
    Quat_AxisAngleToQuat(yawQuat.q, &yawQuat);
    pitchQuat.q[0] = side.x;
    pitchQuat.q[1] = side.y;
    pitchQuat.q[2] = side.z;
    pitchQuat.q[3] = newPitch;
    Quat_AxisAngleToQuat(pitchQuat.q, &pitchQuat);
    MultiplyQuaternionAltOrder(&yawQuat, &pitchQuat, &turn);
    MultiplyQuaternionAltOrder(&turn, &worldQuats[bone], &turned);
    Quat_Copy(&worldQuats[bone], &turned);
    Quat_QuatToMat(&turned, &boneMtxs[bone]);

    if (state == 2 && Magnitude(newYaw) < 0.01f && Magnitude(newPitch) < 0.01f)
        animObj->flags &= ~(ANIMOBJ_AIMING | ANIMOBJ_AIMING_2);
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing

// Draws the object as last updated: its rigged bodies with the palette (and its strongest morphs, near a view), the
// held weapon at its datum, the laser sight's beam, and the entities at the datums
// AUTOINJECT
void AnimObjectDraw(obj_tag *obj, viewer_tag *viewer) {
    if (obj == NULL || obj->animState == NULL)
        return;
    AnimObj *animObj = &obj->animState->animObj;
    if (!(animObj->flags & ANIMOBJ_POSE_VALID))
        return;
    if ((int)(GameState.NumFrames - animObj->lastUpdateFrame) > 1)
        return;
    animObj->lastUpdateFrame = GameState.NumFrames;
    psiBuildMatrixPalette(obj, animObj, 1);
    sAnimSkin_tag *skin = animObj->skin;
    if (skin->numRigged != 0 && skin->numDiscrete != 0)
        View_SetupRenderModes2(viewer, obj);
    else
        View_SetupRenderModes(viewer, obj);

    // The original reads this, as the weapon's model, for a weapon number out of range: it is uninitialised stack
    // unless the morphs were sorted, ours 0 then (docs/nightfire-port-lifts.md, Departures)
    uint morphCount = 0;
    Morph_Count = 0;
    uint numMorphs;
    if (AnimObjectIsClose(obj, 12) && (numMorphs = skin->numMorphs) != 0) {
        for (uint i = 0; i < numMorphs; i++) {
            Morph_Weight[i] = animObj->morphWeights[i];
            Morph_Index[i] = (uchar)i;
        }
        // The strongest eight, strongest first
        uint strongest = numMorphs < 8 ? numMorphs : 8;
        uint i;
        for (i = 0; i < strongest; i++) {
            for (uint j = i + 1; j < numMorphs; j++) {
                if (Morph_Weight[i] < Morph_Weight[j]) {
                    float weight = Morph_Weight[i];
                    uchar index = Morph_Index[j];
                    Morph_Weight[i] = Morph_Weight[j];
                    Morph_Weight[j] = weight;
                    Morph_Index[j] = Morph_Index[i];
                    Morph_Index[i] = index;
                }
            }
        }
        // Less the weakest that are next to nothing, back in index order
        uchar count = (uchar)i;
        for (int k = (int)i - 1; k >= 0 && !(Magnitude(Morph_Weight[k]) >= 1e-06f); k--)
            count = (uchar)k;
        Morph_Count = count;
        morphCount = count;
        for (uint a = 0; a + 1 < morphCount; a++) {
            for (uint b = a + 1; b < morphCount; b++) {
                if (Morph_Index[a] > Morph_Index[b]) {
                    float weight = Morph_Weight[a];
                    uchar index = Morph_Index[a];
                    Morph_Weight[a] = Morph_Weight[b];
                    Morph_Index[a] = Morph_Index[b];
                    Morph_Weight[b] = weight;
                    Morph_Index[b] = index;
                }
            }
        }
    }

    uint flags = animObj->flags;
    celglist_tag *sleeve = animObj->sleeveGlist;
    _MATRIX unused;
    for (int i = 0; i < skin->numRigged; i++) {
        celglist_tag *body = skin->riggedBody[i];
        if (body == NULL)
            continue;
        if (sleeve != NULL && i == 0)
            body = sleeve;
        psiDrawSkinObjectMatrix(body, &unused, obj, animObj->palette);
    }
    Morph_Count = 0;

    if ((flags & ANIMOBJ_FULL_CHARACTER) && skin->numDatums != 0) {
        // The held weapon's model, at its datum
        int weapon = obj->animState->currentWeaponId;
        HASHCODE model;
        int datum = -1;
        if ((uint)weapon <= 0x72) {
            model = weapon_data[weapon].worldModelGfx;
            if (weapon_data[weapon].weaponDatum != 0xff)
                datum = weapon_data[weapon].weaponDatum;
        } else {
            model = (HASHCODE)morphCount;
        }
        if (datum < 0)
            datum = 0;
        if (model != 0)
            AnimDatumSetEntity(obj, datum, model, 1);
    }

    if (obj->animState->currentWeaponId == WEAPON_LASER_PISTOL) {
        // The beam, from the gun's datum to what it hits
        // gunMtx's translation as for the fire event in AnimProcessScriptCmds: 0 where the original has stack
        HITDATA_tag *hitList = NULL;
        _VECTOR from, to;
        _MATRIX gunMtx = {};
        AnimGetBoneWorldTrans(obj, 8, 3, &from, &gunMtx);
        _VECTOR reach;
        reach.x = -100.0f;
        reach.y = 0.0f;
        reach.z = 0.0f;
        MatrixMultiplyVector(&gunMtx, &reach, &to);
        if (Collide_RayIntersect(&from, &to, obj->inCel, obj, NULL, &hitList, 0, HITMASK_SKIP_NO_CONTACT, 0) &&
            hitList != NULL)
            Vec_Copy(&hitList->hitPosition, &to);
        Collide_FreeHitList(&hitList);
        celglist_tag *beam = (celglist_tag *)hashtable_getitem(viewer->visionMode == VISION_NIGHT
                                                               ? HT_Entity_lazer
                                                                   : HT_Entity_watch_lazer);
        if (beam != NULL) {
            to.x = from.x - to.x;
            to.y = from.y - to.y;
            to.z = from.z - to.z;
            _VECTOR rot, scale;
            Vec_Cartesian_2_Spherical(&rot, to.x, to.y, to.z);
            rot.x = -rot.x;
            rot.z = 0.0f;
            scale.y = 1.0f;
            scale.x = 1.0f;
            scale.z = Vec_Magnitude(&to);
            View_DrawGlist(beam, &from, &rot, &scale);
        }
    }

    for (int i = 0; i < skin->numDatums; i++) {
        uchar timer = animObj->datumTimer[i];
        if (timer == 0)
            continue;
        if (timer != 0xff)
            animObj->datumTimer[i] = timer - 1;
        celglist_tag *entity = animObj->datumGlist[i];
        if (entity == NULL)
            continue;
        _MATRIX m;
        Quat_QuatTransToMat(&skin->datums[i].q, (float *)&skin->datums[i].t, &m);
        if (skin->datums[i].bone >= 0)
            Concat(&animObj->boneMtxs[skin->datums[i].bone], &m);
        if (!(entity->applyFlagsToObject & 0x800))
            psiSetTweakARGB(obj->tweakA, obj->tweakR, obj->tweakG, obj->tweakB);
        psiDrawObjectMatrix(entity, &m);
    }
    bShadowCharacter = 0;
    bCharacterLight = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Aiming and joint collision

// The joint box tables, by skin, and the boxes AnimGetCollData builds from them
// XBE_GLOBAL(0x00163198, 0x154) readonly relocate
const AnimCollBone HumanCollisionBones[17] = {
    {5, -1, 0.15625f, {0.11f, 0.11f, 0.11f}},
    {4, 1, 0.203125f, {0.2f, 0.25f, 0.15f}},
    {53, -1, 0.093759f, {0.12f, 0.1f, 0.12f}},
    {54, -1, 0.17185f, {0.1f, 0.25f, 0.1f}},
    {55, -1, 0.109375f, {0.1f, 0.22f, 0.1f}},
    {56, -1, 0.078125f, {0.07f, 0.07f, 0.15f}},
    {49, -1, 0.109375f, {0.12f, 0.1f, 0.12f}},
    {50, -1, 0.17185f, {0.1f, 0.25f, 0.1f}},
    {51, -1, 0.109375f, {0.1f, 0.22f, 0.1f}},
    {52, -1, 0.078125f, {0.07f, 0.07f, 0.15f}},
    {20, -1, 0.109375f, {0.2f, 0.07f, 0.07f}},
    {21, -1, 0.0625f, {0.2f, 0.07f, 0.07f}},
    {23, 21, 0.046875f, {0.1f, 0.05f, 0.05f}},
    {32, -1, 0.109375f, {0.2f, 0.07f, 0.07f}},
    {35, -1, 0.0625f, {0.2f, 0.07f, 0.07f}},
    {39, 35, 0.046875f, {0.1f, 0.05f, 0.05f}},
    {-1, -1, 0.0f, {0.0f, 0.0f, 0.0f}},
};
// XBE_GLOBAL(0x001632f0, 0x154) readonly relocate
const AnimCollBone AstronautCollisionBones[17] = {
    {5, -1, 0.15625f, {0.15f, 0.2f, 0.1f}},
    {3, 1, 0.203125f, {0.2f, 0.25f, 0.15f}},
    {46, -1, 0.093759f, {0.12f, 0.1f, 0.12f}},
    {47, -1, 0.17185f, {0.1f, 0.25f, 0.1f}},
    {48, -1, 0.109375f, {0.1f, 0.22f, 0.1f}},
    {49, -1, 0.078125f, {0.07f, 0.07f, 0.15f}},
    {42, -1, 0.109375f, {0.12f, 0.1f, 0.12f}},
    {43, -1, 0.17185f, {0.1f, 0.25f, 0.1f}},
    {44, -1, 0.109375f, {0.1f, 0.22f, 0.1f}},
    {45, -1, 0.078125f, {0.07f, 0.07f, 0.15f}},
    {16, -1, 0.109375f, {0.2f, 0.07f, 0.07f}},
    {17, -1, 0.0625f, {0.2f, 0.07f, 0.07f}},
    {19, 17, 0.046875f, {0.1f, 0.05f, 0.05f}},
    {27, -1, 0.109375f, {0.2f, 0.07f, 0.07f}},
    {29, -1, 0.0625f, {0.2f, 0.07f, 0.07f}},
    {33, 29, 0.046875f, {0.1f, 0.05f, 0.05f}},
    {-1, -1, 0.0f, {0.0f, 0.0f, 0.0f}},
};
// XBE_GLOBAL(0x001d6bf0, 0x980)
static COLLJOINT_tag HitList2[32];

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

// Turns the object's head to target (NULL stops it), within range, at rate: an object's bone with targetIsObj,
// else a position. With randomDelay it waits a tenth of a second plus up to a quarter more first.
// AUTOINJECT
void AnimObjectSetHeadTracking(obj_tag *obj, const void *target, float range, float rate, char randomDelay,
                               char targetIsObj, uchar bone) {
    if (obj == NULL || obj->animState == NULL)
        return;
    AnimObj *animObj = &obj->animState->animObj;
    uchar delay = 0;
    animObj->headTrackRange2 = range * range;
    animObj->headTrackSpeed = rate;
    if (target == NULL) {
        animObj->flags &= ~ANIMOBJ_HEAD_TRACK;
        animObj->headTrackTarget = NULL;
    } else {
        if (randomDelay)
            delay = Rand_Rand(FRAME_RATE_INT / 4) + FRAME_RATE_INT / 10;
        animObj->flags |= ANIMOBJ_HEAD_TRACK;
        animObj->headTrackTarget = target;
        if (targetIsObj) {
            animObj->headTrackTargetBone = bone;
            animObj->flags |= ANIMOBJ_HEAD_TARGET_OBJ;
        } else {
            animObj->flags &= ~ANIMOBJ_HEAD_TARGET_OBJ;
        }
    }
    animObj->headTrackDelay = delay;
}

// The boxes round an animated object's joints, in the world, for Collide_Jointy: one per row of the skin's table,
// in HitList2, with their count. The first box sits as far above its bone as the bone is above the other. NULL and
// no boxes unless the object is animated with a pose and is a full character.
// AUTOINJECT
COLLJOINT_tag* AnimGetCollData(obj_tag *obj, ushort *count) {
    if (obj == NULL || obj->animState == NULL) {
        *count = 0;
        return NULL;
    }
    AnimObj *animObj = &obj->animState->animObj;
    if (!(animObj->flags & ANIMOBJ_POSE_VALID) || !(animObj->flags & ANIMOBJ_FULL_CHARACTER)) {
        *count = 0;
        return NULL;
    }

    psiBuildMatrixPalette(obj, animObj, 0);
    sAnimSkin_tag *skin = animObj->skin;
    const AnimCollBone *row = skin->hashcode == HT_Skin_Astronaut ? AstronautCollisionBones : HumanCollisionBones;
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

// ---------------------------------------------------------------------------------------------------------------
// The roots' translations, for others

// The root translation of a script's bone sequence at the script's current frame. False, with out untouched, for a
// script without ANIMSCRIPT_HAS_ROOT_TRANS or whose sequence is not loaded.
// AUTOINJECT
bool AnimGetFirstTrans(sAnimScript_tag *script, _VECTOR *out) {
    float frame[ANIM_FRAME_BUFFER_SIZE / sizeof(float)];

    if (!(script->flags & ANIMSCRIPT_HAS_ROOT_TRANS))
        return false;

    sAnimSeq_tag *seq = script->boneAnimSeq;
    if (AnimSeqGetInfo(NULL, seq->hashcode, seq) == NULL)
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
    AnimSeqGetInfo(NULL, hashcode, &seq);
    seq.firstFrame = 1;
    seq.lastFrame = seq.numFrames;
    AnimFrameCopy(pose, &seq, NULL, (float)frame);
    out->x = pose[0];
    out->y = pose[1];
    out->z = pose[2];
}

// ---------------------------------------------------------------------------------------------------------------
// Sets

// AnimTagGetUnique, inline on the Xbox: the next set's tag
static uint AnimTagGetUnique(void) {
    if (AnimTagTimeStamp == 0)
        AnimTagTimeStamp = 1;
    return AnimTagTimeStamp++;
}

// A new set linked in after after (or on its own), with its tag; NULL if it cannot be allocated
sAnimSet_tag* AnimTagAllocAndGetUnique(sAnimSet_tag *after) {
    sAnimSet_tag *set = (sAnimSet_tag *)Mem_Malloc(sizeof(sAnimSet_tag), MALLOC_ANIM_SET_STRUCTS, 0);
    if (set == NULL)
        return NULL;
    memset(set, 0, sizeof(sAnimSet_tag));
    if (after == NULL) {
        set->prev = NULL;
        set->next = NULL;
    } else {
        sAnimSet_tag *next = after->next;
        set->prev = after;
        set->next = next;
        after->next = set;
        if (next != NULL)
            next->prev = set;
    }
    set->tag = AnimTagGetUnique();
    return set;
}

// Starts a set on list (idle, walks slowest first, 0, then the strafes), deleting the object's scripts
void AnimSetInit(sAnimSet_tag *set, HASHCODE *list, obj_tag *obj, float a, float b) {
    if (obj == NULL || obj->animState == NULL || set == NULL)
        return;
    AnimObj *animObj = &obj->animState->animObj;
    set->speedList = list;
    set->speedCount = 0;
    while (set->speedList[set->speedCount] != 0)
        set->speedCount++;
    set->strafeList = &list[set->speedCount + 1];
    set->strafeCount = 0;
    while (set->strafeList[set->strafeCount] != 0)
        set->strafeCount++;
    NF_WARN_IF(set->speedCount < 3, "Too few move anims");     // GC check (0x8004754c)
    NF_WARN_IF(set->strafeCount < 3, "Too few idle anims");    // GC check (0x8004754c)
    set->oneShotState = 0;
    set->strafeDir = 0xff;
    set->strafeScript = NULL;
    set->speedIndex = -1;
    set->switchCooldown = 0;
    set->walkTimestamp = 0;
    set->blendTimestamp = 0;
    set->blendScript = NULL;
    set->idleTimer = (Rand_Random() & 0x3ff) + 0xff;
    set->a = a;
    animObj->distancePhase = a;
    set->b = b;
    AnimListBuild(obj, ANIMLIST_ALL, 0, NULL, ANIMLIST_RESTART);
    for (int i = 0; i < GlbAnimListCount; i++)
        AnimScriptDelete(obj, &GlbAnimList[i]);
    GlbAnimListCount = 0;
}

// A set at the end of the object's, up to four
// AUTOINJECT
sAnimSet_tag* AnimSetAppend(obj_tag *obj, HASHCODE *list, float a, float b) {
    if (obj == NULL || obj->animState == NULL)
        return NULL;
    AnimObj *animObj = &obj->animState->animObj;
    sAnimSet_tag *set = animObj->animSets;
    if (set == NULL) {
        set = (sAnimSet_tag *)Mem_Malloc(sizeof(sAnimSet_tag), MALLOC_ANIM_SET_STRUCTS, 0);
        if (set != NULL) {
            memset(set, 0, sizeof(sAnimSet_tag));
            set->prev = NULL;
            set->next = NULL;
            set->tag = AnimTagGetUnique();
        }
        animObj->animSets = set;
    } else {
        int count = 1;
        while (set->next != NULL) {
            set = set->next;
            count++;
        }
        if (count >= 4)
            return NULL;
        set = AnimTagAllocAndGetUnique(set);
    }
    AnimSetInit(set, list, obj, a, b);
    return set;
}

// A set, its strafe and its scripts deleted and unlinked; *slot is freed
// AUTOINJECT
void AnimSetDelete(obj_tag *obj, sAnimSet_tag **slot) {
    if (obj == NULL || obj->animState == NULL)
        return;
    AnimState *animState = obj->animState;
    sAnimSet_tag *set = *slot;
    if (set->strafeScript != NULL)
        AnimScriptDelete(obj, &set->strafeScript);
    AnimListBuild(obj, ANIMLIST_TAG, (*slot)->tag, NULL, ANIMLIST_RESTART);
    AnimListDelete(obj);
    set = *slot;
    if (set->prev == NULL)
        animState->animObj.animSets = set->next;
    else
        set->prev->next = set->next;
    if (set->next != NULL)
        set->next->prev = set->prev;
    Mem_Free((void **)slot);
}

// AUTOINJECT
void AnimSetDeleteAll(obj_tag *obj) {
    if (obj == NULL || obj->animState == NULL)
        return;
    sAnimSet_tag *set = obj->animState->animObj.animSets;
    while (set != NULL) {
        sAnimSet_tag *next = set->next;
        AnimSetDelete(obj, &set);
        set = next;
    }
}

// The set's scripts for the speed: idle, or two walks blended by where the speed falls between them and kept in step
// by the distance walked, changing at most every four ticks; a one-shot plays out first. In multiplayer, a strafe
// over them for the sideways speed.
// AUTOINJECT
void AnimSetUpdate(obj_tag *obj, sAnimSet_tag *set, float speed, float forward, float maxForward, float side,
                   float maxSide) {
    float blend = 0.0f;
    if (obj == NULL || obj->animState == NULL || set == NULL)
        return;
    AnimObj *animObj = &obj->animState->animObj;
    if (speed <= 0.0002f && speed >= -0.0002f)
        forward = 0.0f;
    animObj->distanceThisTick = forward * set->b;

    bool walking = true;
    while (set->oneShotState != 0) {
        if (set->oneShotState != 1) {
            walking = false;
            break;
        }
        sAnimScript_tag *oneShot = AnimListBuild(obj, ANIMLIST_TAG, set->tag, NULL, ANIMLIST_RESTART);
        if (!(oneShot->flags & ANIMSCRIPT_STOPPED) && speed == 0.0f) {
            walking = false;
            break;
        }
        AnimScriptDelete(obj, &oneShot);
        set->oneShotState = 0;
    }

    if (walking) {
        double pace = Magnitude(forward);
        if (pace < 0.0)
            pace = 0.0;
        else if (pace > maxForward)
            pace = maxForward;
        pace = pace / maxForward;

        int index = 0;
        if (pace > 0.0002f || pace < -0.0002f || pace != pace) {
            int top = set->speedCount - 2;
            double scaled = pace * top;
            index = Ftol(scaled);
            if (index == top)
                index--;
            blend = (float)(scaled - index);
            index++;
        }

        if (set->switchCooldown != 0)
            set->switchCooldown--;
        if (index != set->speedIndex) {
            if (set->switchCooldown != 0) {
                index = set->speedIndex;
            } else {
                set->switchCooldown = 4;
                bool fadeAll = set->speedIndex == 0 || index == 0;
                set->speedIndex = (char)index;
                if (fadeAll) {
                    AnimListBuild(obj, ANIMLIST_TAG, set->tag, NULL, ANIMLIST_RESTART);
                    for (int i = 0; i < GlbAnimListCount; i++) {
                        sAnimScript_tag *script = GlbAnimList[i];
                        if (!(script->flags & ANIMSCRIPT_FADING)) {
                            script->flags |= ANIMSCRIPT_FADING | ANIMSCRIPT_FADE_OUT;
                            script->fadeLen = 8.0f;
                            script->fadeTime = 8.0f;
                            script->fadeDir = -1;
                        }
                    }
                }
                if (index == 0) {
                    sAnimScript_tag *idle = AnimScriptAppend(obj, set->speedList[0]);
                    idle->groupTag = set->tag;
                } else {
                    if (!fadeAll) {
                        AnimListBuild(obj, ANIMLIST_TAG, set->tag, NULL, ANIMLIST_RESTART);
                        for (int i = 0; i < GlbAnimListCount; i++) {
                            sAnimScript_tag *script = GlbAnimList[i];
                            if (set->walkTimestamp == script->timestamp || set->blendTimestamp == script->timestamp)
                                AnimScriptDelete(obj, &script);
                        }
                    }
                    const uint walkFlags = ANIMSCRIPT_LOOP | ANIMSCRIPT_GAME_FLAG | ANIMSCRIPT_ZERO_ROOT_Z
                                           | ANIMSCRIPT_ZERO_ROOT_X;
                    sAnimScript_tag *walk = AnimScriptAppend(obj, set->speedList[index]);
                    walk->groupTag = set->tag;
                    walk->flags |= walkFlags;
                    walk->speedMode = ANIMSPEED_DISTANCE;
                    walk->boneAnimSeq->frameCallback = AnimSet_WalkUpdater;
                    set->walkTimestamp = walk->timestamp;
                    sAnimScript_tag *faster = AnimScriptAppend(obj, set->speedList[index + 1]);
                    faster->groupTag = set->tag;
                    faster->flags |= walkFlags;
                    faster->speedMode = ANIMSPEED_SYNC;
                    faster->syncMaster = walk;
                    set->blendTimestamp = faster->timestamp;
                    set->blendScript = faster;
                    faster->ownerSlot = &set->blendScript;
                }
                AnimObjectUpdate(obj);
            }
        }
        if (index != 0 && set->blendScript != NULL)
            set->blendScript->syncBlend = blend;

        AnimListBuild(obj, ANIMLIST_TAG, set->tag, NULL, ANIMLIST_RESTART);
        if (GlbAnimListCount == 1)
            animObj->distancePhase = set->a;
        else
            set->idleTimer = (Rand_Random() & 0x3ff) + 0xff;
    }

    if (!MPSettings.isMultiplayer)
        return;

    double sideways = (double)side / maxSide;
    float sidewaysF = (float)sideways;
    double ahead = (double)forward / maxForward;
    if (!(ahead >= 0.0))
        ahead = -ahead;
    Extended angle = FPatan(sideways, ahead) * Extended(0.63661975f);
    if (!((double)angle >= 0.0))
        angle = -angle;
    float strafe = (float)angle;

    int dir;
    if ((side <= 0.0002f && side >= -0.0002f) || strafe < 0.5f)
        dir = 0;
    else
        dir = side > 0.0f ? 1 : 2;

    if (dir != (char)set->strafeDir) {
        set->strafeDir = (uchar)dir;
        set->speedIndex = -1;
        if (set->strafeScript != NULL)
            AnimScriptDelete(obj, &set->strafeScript);
        if (dir == 0)
            return;
        set->strafeScript = AnimScriptAppendBlend(obj, set->speedList[set->speedCount + (dir == 1 ? 1 : 3)]);
        set->strafeScript->ownerSlot = &set->strafeScript;
        set->strafeScript->flags |= ANIMSCRIPT_LOOP | ANIMSCRIPT_GAME_FLAG | ANIMSCRIPT_ZERO_ROOT_Z
                                    | ANIMSCRIPT_ZERO_ROOT_X | ANIMSCRIPT_OVERLAY;
    } else if (dir == 0) {
        return;
    }
    if (set->strafeScript != NULL) {
        set->strafeScript->speed = Magnitude(sidewaysF);
        set->strafeScript->fadeTime = strafe;
    }
}
