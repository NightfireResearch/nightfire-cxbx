#ifndef DRIVING_EAGL_ANIM_ANIMMISC_H_
#define DRIVING_EAGL_ANIM_ANIMMISC_H_

// The small never-built channel types (docs/driving/eagl.md 3.4): DeltaLerp, KeyLerp, phase, raw state, and the
// raw pose data behind type 0. Provisional ports, untested: see AnimMisc.cpp.

#include "FnAnim.h"

#include <bit>

enum PhaseFlags : uint8_t {          // PhaseChanData::flags
    kPhaseFlag01 = 0x01,             // MW: start with the right foot (no first-cycle offset)
    kPhaseLooping = 0x02,            // the phase channel wraps the time into [0, numFrames - 1]
    kPhaseStep1 = 0x08,              // a sample every frame
    kPhaseStep2 = 0x10,              // every 2 frames
    kPhaseStep4 = 0x20,              // every 4
    kPhaseStep8 = 0x40,              // every 8 (none of these: every frame)
};

// Degrees a phase sample step (the original's .rdata float at 0x001a120c)
constexpr float kPhaseScale = 360.0f / 255;
static_assert(std::bit_cast<uint32_t>(kPhaseScale) == 0x3fb4b4b5, "the original's 360/255");

// The phase data (FnPhaseChan's anim data, as GetPhaseChan answers it; MW's PhaseChan), thiscall on the data. The
// samples are u8 angles (360/255 degrees a step, -180), after the half-cycle lengths (at least two bytes of them).
struct PhaseChanData : AnimData {
    uint16_t numFrames;              // +0x04
    uint16_t startTime;              // +0x06
    uint8_t flags;                   // +0x08 PhaseFlags
    uint8_t numCycles;               // +0x09
    uint8_t cycles[2];               // +0x0a [max(numCycles, 2)] the half-cycle lengths in frames, then the samples

    const uint8_t* Samples() const { return cycles + (numCycles < 2 ? 2 : numCycles); }
    int SampleStep() const {         // frames a sample
        if (flags & kPhaseStep1)
            return 1;
        if (flags & kPhaseStep2)
            return 2;
        if (flags & kPhaseStep4)
            return 4;
        return (flags & kPhaseStep8) ? 8 : 1;
    }

    // phase = {target degrees, direction, frame limit (<= 0 none)}: the frame where the phase reaches the target
    // moving in the direction's sense, else the sample nearest it (AnimPoseBlend.cpp; invented names).
    bool FindMatchTime(const float *phase, float *time);         // 0x000fd0c0
    int SampleCount();                                           // 0x000fd6d0
};
static_assert(offsetof(PhaseChanData, cycles) == 0xa, "the phase data's cycles are at +0xa");

enum RawPoseSignature : uint32_t {   // a signature table entry in the data
    kSignatureQuat = 0,              // a quaternion (4 floats)
    kSignatureEuler = 1,             // Euler angles in degrees (3 floats)
    kSignatureTrans = 2,             // a translation (3 floats)
};

// The raw pose data (thiscall on the data). Two signature tables of 'count' dwords, each a run of groups (one per
// bone, 0x30 bytes of output from out + 0x10): a dword n then n channel functions. In the data the entries are
// RawPoseSignature numbers; InitAnimMemoryMap swaps them for the original addresses of the functions below (the
// first table copies a frame, the second interpolates two), and RestoreSignatures swaps them back. The frames
// follow the tables.
struct RawPoseChannel : AnimData {
    int32_t count;                   // +0x04 dwords in each signature table
    int32_t frameSize;               // +0x08 dwords a frame
    int32_t frames;                  // +0x0c
    uint32_t signatures[1];          // +0x10 [count], then lerps[count], then frames x frameSize floats

    uint32_t* Lerps() { return signatures + count; }
    float* Frame(int frame) { return reinterpret_cast<float *>(signatures + count * 2) + frameSize * frame; }
    void RestoreSignatures();                                    // 0x000fdb40 (invented name)
    void EvalFrame(int frame, float *out, void *mask);           // 0x000fdc20
    void Eval(float time, float *out, bool interpolate, void *mask);   // 0x000fdd40
    void Lerp(float t, int frame0, int frame1, float *out, void *mask);  // 0x000fe110 (invented name)
};
static_assert(offsetof(RawPoseChannel, signatures) == 0x10, "the raw pose header is 0x10 bytes");

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

// The raw state data behind FnRawStateChan (type 16), shared with AnimObjects.cpp's GetLength.
struct RawStateChanData : AnimData {
    uint16_t length;                 // +0x04 what FnRawStateChan::GetLength answers
    uint16_t frames;                 // +0x06
    uint8_t fields;                  // +0x08
    uint8_t frameSize;               // +0x09 bytes a frame
    uint16_t descriptors[1];         // +0x0a [fields], then the frames on a 4-byte boundary

    uint8_t* Frames() {
        int n = fields;
        uint8_t *self = reinterpret_cast<uint8_t *>(this);
        return (n & 1) ? self + n * 2 + 0xa : self + n * 2 + 0xc;
    }
};

#endif // DRIVING_EAGL_ANIM_ANIMMISC_H_
