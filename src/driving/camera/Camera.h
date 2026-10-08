#ifndef DRIVING_CAMERA_CAMERA_H_
#define DRIVING_CAMERA_CAMERA_H_

// ---------------------------------------------------------------------------------------------------------------
// The engine's camera classes, the base of every camera and view:
//   RCamera (0xc0 bytes, vtable 0x00190350): a frame (row vectors, the position in row 3), its inverse, made when
//     asked for after a change, and a field of view.
//   RViewCamera (0x4c bytes, vtable 0x00192444): a view through a camera - an EAGL viewport, its extents in the
//     screen as fractions, its near and far planes, its level-of-detail multiplier - and how it is set up and
//     drawn (Render). RRenderWorldCamera and RPlayerViewCamera (WorldCamera.h) derive from it.
//   RWorldCamera (0x130 bytes, vtable 0x001924e4): an RCamera that follows an anchor (a physics object), plays
//     camera animations and receives camera input. RPlayerCamera (PlayerCamera.h) derives from it.
// RCamera's and RViewCamera's methods are ported in Camera.cpp, RWorldCamera's in WorldCamera.cpp.
//
// The vtables stay the game's: the classes declare no `virtual`, and calls to virtual methods go through the
// object's vtable (the *Virtual helpers).
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../world/CollisionTypes.h"    // MATRIX4, Coord3, Coord4

namespace EAGL {
struct RenderContext;
struct ViewPort;
}
class ActionQueue;
struct Handle;                          // RAnimEngine::Handle
struct IniFiles;
struct PhysicsObject;
struct RPathHandle;

namespace CARP {
class Instance;
}

// ---- RCamera

class RCamera {
public:
    void **vtable;                      // +0x00
    uint8_t unknown04[0xc];
    MATRIX4 matrix;                     // +0x10 the camera's frame; row 3 its position
    Coord4 unknown50;                   // +0x50 (0, 0, 0, 1) at construction; SetViewingTransform's last argument
    uint8_t matrixChanged;              // +0x60 the inverse is out of date
    uint8_t unknown61[0xf];
    MATRIX4 inverse;                    // +0x70 the world into the camera's frame (CreateMatrix4Inv)
    uint8_t active;                     // +0xb0 SetActive's
    uint8_t unknownB1[3];
    float fieldOfView;                  // +0xb4 in degrees
    uint8_t unknownB8[8];

    RCamera* Construct();                                                                       // 0x00078430
    // The copy constructor (Ghidra: FUN_00096980): every field but the unknown ones
    RCamera* ConstructCopy(const RCamera *other);                                               // 0x00096980
    void Destruct();                                                                            // 0x00096820
    RCamera* Delete(unsigned flags);    // the scalar deleting destructor, vtable slot 0          // 0x00078470
    void CreateMatrix4Inv();            // the inverse, if the frame changed                    // 0x000784a0
    void ConvertToRHCS();               // negates the frame's row 2                            // 0x00078500
    void SetMatrix4(const MATRIX4 *frame);                                                      // 0x00078520
    void SetActive(bool active);        // vtable slot 1                                        // 0x00078540
};
static_assert(sizeof(RCamera) == 0xc0, "RCamera is 192 bytes");
static_assert(offsetof(RCamera, matrix) == 0x10 && offsetof(RCamera, unknown50) == 0x50 &&
              offsetof(RCamera, matrixChanged) == 0x60 && offsetof(RCamera, inverse) == 0x70 &&
              offsetof(RCamera, active) == 0xb0 && offsetof(RCamera, fieldOfView) == 0xb4, "RCamera layout");

// ---- RViewCamera

class RViewCamera {
public:
    // How SetRenderCamera sets the view up
    enum TransformMode : int32_t {
        kDeviceTransform = 0,           // SetDeviceTransformMode
        kTransformMode1 = 1,            // SetDeviceTransformMode as well
        kWorldTransform = 2,            // SetWorldTransformMode; the constructor's
    };

