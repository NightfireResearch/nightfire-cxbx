#ifndef DRIVING_RENDER_REFLECTION_H_
#define DRIVING_RENDER_REFLECTION_H_

// ---------------------------------------------------------------------------------------------------------------
// The renderer's feature switches (FeatureManager) and the car reflections (RReflection, one instance at
// 0x001f2dfc): each frame the world around the eye is drawn twice into 256x256 offscreen buffers, once facing
// forwards and once backwards, and the two are folded into a sphere map the car materials read
// ("GAME::ReflectionMap"). RReflection also keeps the four scene objects nearest the eye that are drawn into the
// reflection, and the per-car lighting records the car render methods read ("GAME::CarLighting*Props"), tuned from
// "Render:CarRender". See Reflection.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../camera/Camera.h"           // RViewCamera, RCamera, Coord3, Coord4, MATRIX4
#include "../eagl/GeoPrimState.h"
#include "OffscreenBuffer.h"

namespace EAGL {
struct TAR;
struct ViewPort;
}
class RSceneObj;

// ---- FeatureManager

// The feature manager's statics (0x001f2d78..0x001f2dfc), one record. Init takes the screen's format; the rest
// are the values SetUserSpecifiedRenderFeatures sets, few of them read anywhere.
struct FeatureManagerData {
    int32_t resolutionStamp;            // +0x00 Init's fourth argument (Camera.cpp's ResolutionStamp)
    int32_t width;                      // +0x04 the screen's size in pixels
    int32_t height;                     // +0x08
    int32_t depth;                      // +0x0c bits per pixel
    uint8_t unknown10[0xa];
    uint8_t initialised;                // +0x1a set by Init
    uint8_t unknown1B[0x10];
    uint8_t texelsAreOffset[2];         // +0x2b cleared by SetTexelsAreOffset
    uint8_t unknown2D[0x13];
    uint8_t unknown40;                  // +0x40 0
    uint8_t unknown41;                  // +0x41 0
    uint8_t unknown42;                  // +0x42 0
    uint8_t unknown43;                  // +0x43 1
    int32_t unknown44;                  // +0x44 2
    uint8_t unknown48[4];               // +0x48 1, 1, 1, 1
    float unknown4C;                    // +0x4c 400
    int32_t unknown50;                  // +0x50 copied from 0x001c4820
    float unknown54;                    // +0x54 400
    int32_t unknown58;                  // +0x58 8
    int32_t unknown5C;                  // +0x5c 1
    int32_t unknown60;                  // +0x60 0x100; RParticleSystem reads it
    uint8_t unknown64;                  // +0x64 0
    uint8_t unknown65;                  // +0x65 0
    uint8_t unknown66;                  // +0x66 1
    uint8_t unknown67;                  // +0x67 1
    uint8_t unknown68[4];
    int32_t unknown6C;                  // +0x6c 7
    int32_t unknown70;                  // +0x70 300
    int32_t unknown74;                  // +0x74 3
    int32_t unknown78;                  // +0x78 0
    int32_t unknown7C;                  // +0x7c 1
    uint8_t locked;                     // +0x80 set: SetUserSpecifiedRenderFeatures changes nothing (name ours)
    uint8_t unknown81[3];
};
static_assert(sizeof(FeatureManagerData) == 0x84, "the feature manager's statics end at the reflection pointer");
static_assert(offsetof(FeatureManagerData, initialised) == 0x1a &&
              offsetof(FeatureManagerData, texelsAreOffset) == 0x2b &&
              offsetof(FeatureManagerData, unknown40) == 0x40 && offsetof(FeatureManagerData, unknown60) == 0x60 &&
              offsetof(FeatureManagerData, unknown6C) == 0x6c && offsetof(FeatureManagerData, locked) == 0x80,
              "FeatureManagerData layout");

class FeatureManager {
public:
    // Clears both flags; the arguments are not read
    static void SetTexelsAreOffset(bool a, bool b);                                     // 0x000980e0
    // The feature settings, unless locked
    static void SetUserSpecifiedRenderFeatures();                                       // 0x000980f0
    // The screen's format, the mouse's bounds, then SetUserSpecifiedRenderFeatures
    static void Init(int width, int height, int depth, int resolutionStamp);            // 0x000981c0
};

// ---- RReflection

// A material's lighting, as the tuning names its parts ("[Paint]Ambient" ...)
struct ReflMaterial {
    float ambient;                      // +0x00
    float diffuse;                      // +0x04
    float specular;                     // +0x08
    float reflect;                      // +0x0c
};

// LightingProps's kinds (RCARPFile's names: "GAME::CarLightingGlassProps" ... - the tuning calls kind 1 "SpecBump"
// and kind 2 "Paint")
enum ReflLightingKind {
    kReflGlass = 0,
    kReflSpecular = 1,
    kReflGlossy = 2,
    kReflDull = 3,
    kReflChrome = 4,
    kReflDash = 5,
};

