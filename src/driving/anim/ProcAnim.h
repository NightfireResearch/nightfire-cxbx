#ifndef DRIVING_ANIM_PROCANIM_H_
#define DRIVING_ANIM_PROCANIM_H_

// ---------------------------------------------------------------------------------------------------------------
// The proc-anim functions: how an instance is placed when it is drawn. GetInstanceMatrix calls the one of the
// instance's procAnimType from the game's table of 256 (0x001c41c0), with the instance's proc-anim state:
//   0..99    QuaternionStreamAnimation: its animation (RAnimEngine::EvaluateInstance; the type is the animation)
//   100      none (Generic_FuncReturnsFalse: not drawn)          101      ProcAnimWidened
//   237      ProcAnimBillboard          238  ProcAnimSwingBillboard    239  ProcAnimParamRotateZ
//   240      ProcAnimParamRotateY       241  ProcAnimParamRotateX      242  ProcAnimSpinY2
//   243      QuaternionStreamAnimation  244  ProcAnimParamScale        245, 246, 255  ProcAnimStatic
//   247      ProcAnimSway               249  ProcAnimXFormCopy         250  ProcAnimByState
//   251      ProcAnimSpinZ              252  ProcAnimSpinY             253  ProcAnimSpinX
//   254      ProcAnimXFormTransform     the rest StubError
// The functions' names are ours (the PS2 build has them unnamed). A function's parameter is its state's (with a
// state) or the instance's procAnimIndex; the parameter and transform lists are set by RSkeletalObj::SetProcAnimState.
// See ProcAnim.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/Carp.h"                // CARP::Instance, MATRIX4
#include "../data/CoordConvert.h"        // Coord4

class RSceneObj;
struct ProcAnimState;                    // world/World.h

// A proc-anim function: the instance's matrix into `out`, moved by `offset` where it uses one; false: not drawn
typedef bool (*ProcAnimFunction)(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);

// The instance's matrix by its proc-anim function; `states` the instances' proc-anim states
bool GetInstanceMatrix(CARP::Instance *instance, const Coord4 *offset, MATRIX4 *out, ProcAnimState *states);   // 0x0008a4f0
// The transforms and parameters the proc-anim functions use; NULL for the game's single zero one
void SetProcAnimXFormList(MATRIX4 *xforms, uint32_t count);                                     // 0x0008a520
void SetProcAnimParamList(float *params, uint32_t count);                                       // 0x0008a550

// The proc-anim functions
// The instance's own matrix
bool ProcAnimStatic(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);       // 0x0008a580
// ... its first and third rows a little longer
bool ProcAnimWidened(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);      // 0x0008a5b0
// Its animation (RAnimEngine::EvaluateInstance)
bool QuaternionStreamAnimation(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);   // 0x0008a620
// Turned about x, y or z by the parameter (signed) in 256ths of a degree a tick
bool ProcAnimSpinX(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);        // 0x0008a640
bool ProcAnimSpinY(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);        // 0x0008a6d0
bool ProcAnimSpinZ(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);        // 0x0008a760
bool ProcAnimSpinY2(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);       // 0x0008b110
// Turned about x, y or z by the degrees of the parameter list's entry the parameter names
bool ProcAnimParamRotateX(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out); // 0x0008a7f0
bool ProcAnimParamRotateY(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out); // 0x0008a890
bool ProcAnimParamRotateZ(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out); // 0x0008a930
// Transformed by, or replaced by, the transform list's entry the instance's procAnimIndex names (from 1)
bool ProcAnimXFormTransform(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);   // 0x0008a9d0
bool ProcAnimXFormCopy(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);    // 0x0008aa20
// Facing away from the camera, at the instance's position
bool ProcAnimBillboard(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);    // 0x0008aa50
// ... upright, swinging about its forward axis over time
bool ProcAnimSwingBillboard(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);  // 0x0008ad40
// Tilted and raised by two waves over time
bool ProcAnimSway(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);         // 0x0008af60
// By the state's own proc-anim type, when the state has no scene object
bool ProcAnimByState(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);      // 0x0008b1a0
// Scaled in y by how near its x is to the first parameter, then transformed by the first transform
bool ProcAnimParamScale(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out);   // 0x0008b1c0

#endif // DRIVING_ANIM_PROCANIM_H_
