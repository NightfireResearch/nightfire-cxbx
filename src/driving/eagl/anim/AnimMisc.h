#ifndef DRIVING_EAGL_ANIM_ANIMMISC_H_
#define DRIVING_EAGL_ANIM_ANIMMISC_H_

// The small never-built channel types (docs/driving/eagl.md 3.4): DeltaLerp, KeyLerp, phase, raw state, and the
// raw pose data behind type 0. Provisional ports, untested: see AnimMisc.cpp.

#include "FnAnim.h"

// The phase data (thiscall on the data): u16 type, u16 checksum, u16 samples at +4, u16 at +6, u8 flags at +8
// (1, 2 looping, 8/0x10/0x20/0x40 one sample per 1/2/4/8 frames), u8 at +9, then u8 samples (360/255 degrees a
// step, -180) from +0xa + max(byte 9, 2).
// Its sample count (0x000fd6d0) is PhaseMatchData::SampleCount, AnimPoseBlend.cpp.

// The raw pose data (thiscall on the data). Two signature tables of 'count' dwords, each a run of groups (one per
// bone, 0x30 bytes of output from out + 0x10): a dword n then n channel functions. In the data the entries are
// type numbers (0 quaternion, 1 Euler angles in degrees, 2 translation); InitAnimMemoryMap swaps them for the
// original addresses of the functions below (the first table copies a frame, the second interpolates two), and
// RestoreSignatures swaps them back. The frames follow the tables.
struct RawPoseChannel {
    uint16_t type;                   // +0x00
    uint16_t checksum;               // +0x02
    int32_t count;                   // +0x04 dwords in each signature table
    int32_t frameSize;               // +0x08 dwords a frame
    int32_t frames;                  // +0x0c
    // +0x10 uint32_t signatures[count], lerps[count], then frames x frameSize floats

    void RestoreSignatures();                                    // 0x000fdb40 (invented name)
    void EvalFrame(int frame, float *out, void *mask);           // 0x000fdc20
    void Eval(float time, float *out, bool interpolate, void *mask);   // 0x000fdd40
    void Lerp(float t, int frame0, int frame1, float *out, void *mask);  // 0x000fe110 (invented name)
};
static_assert(sizeof(RawPoseChannel) == 0x10, "the raw pose header is 0x10 bytes");

void RawPoseChannel_InitAnimMemoryMap(uint8_t *data);            // 0x000fda70 (cdecl on the data)
void EAGLAnim_EulerToQuat(const float *angles, float *quat);     // 0x000fde00 (invented name)
void EAGLAnim_EulF3(float **cursor, float *out);                 // 0x000fdec0
void RawPose_CopyQuat(float **cursor, float *out);               // 0x000fdf20 (invented name)
void RawPose_CopyTrans(float **cursor, float *out);              // 0x000fdf60 (invented name)
void RawPose_LerpEuler(float t, float **cursor0, float **cursor1, float *out);   // 0x000fdf90 (invented name)
void RawPose_LerpQuat(float t, float **cursor0, float **cursor1, float *out);    // 0x000fdfd0 (invented name)
void RawPose_LerpTrans(float t, float **cursor0, float **cursor1, float *out);   // 0x000fe060 (invented name)

int AnimTruncateRawState(float value);                           // 0x000fda60 (invented name, unreferenced)
int AnimTruncateRawPose(float value);                            // 0x000fe270 (invented name, unreferenced)

#endif // DRIVING_EAGL_ANIM_ANIMMISC_H_