// One lighting record: one per car name, then the characters', then the dynamic objects'
struct ReflLighting {
    ReflMaterial materials[6];          // +0x00 by ReflLightingKind
    float fresnel;                      // +0x60 "Fresnel"; GetReflectionData2 answers this address
    float reflectionWarp;               // +0x64 "Reflection Warp"
    float specularSpot;                 // +0x68 "SpecularSpot"
    float specularStrength;             // +0x6c SetReflectiveSpecularStrength's
    float spherifyNormals;              // +0x70 "Spherify Normals"
    float unknown74[3];
};
static_assert(sizeof(ReflLighting) == 0x80, "a lighting record is 0x80 bytes");

class RReflection : public RViewCamera {
public:
    // The reflection's own data (Ghidra: RReflection::ReflPrivateData, 0x280 bytes, UMemory::FastAlloc'd)
    struct ReflPrivateData {
        uint8_t skyBright;              // +0x00 "Sky bright": the world's brightness in the reflection, 0..255
        uint8_t unknown01;
        uint8_t unknown02;              // +0x02 0x18
        uint8_t unknown03;
        uint32_t unknown04;             // +0x04 0x78787878
        uint8_t unknown08;              // +0x08 1
        uint8_t unknown09;              // +0x09 1
        uint8_t unknown0A[2];
        float eyeHeight;                // +0x0c added to the eye's height (SetupView)
        float unknown10;                // +0x10 120
        uint8_t unknown14[0x8c];
        ReflLighting *lighting;         // +0xa0 lightingCount records (operator new[])
        uint8_t unknownA4[0xc];
        ROffscreenBuffer buffers[3];    // +0xb0 forwards, backwards, and the sphere map they are folded into
        uint8_t unknown110[4];
        EAGL::ViewPort *savedViewPort;  // +0x114 the view's own viewport, put back by the destructor
        EAGL::TAR *crefTexture;         // +0x118 'cref'
        EAGL::TAR *wrefTexture;         // +0x11c 'wref': TextureWeaponEnvMap
        EAGL::TAR *rmskTexture;         // +0x120 'rmsk'
        EAGL::TAR *carSTexture;         // +0x124 'carS': TextureSpecular
        uint8_t unknown128[8];
        Coord4 viewDirection;           // +0x130 the view's row 2 (SetupView): DrawWorldAtPoint's facing
        MATRIX4 reflectionMatrix;       // +0x140 the view's inverse frame (GetReflectionMatrix)
        Coord3 carPosition;             // +0x180 SetReflectivity's object (GetReflectionCarPos)
        float spherifyNormals;          // +0x18c that object's record's
        EAGL::GeoPrimState states[2];   // +0x190
        uint8_t unknown228[4];
        // The scene objects drawn into the reflection, at most four: their nearness ranks (0 nearest, 3 for the
        // free slot or the farthest), distances and objects
        int8_t ranks[4];                // +0x22c
        float distances[4];             // +0x230
        RSceneObj *objects[4];          // +0x240
        int32_t charactersIndex;        // +0x250 the record after the cars'
        int32_t dynamicObjectsIndex;    // +0x254
        int32_t lightingCount;          // +0x258
        const char **lightingNames;     // +0x25c the records' names (the tuning's index names)
        uint8_t unknown260;             // +0x260 1
        uint8_t unknown261[0xf];
        Coord4 warpage;                 // +0x270 EnableReflectionMapWarpage's ("GAME::FishEyeParams")

        ReflPrivateData* Construct();                                                   // 0x00098b60
        void Destruct();                                                                // 0x000982a0
    };

    ReflPrivateData *privateData;       // +0x4c
    float farthestDistance;             // +0x50 (Ghidra: nearestSubmitDistance) the largest of the four distances

    RReflection* Construct();                                                           // 0x000994c0
    void Destruct();                                                                    // 0x00099590
    RReflection* Delete(unsigned flags);    // the scalar deleting destructor, vtable slot 0  // 0x00099cc0
    static void Init();                     // makes the instance                        // 0x00099c50
    static void Kill();                     // deletes it                                // 0x00098340
    // The textures the car materials blend with: 'cref' and 'wref', from data\render\ext.xsh if need be
    void InitPostSim();                                                                 // 0x000993e0