    void **vtable;                      // +0x00
    RCamera *camera;                    // +0x04
    int32_t transformMode;              // +0x08 TransformMode
    int32_t resolutionStamp;            // +0x0c the screen's resolution stamp the shape was last made for
    int32_t viewId;                     // +0x10 -1 at construction; 0: the first view (Missile::Render's flag)
    int32_t player;                     // +0x14
    EAGL::ViewPort *viewPort;           // +0x18
    // The extents: the view's corners as fractions of the screen, and its planes' distances
    float xMin;                         // +0x1c
    float yMin;                         // +0x20
    float nearZ;                        // +0x24
    float xMax;                         // +0x28
    float yMax;                         // +0x2c
    float farZ;                         // +0x30
    float lodMultiplier;                // +0x34
    uint32_t fillColour;                // +0x38 0: the viewport is not cleared
    uint8_t active;                     // +0x3c between SetRenderCamera and EndView
    uint8_t unknown3D[3];
    uint32_t zBufferRangeA;             // +0x40 SetZBufferRange's
    uint32_t zBufferRangeB;             // +0x44
    float guardBandSize;                // +0x48 SetGuardBandSize's; not set by the constructor

    // A new camera when `camera` is NULL
    RViewCamera* Construct(RCamera *camera);                                                    // 0x00096ae0
    void Destruct();                                                                            // 0x00096830
    void DestructThunk();               // a second entry to the destructor (a jump to it)       // 0x0008bf20
    RViewCamera* Delete(unsigned flags);    // the scalar deleting destructor, vtable slot 0      // 0x00096c60

    void SetGuardBandSize(float size);                                                          // 0x00096850
    void UpdateForResolution();         // the viewport's shape from the extents                 // 0x00096860
    void RefreshLODMultiplier();                                                                // 0x000968b0
    void EndView();                                                                             // 0x000968e0
    void SetZBufferRange(uint32_t a, uint32_t b);                                               // 0x00096900
    // The aspect ratio, answered unrounded
    double AspectRatio();                                                                       // 0x00096960
    static void SetViewPortToUnitTransformMode(EAGL::ViewPort *viewPort, float nearZ, float farZ);   // 0x000969f0
    void SetDeviceTransformMode();                                                              // 0x00096a70
    void SetFillColour(uint32_t colour);                                                        // 0x00096ca0
    void SetExtents(float x, float y, float width, float height);                               // 0x00096cc0
    void SetExtents(const RViewCamera *other);                                                  // 0x00096d70
    void SetWorldTransformMode();                                                               // 0x00096eb0
    void SetRenderCamera();                                                                     // 0x00096fc0
    // PreRender, SetRenderCamera, DoRender, EndView, PostRender
    void Render();                                                                              // 0x00097060

    // ---- calls through the vtable (slot 1 is a no-op in every view class)
    void PreRenderVirtual() { CallSlot(2); }
    void PostRenderVirtual() { CallSlot(3); }       // (the name is ours)
    void DoRenderVirtual() { CallSlot(4); }
    void ConfigureViewVirtual() { CallSlot(5); }

private:
    void CallSlot(int slot) {
        typedef void (RViewCamera::*Method)();
        (this->*XbeVirtual<Method>(this, slot))();
    }
};
static_assert(sizeof(RViewCamera) == 0x4c, "RViewCamera is 76 bytes");
static_assert(offsetof(RViewCamera, viewPort) == 0x18 && offsetof(RViewCamera, xMin) == 0x1c &&
              offsetof(RViewCamera, farZ) == 0x30 && offsetof(RViewCamera, active) == 0x3c &&
              offsetof(RViewCamera, guardBandSize) == 0x48, "RViewCamera layout");

// FUN_00096e20: the field of view and aspect ratio swayed by the step count, when the player's vehicle asks for it
void ApplyPerspectiveFunction(float *aspect, float *fieldOfView);                               // 0x00096e20

// ---- what the world cameras read of other systems (not ported; only these fields, the names ours)

// The AI's spline path (AISplinePath): the path it follows and where the spline is placed
class AISplinePath {
public:
    uint8_t unknown00[0x60];
    RPathHandle *path;                  // +0x60
    uint8_t unknown64[0xc];
    MATRIX4 placement;                  // +0x70
};
static_assert(offsetof(AISplinePath, path) == 0x60 && offsetof(AISplinePath, placement) == 0x70,
              "AI spline path layout");

// ---- RWorldCamera

