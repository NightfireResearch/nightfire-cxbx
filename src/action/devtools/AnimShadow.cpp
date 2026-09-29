#include "AnimShadow.h"

#include "../actionhelpers.h"
#include "../../common/xbeOriginal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// Shadow tests: checking reimplementations against the originals on live data.
//
// With AnimShadowTest=on in settings.ini, AnimShadow_Install points each tested function's entry at a wrapper
// here instead of at its reimplementation. The wrapper snapshots everything the function can write, runs the
// original (XbeOriginalScope puts its entry bytes back for the call), keeps what it produced, restores the
// snapshot, runs ours - a plain call - and compares the two. Ours is what the game keeps. A summary is printed
// every power of two calls, with how many calls took each path through the function, so a pass says what it
// covered; the first few differences are printed in detail.
//
// The originals still call whatever they called before (our maths helpers, the original head tracking, and each
// other through these wrappers), so a difference points at the function being compared.
//
// Nothing here is game logic. The reimplementations know nothing of it.
// ---------------------------------------------------------------------------------------------------------------

#define ADDR_psiBuildMatrixPalette 0x000dd290
#define ADDR_AnimGetBoneWorldTrans 0x00013070
#define ADDR_AnimFrameCopy 0x00013e50

struct ShadowStats {
    const char *name;
    const char *pathNames[4];
    unsigned calls;
    unsigned differ;
    float maxErr;
    unsigned paths[4];
};

// The largest absolute difference between two float arrays, or INFINITY if one holds a NaN the other does not.
static float Shadow_MaxDiff(const float *a, const float *b, int count) {
    float worst = 0.0f;
    for (int i = 0; i < count; i++) {
        if (a[i] == b[i] || (a[i] != a[i] && b[i] != b[i]))
            continue;
        float d = fabsf(a[i] - b[i]);
        if (d != d)
            return INFINITY;
        if (d > worst)
            worst = d;
    }
    return worst;
}