    // ---- what the car render methods read
    Coord4* GetReflectionMapWarpageData();                                              // 0x00098210
    // The sphere map's warp: (0x001c482c, 0x001c4830, 0, 0x001c4830 + 1) on, (1, 0, 0, 1) off
    void EnableReflectionMapWarpage(bool enable);                                       // 0x00098220
    void SetReflectiveSpecularStrength(int index, float strength);                      // 0x00098360
    // A lighting record's material: `name` a car's, "Character" or "Dynamic Objects"
    ReflMaterial* LightingProps(int kind, const char *name);                            // 0x00098380
    float* GetReflectionData2(const char *name);                                        // 0x00098410
    Coord3* GetReflectionCarPos();                                                      // 0x00098470
    EAGL::TAR* TextureWeaponEnvMap();                                                   // 0x00098480
    EAGL::TAR* TextureSpecular();                                                       // 0x00098490
    EAGL::TAR* Texture();               // the sphere map                               // 0x000984a0
    MATRIX4* GetReflectionMatrix();                                                     // 0x000984b0
    // The object's car position and its record's spherify value
    void SetReflectivity(RSceneObj *object);                                            // 0x00099610

    // ---- the scene objects drawn into the reflection
    // Takes the object into the nearest four (a no-op when it is already there)
    void PrivateSubmitSceneObj(float distance, RSceneObj *object);                      // 0x00098520
    void DeregisterSceneObj(RSceneObj *object);                                         // 0x000986a0
    // Draws the four through their RenderSimple (vtable slot 3), vehicles allowed
    void SceneObjRender(EAGL::ViewPort *unused);                                        // 0x00098850
    // Empties the four and submits them again at their present distances
    void ResetSceneObjDistances();                                                      // 0x000988a0

    // ---- drawing the maps
    // Into the buffer: its viewport, the renderer's current view and camera position
    void Begin(ROffscreenBuffer *buffer);                                            // 0x000984c0
    // The view's frame from `frame` at the eye, its row 2 and row 0 times `side` (FUN_00099670; name ours)
    void SetupView(const Coord3 *eye, const MATRIX4 *frame, float side);                // 0x00099670
    // The world at the eye and the four objects, fog pushed out, glares off (FUN_000997d0; name ours)
    void RenderView(float side, const MATRIX4 *frame, const Coord3 *eye);               // 0x000997d0
    // Both hemispheres from the first camera view's frame, then the sphere map (FUN_00099ce0; name ours)
    void UpdateMaps(const Coord3 *eye);                                                 // 0x00099ce0
};
static_assert(sizeof(RReflection) == 0x54, "RReflection is 0x54 bytes");
static_assert(sizeof(RReflection::ReflPrivateData) == 0x280, "ReflPrivateData is 0x280 bytes");
static_assert(offsetof(RReflection::ReflPrivateData, lighting) == 0xa0 &&
              offsetof(RReflection::ReflPrivateData, buffers) == 0xb0 &&
              offsetof(RReflection::ReflPrivateData, savedViewPort) == 0x114 &&
              offsetof(RReflection::ReflPrivateData, carSTexture) == 0x124 &&
              offsetof(RReflection::ReflPrivateData, viewDirection) == 0x130 &&
              offsetof(RReflection::ReflPrivateData, reflectionMatrix) == 0x140 &&
              offsetof(RReflection::ReflPrivateData, carPosition) == 0x180 &&
              offsetof(RReflection::ReflPrivateData, states) == 0x190 &&
              offsetof(RReflection::ReflPrivateData, ranks) == 0x22c &&
              offsetof(RReflection::ReflPrivateData, objects) == 0x240 &&
              offsetof(RReflection::ReflPrivateData, lightingNames) == 0x25c &&
              offsetof(RReflection::ReflPrivateData, warpage) == 0x270, "ReflPrivateData layout");

// The two hemisphere textures UpdateMaps folds into the sphere map (a stack pair in the original; name ours)
struct ReflMapPair {
    EAGL::TAR *hemispheres[2];          // forwards, backwards

    // Strips of the unit sphere, each hemisphere's texture on its half (FUN_00099990)
    void DrawSphereMap();                                                               // 0x00099990
};

// The sphere map's coordinates of a direction: (x, y) / 2|d + (0, 0, 1)| + 0.5, with z and w 1 (FUN_00098980;
// names ours)
void ReflSphereMapCoords(const Coord4 *direction, Coord4 *out);                         // 0x00098980
// A hemisphere texture's coordinates of a direction: (x, y) / (1 - z) where z <= 0 (front set), else
// (-x, y) / (1 + z), brought into 0..1; z and w 1 (FUN_00098a00)
void ReflHemisphereCoords(const Coord4 *direction, Coord4 *out, bool *front);           // 0x00098a00
// A vertex of the sphere map's strips: the direction (sin(turns) * height, cos(turns) * height, (flip ? -1 : 1) -
// height) to both sets of coordinates (FUN_00098ab0)
void ReflMapVertex(float height, float turns, bool flip, Coord4 *position, Coord4 *uv);  // 0x00098ab0

#define TheReflection (*(RReflection **)0x001f2dfc)

#endif // DRIVING_RENDER_REFLECTION_H_