// An anchor's position or velocity (or three floats) read as a VU0 vector: with the word after it, as the
// original's 16-byte loads do (the bodies keep these vectors 16 bytes apart)
inline const Coord4 *AsVector4(const Coord3 *v) {
    return reinterpret_cast<const Coord4 *>(v);
}
inline const Coord4 *AsVector4(const float *v) {
    return reinterpret_cast<const Coord4 *>(v);
}

// ... and a vector's x, y and z as a Coord3
inline Coord3 *AsCoord3(Coord4 *v) {
    return reinterpret_cast<Coord3 *>(v);
}
inline const Coord3 *AsCoord3(const Coord4 *v) {
    return reinterpret_cast<const Coord3 *>(v);
}

// An anchor's offset and its slide, as the camera tuning file gives them (ReadAnchorInfo; the name is ours)
struct CameraAnchorInfo {
    Coord3 offset;                      // Anchor_X, Anchor_Y, Anchor_Z
    float slideDist;                    // Anchor_Slide_Dist
    float slideRate;                    // Anchor_Slide_Rate
    float slideRecoveryRate;            // Anchor_Slide_Rec_Rate
};
static_assert(sizeof(CameraAnchorInfo) == 0x18, "an anchor's info is six floats");

class RWorldCamera : public RCamera {
public:
    enum ModeChangeFlag : uint32_t {
        kAnchorChanged = 0x1,           // set by SetAnchor and RestartCamera
        kLookBackChanged = 0x2,         // RPlayerCamera::SetCameraLookBack, PauseOff
        kForceSmoothChange = 0x4,       // RPlayerCamera::DoSmoothModeChange answers true
        kNoSmoothChange = 0x8,          // ... false
    };

    Coord4 eye;                         // +0xc0 where the camera is (SetViewingTransform's position)
    Coord4 lookAt;                      // +0xd0 what it looks at (AnchorCamera's)
    Coord4 lookAtOffset;                // +0xe0 lookAt from the anchor, in the world's axes
    uint32_t modeChangeFlags;           // +0xf0 ModeChangeFlag
    float zoomFov;                      // +0xf4 33 at a restart
    float zoomFovTarget;                // +0xf8 33 at a restart
    uint32_t unknownFC;
    Coord4 unknown100;                  // +0x100 (0, 0, 0, 1) at a restart
    uint32_t animSystemId;              // +0x110 the animation system playing
    Handle *animHandle;                 // +0x114 the camera animation
    CARP::Instance *cameraAnims;        // +0x118 the anchor's 'Cams' instances
    uint32_t cameraAnimCount;           // +0x11c
    AISplinePath *aiSplinePath;         // +0x120
    ActionQueue *inputQueue;            // +0x124 StartCameraInputReceiver's
    PhysicsObject *anchor;              // +0x128
    uint32_t unknown12C;

    RWorldCamera* Construct();                                                                  // 0x00098070
    void Destruct();                                                                            // 0x000979f0
    RWorldCamera* Delete(unsigned flags);   // the scalar deleting destructor, vtable slot 0      // 0x00097eb0

    // The frame from eye to lookAt with `up`; `velocity` (if not NULL) into unknown50, four words copied (an
    // anchor's Coord3 velocity and the word after it, or a Coord4)
    void SetViewingTransform(const Coord4 *up, const void *velocity, int unused);               // 0x00097100
    // ... with lookAt moved by offset[1] along `up` and offset[0] along the frame's old row 0
    void SetViewingTransform(const Coord4 *up, const float *offset, const void *velocity, int unused);   // 0x00097190

    // The anchor's motion. A NULL or invalid anchor (one whose body is not a valid rigid or simple body) answers
    // zero, the identity, or a zero vector; an invalid anchor is dropped.
    float GetAnchorSpeed();             // in the ground plane                                    // 0x00097240
    void GetAnchorAcceleration(Coord4 *acceleration);   // in the anchor's x and z axes          // 0x000972c0
    MATRIX4* GetAnchorMatrix4();                                                                // 0x000973c0
    Coord3* GetAnchorPosition();                                                                // 0x00097470
    int GetAnchorResetAvailable();                                                              // 0x000974e0
    // Takes the anchor's 'Cams' instances from its render object's group
    void SetAnchor(PhysicsObject *anchor);                                                      // 0x00097530
    double GetAnchorRenderOffset();     // answered unrounded                                   // 0x000975d0
    Coord3* GetAnchorLinearVelocity();                                                          // 0x00097fc0

