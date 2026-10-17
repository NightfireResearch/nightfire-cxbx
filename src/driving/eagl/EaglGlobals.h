#ifndef DRIVING_EAGL_EAGLGLOBALS_H_
#define DRIVING_EAGL_EAGLGLOBALS_H_

// EAGL's globals that more than one file of the port uses, each named once (docs/driving/eagl.md). D3D8's own tables
// and EAGL's shadows of what it sent are D3D8State.h's. The types a macro names (SymbolPool, ShapeFile...) come from
// the header of the file that defines them, included where the macro is used. The ones that are ours are defined
// in the file that writes them, with the original address in their XBE_GLOBAL tag.

#include <stdint.h>

#include "../../helpers.h"

struct BuiltInShapeFile;

namespace EAGL {
struct Device;
struct RenderContext;
struct RenderMethod;
struct TextureRenderContext;
}

// ---- the allocator hooks: Device::SetNewOverride / SetDeleteOverride point them at the game's (GameCallbacks.cpp)

typedef void *(*EaglMallocFn)(uint32_t size, const char *name);
typedef void (*EaglFreeFn)(void *pointer, uint32_t size);
extern EaglMallocFn EaglMalloc;                             // eagl_alloc until then (View.cpp)
extern EaglFreeFn EaglFree;                                 // eagl_free

// ---- the device and the current contexts (View.cpp, RenderContext.cpp, Model.cpp)

extern EAGL::Device *CurrentDevice;
extern EAGL::RenderContext *CurrentRenderContext;
extern EAGL::TextureRenderContext *CurrentTextureRenderContext;
extern void *D3DDevicePointer;                              // D3D8's device, written by Direct3D_CreateDevice
extern int32_t CurrentVariation;                            // EAGLInternal::CurrentVariation

// The render methods, linked through next: the constructed ones (the static initialisers'), and the children waiting
// for their parent's shaders (RenderMethod.cpp).
extern EAGL::RenderMethod *ConstructedMethods;
extern EAGL::RenderMethod *WaitingMethods;

// ---- the matrices render methods read by name (gpViewMatrix ...), written by View.cpp and registered
// by EAGL_SymbolInit (Loader.cpp)

alignas(16) extern float ViewMatrix[16];
alignas(16) extern float ModelViewMatrix[16];
alignas(16) extern float ModelViewProjectionMatrix[16];
alignas(16) extern float ProjectionMatrix[16];
alignas(16) extern float ModelMatrix[16];
alignas(16) extern float ViewProjectionMatrix[16];

// ---- the loader's pools, which the static initialisers construct (Loader.cpp)

class SymbolPool;
class ConstructorPool;
class RuntimeAllocConstructorPool;
extern SymbolPool GlobalPool;
extern RuntimeAllocConstructorPool TheRuntimeAllocPool;
extern ConstructorPool TheConstructorPool;

// ---- textures (RenderContext.cpp, Tar.cpp, RenderMethod.cpp)

extern float LodBiasOverride;                               // 0 = each TAR's own
extern uint32_t FilterOverride;                             // 0xffffffff = each TAR's own
extern uint8_t YuvEnable;                                   // the TAR code's YUV-enable shadow
extern uint8_t ResourceRegistered;                          // a resource was registered: opcode 15 flushes the cache
extern BuiltInShapeFile BuiltInShapes;                      // EAGL's own SHPX, linked into the executable (Realgraph.h)

// ---- fonts and animation (Realgraph.cpp, anim/AnimDeltaF.cpp)

extern const uint8_t *DefaultFont;
extern uint8_t ReverseDeltaSumEnabled;                      // 0: going back decodes the block again

#endif // DRIVING_EAGL_EAGLGLOBALS_H_