// Records one comparison. 'err' is the largest float difference, 'otherDiffers' anything that is not a float
// (flags, bytes) and has to match exactly.
static void Shadow_Report(ShadowStats *st, unsigned pathMask, float err, bool otherDiffers, const char *detail) {
    const float tolerance = 1e-4f;
    st->calls++;
    for (int i = 0; i < 4; i++)
        if (pathMask & (1 << i))
            st->paths[i]++;
    if (err > st->maxErr)
        st->maxErr = err;
    if (err > tolerance || otherDiffers) {
        st->differ++;
        if (st->differ <= 10)
            printf("[animshadow] %s differs (call %u, max float difference %g%s): %s\n", st->name, st->calls, err,
                   otherDiffers ? ", non-float fields differ" : "", detail);
    }
    if ((st->calls & (st->calls - 1)) == 0 && st->calls >= 64) {
        char paths[160] = "";
        for (int i = 0; i < 4; i++)
            if (st->pathNames[i] != NULL)
                snprintf(paths + strlen(paths), sizeof(paths) - strlen(paths), "%s%s %u", i ? ", " : "",
                         st->pathNames[i], st->paths[i]);
        printf("[animshadow] %s: %u calls, %u differ, max float difference %g (%s)\n", st->name, st->calls,
               st->differ, st->maxErr, paths);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// psiBuildMatrixPalette. It writes the animated object (flags, head and aim angles, look-morph bytes) and the
// bone and palette matrices.
// ---------------------------------------------------------------------------------------------------------------

static ShadowStats g_palette = { "psiBuildMatrixPalette", { "bones built", "palette built", "head tracking", "aiming" } };

static void __cdecl Shadow_psiBuildMatrixPalette(obj_tag *obj, AnimObj *ao, char buildPalette) {
    int numBones = SkeletonTable[ao->skin->skeletonNum]->numBones;
    static _MATRIX bonesBefore[96], paletteBefore[96], bonesOrig[96], paletteOrig[96];
    size_t mtxBytes = numBones * sizeof(_MATRIX);

    unsigned pathMask = 0;
    if (!(ao->flags & ANIMOBJ_BONES_VALID)) {
        pathMask |= 1;
        if (ao->flags & (ANIMOBJ_HEADTRACK_REQ | ANIMOBJ_HEADTRACK_ON)) pathMask |= 4;
        if (ao->flags & (ANIMOBJ_AIM_REQ | ANIMOBJ_AIM_ON)) pathMask |= 8;
    }
    if (buildPalette && ao->skin->numRigged != 0 && !(ao->flags & ANIMOBJ_PALETTE_VALID))
        pathMask |= 2;

    AnimObj aoBefore = *ao;
    memcpy(bonesBefore, ao->boneMtxs, mtxBytes);
    if (ao->palette)
        memcpy(paletteBefore, ao->palette, mtxBytes);

    {
        XbeOriginalScope original(ADDR_psiBuildMatrixPalette);
        ((void(__cdecl *)(obj_tag *, AnimObj *, char))ADDR_psiBuildMatrixPalette)(obj, ao, buildPalette);
    }
    AnimObj aoOrig = *ao;
    memcpy(bonesOrig, ao->boneMtxs, mtxBytes);
    if (ao->palette)
        memcpy(paletteOrig, ao->palette, mtxBytes);

    *ao = aoBefore;
    memcpy(ao->boneMtxs, bonesBefore, mtxBytes);
    if (ao->palette)
        memcpy(ao->palette, paletteBefore, mtxBytes);
    psiBuildMatrixPalette(obj, ao, buildPalette);

    float errBones = Shadow_MaxDiff((float *)bonesOrig, (float *)ao->boneMtxs, numBones * 15);
    float errPalette = ao->palette ? Shadow_MaxDiff((float *)paletteOrig, (float *)ao->palette, numBones * 15) : 0;
    float errAngles = Shadow_MaxDiff(&aoOrig.headYaw, &ao->headYaw, 4);
    bool otherDiffers = aoOrig.flags != ao->flags || aoOrig.lookMorphX != ao->lookMorphX ||
                        aoOrig.lookMorphY != ao->lookMorphY || aoOrig.headTrackHoldTimer != ao->headTrackHoldTimer;

    float err = errBones;
    if (errPalette > err) err = errPalette;
    if (errAngles > err) err = errAngles;
    char detail[160];
    snprintf(detail, sizeof(detail), "%d bones, bones %g, palette %g, head/aim angles %g, flags %08x vs %08x",
             numBones, errBones, errPalette, errAngles, aoOrig.flags, ao->flags);
    Shadow_Report(&g_palette, pathMask, err, otherDiffers, detail);
}

// ---------------------------------------------------------------------------------------------------------------
// AnimGetBoneWorldTrans. Both versions start by bringing the bone matrices up to date; doing that first means
// neither has to, so the two compare the lookup alone.
// ---------------------------------------------------------------------------------------------------------------

static ShadowStats g_boneWorldTrans = { "AnimGetBoneWorldTrans", { "bone", "datum", "no pose or datum", NULL } };

static void __cdecl Shadow_AnimGetBoneWorldTrans(obj_tag *obj, uint which, int datumOffset, _VECTOR *posOut,
                                                 _MATRIX *mtxOut) {
    bool posed = obj != NULL && obj->animState != NULL && (obj->animState->animObj.flags & ANIMOBJ_POSE_VALID);
    if (posed)
        psiBuildMatrixPalette(obj, &obj->animState->animObj, 0);

    // Fields a version leaves unwritten (Mat_Identity sets only the rotation) must start equal in both.
    _VECTOR posOrig = *posOut;
    _MATRIX mtxOrig;
    if (mtxOut != NULL)
        mtxOrig = *mtxOut;
    {
        XbeOriginalScope original(ADDR_AnimGetBoneWorldTrans);
        ((void(__cdecl *)(obj_tag *, uint, int, _VECTOR *, _MATRIX *))ADDR_AnimGetBoneWorldTrans)(
            obj, which, datumOffset, &posOrig, mtxOut != NULL ? &mtxOrig : NULL);
    }
    AnimGetBoneWorldTrans(obj, which, datumOffset, posOut, mtxOut);

    float err = Shadow_MaxDiff((float *)&posOrig, (float *)posOut, 3);
    if (mtxOut != NULL) {
        float errMtx = Shadow_MaxDiff((float *)&mtxOrig, (float *)mtxOut, 15);
        if (errMtx > err)
            err = errMtx;
    }

    unsigned pathMask = 4;
    if (posed) {
        if (which & 0x80000000)
            pathMask = 1;
        else {
            sAnimSkin_tag *skin = obj->animState->animObj.skin;
            for (int d = 0; d < skin->numDatums; d++)
                if (skin->datums[d].id == (int)which + datumOffset)
                    pathMask = 2;
        }
    }
    char detail[128];
    snprintf(detail, sizeof(detail), "which %08x + %d, position (%g, %g, %g) vs (%g, %g, %g)", which, datumOffset,
             posOrig.x, posOrig.y, posOrig.z, posOut->x, posOut->y, posOut->z);
    Shadow_Report(&g_boneWorldTrans, pathMask, err, false, detail);
}

// ---------------------------------------------------------------------------------------------------------------
// AnimFrameCopy, compared bit for bit. It writes the output, the sequence (first-translation fields and flags),
// the raw sequence's cursor and the unpack cache.
//
// Every fourth decoded frame, up to 4000, also goes to anim_capture.bin, so an offline decoder (the Blender
// export's) can be checked against what the game computed (tools/anim/compare_capture.py). Each record: u32
// hashcode, f32 frame as passed, u8 skeletonNum, u8 morph, u16 element count, f32 skin scale[3], u32 used-bone
// mask[3] (all ones without a skin), u32 byte count, then the output.
// ---------------------------------------------------------------------------------------------------------------

static void Shadow_CaptureFrame(sAnimSeq_tag *seq, sAnimSkin_tag *skin, float frame, int numElems, bool morph,
                                const float *out, size_t outBytes) {
    static FILE *file = NULL;
    static int records = 0, calls = 0;
    if (records >= 4000 || (calls++ & 3) != 0) // every fourth, to reach past the opening cutscene
        return;
    if (file == NULL && (file = fopen("anim_capture.bin", "wb")) == NULL)
        return;
    uint used[3] = { 0xffffffff, 0xffffffff, 0xffffffff };
    if (skin != NULL && !morph) {
        used[0] = used[1] = used[2] = 0;
        for (int b = 0; b < numElems; b++)
            if (skin->bones[b] & ANIM_BONE_USED)
                used[b >> 5] |= 1u << (b & 31);
    }
    uchar hdr[4 + 4 + 1 + 1 + 2 + 12 + 12 + 4];
    uchar *h = hdr;
    memcpy(h, &seq->hashcode, 4); h += 4;
    memcpy(h, &frame, 4); h += 4;
    *h++ = seq->skeletonNum;
    *h++ = morph ? 1 : 0;
    ushort n = (ushort)numElems;
    memcpy(h, &n, 2); h += 2;
    memcpy(h, &Glb_SkinScale, 12); h += 12;
    memcpy(h, used, 12); h += 12;
    uint bytes = (uint)outBytes;
    memcpy(h, &bytes, 4); h += 4;
    fwrite(hdr, 1, sizeof(hdr), file);
    fwrite(out, 1, outBytes, file);
    if (++records % 500 == 0)
        fflush(file);
}

static ShadowStats g_frameCopy = { "AnimFrameCopy", { "bones", "morph", "unpack cache", "no skin" } };

static void __cdecl Shadow_AnimFrameCopy(float *out, sAnimSeq_tag *seq, sAnimSkin_tag *skin, float frame) {
    // Bit-exact float comparisons depend on the x87 precision the originals run at, on the game's own thread.
    static bool reportedFpu = false;
    if (!reportedFpu) {
        reportedFpu = true;
        unsigned short cw = 0;
#if defined(_MSC_VER) && !defined(__clang__)
        __asm fnstcw cw
#endif
        printf("[animshadow] x87 control word %04x (precision %s)\n", cw,
               ((cw >> 8) & 3) == 3 ? "64-bit" : ((cw >> 8) & 3) == 2 ? "53-bit" : "24-bit");
    }

    bool morph = (seq->flags & ANIMSEQ_FLAG_MORPH) != 0;
    int numElems = morph ? seq->numElems : SkeletonTable[seq->skeletonNum]->numBones;
    size_t outBytes = morph ? numElems * sizeof(float) : numElems * 28;
    if (outBytes < sizeof(_VECTOR))
        outBytes = sizeof(_VECTOR); // the root rescale touches three floats even of a morph output
    bool useCache = (seq->flags & ANIMSEQ_FLAG_UNPAK_CACHE) != 0 && seq->unpakCache != NULL;
    size_t cacheBytes = useCache ? seq->numElems * sizeof(AnimUnpakEntry) : 0;

    static uchar outBefore[96 * 28], outOrig[96 * 28], cacheBefore[256 * 8], cacheOrig[256 * 8];
    if (outBytes > sizeof(outBefore) || cacheBytes > sizeof(cacheBefore)) {
        AnimFrameCopy(out, seq, skin, frame);
        return;
    }
    sAnimSeq_tag seqBefore = *seq;
    memcpy(outBefore, out, outBytes);
    if (cacheBytes)
        memcpy(cacheBefore, seq->unpakCache, cacheBytes);

    {
        XbeOriginalScope original(ADDR_AnimFrameCopy);
        ((void(__cdecl *)(float *, sAnimSeq_tag *, sAnimSkin_tag *, float))ADDR_AnimFrameCopy)(out, seq, skin, frame);
    }
    sAnimSeq_tag seqOrig = *seq;
    memcpy(outOrig, out, outBytes);
    if (cacheBytes)
        memcpy(cacheOrig, seq->unpakCache, cacheBytes);

    *seq = seqBefore;
    memcpy(out, outBefore, outBytes);
    if (cacheBytes)
        memcpy(seq->unpakCache, cacheBefore, cacheBytes);
    AnimFrameCopy(out, seq, skin, frame);

    int firstDiff = -1;
    for (size_t i = 0; i < outBytes / 4 && firstDiff < 0; i++)
        if (((uint *)outOrig)[i] != ((uint *)out)[i])
            firstDiff = (int)i;
    float err = Shadow_MaxDiff((float *)outOrig, out, (int)(outBytes / 4));
    bool otherDiffers = memcmp(&seqOrig, seq, sizeof(sAnimSeq_tag)) != 0 ||
                        (cacheBytes && memcmp(cacheOrig, seq->unpakCache, cacheBytes) != 0);

    Shadow_CaptureFrame(seq, skin, frame, numElems, morph, out, outBytes);

    unsigned pathMask = (morph ? 2 : 1) | (useCache ? 4 : 0) | (skin == NULL ? 8 : 0);
    char detail[200];
    snprintf(detail, sizeof(detail), "seq %08x frame %g, %d elements, first differing float %d (%g vs %g)%s",
             seq->hashcode, frame, numElems, firstDiff, firstDiff >= 0 ? ((float *)outOrig)[firstDiff] : 0.0f,
             firstDiff >= 0 ? out[firstDiff] : 0.0f, otherDiffers ? ", sequence or cache state differs" : "");
    // Any differing bit is a failure here, however small the value difference (-0 against +0 included).
    Shadow_Report(&g_frameCopy, pathMask, firstDiff >= 0 ? (err > 1e-4f ? err : 1.0f) : 0.0f, otherDiffers,
                  detail);
}

// ---------------------------------------------------------------------------------------------------------------

void AnimShadow_Install(void) {
    if (!Settings_GetAnimShadowTest())
        return;

    printf("[animshadow] shadow tests on\n");

    static const struct { unsigned at; const void *wrapper; const char *name; } tests[] = {
        { ADDR_psiBuildMatrixPalette, (const void *)&Shadow_psiBuildMatrixPalette, "psiBuildMatrixPalette" },
        { ADDR_AnimGetBoneWorldTrans, (const void *)&Shadow_AnimGetBoneWorldTrans, "AnimGetBoneWorldTrans" },
        { ADDR_AnimFrameCopy, (const void *)&Shadow_AnimFrameCopy, "AnimFrameCopy" },
    };
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
        if (!XbeOriginal_Redirect(tests[i].at, tests[i].wrapper))
            printf("[animshadow] %s at 0x%08x is not patched - not tested\n", tests[i].name, tests[i].at);
}