    void StartCameraInputReceiver();                                                            // 0x00097620
    // Hands every queued action to CameraInputCallback; vtable slot 5
    void ReceiveCameraInput();                                                                  // 0x00097690
    // Moves the field of view towards `target` by at most `step`, keeping it above 2 degrees
    void SetCameraZoom(float target, float step);                                               // 0x00097710

    bool LoadSingleAnimation(CARP::Instance *instance);                                         // 0x00097770
    bool LoadAISplinePathAnimation(void *path);                                                 // 0x000977d0
    // The 'Cams' instance whose animation has the id
    bool LoadSingleAnimationFromList(uint32_t id);                                              // 0x00097870
    bool PlayCurrentAnimation();        // whether it is still playing                          // 0x00097930
    // The frame at the animation's current time into `frame`, the eye and the zoom; `flags` 1 mirrors it, 2
    // picks the other axis swap. Whether it is playing.
    bool UpdateAnimationCam(MATRIX4 *frame, uint8_t flags);                                     // 0x00097aa0
    static void ReadAnchorInfo(IniFiles *ini, const char *section, CameraAnchorInfo *info);    // 0x00097970

    // lookAt: the anchor's position plus `offset` turned into the anchor's axes, eased towards when `smooth`
    void AnchorCamera(bool smooth, const Coord3 *offset);                                       // 0x00097c60
    // ... with `offset`'s y less the anchor's render offset, and no x
    void AnchorRelativeCamera(bool smooth, const Coord3 *offset);                               // 0x00097d70
    // vtable slot 4
    void RestartCamera();                                                                       // 0x00097ee0

    // ---- calls through the vtable
    // Slot 6: an action from the input queue (RWorldCamera's ignores it; RPlayerCamera::CameraInputCallback)
    void CameraInputCallbackVirtual(int32_t action, float value) {
        typedef void (RWorldCamera::*Method)(int32_t action, float value);
        (this->*XbeVirtual<Method>(this, 6))(action, value);
    }

private:
    // The anchor if its body is valid, else NULL, dropping it (inlined in the game; the name is ours)
    PhysicsObject* ValidAnchor();
};
static_assert(sizeof(RWorldCamera) == 0x130, "RWorldCamera is 304 bytes");
static_assert(offsetof(RWorldCamera, eye) == 0xc0 && offsetof(RWorldCamera, modeChangeFlags) == 0xf0 &&
              offsetof(RWorldCamera, unknown100) == 0x100 && offsetof(RWorldCamera, animSystemId) == 0x110 &&
              offsetof(RWorldCamera, inputQueue) == 0x124 && offsetof(RWorldCamera, anchor) == 0x128,
              "RWorldCamera layout");

// ---- the renderer as the cameras use it (RRenderer is not ported; only these fields)

struct CameraRendererFields {
    uint32_t unknown00;
    RViewCamera *currentView;           // +0x04 SetRenderCamera's view
    uint8_t unknown08[0x18];
    uint32_t unknown20;                 // +0x20 cleared by RPlayerViewCamera::ConfigureView
    uint8_t unknown24[0xc];
    Coord4 cameraPosition;              // +0x30 the view's camera's, w 1
    int32_t screenWidth;                // +0x40
    int32_t screenHeight;               // +0x44
    uint8_t unknown48[4];
    uint8_t widescreen;                 // +0x4c
    uint8_t unknown4D[0xb];
    float fieldOfViewScale;             // +0x58
    uint8_t unknown5C[8];
    EAGL::RenderContext *renderContext; // +0x64
};
static_assert(offsetof(CameraRendererFields, currentView) == 0x04 &&
              offsetof(CameraRendererFields, cameraPosition) == 0x30 &&
              offsetof(CameraRendererFields, screenWidth) == 0x40 &&
              offsetof(CameraRendererFields, widescreen) == 0x4c &&
              offsetof(CameraRendererFields, fieldOfViewScale) == 0x58 &&
              offsetof(CameraRendererFields, renderContext) == 0x64, "RRenderer offsets");

#endif // DRIVING_CAMERA_CAMERA_H_
