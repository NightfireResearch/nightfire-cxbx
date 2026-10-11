#ifndef DRIVING_EAGL_EAGLGLOBALS_H_
#define DRIVING_EAGL_EAGLGLOBALS_H_

// EAGL's globals that more than one file of the port uses, each named once (docs/driving/eagl.md). D3D8's own tables
// and EAGL's shadows of what it sent are D3D8State.h's. The types a macro names (SymbolPool, ShapeFile...) come from
// the header of the file that defines them, included where the macro is used. The ones that are ours are defined
// in the file that writes them, with the original address in their XBE_GLOBAL tag.

#include <stdint.h>

#include "../../helpers.h"

namespace EAGL {
struct Device;
struct RenderContext;
struct RenderMethod;
struct TextureRenderContext;
}

// ---- the allocator hooks: Device::SetNewOverride / SetDeleteOverride point them at the game's (GameCallbacks.cpp)

typedef void *(*EaglMallocFn)(uint32_t size, const char *name);
typedef void (*EaglFreeFn)(void *pointer, uint32_t size);
// XBE_GLOBAL(0x001caf68, 4)
#define EaglMalloc (*(EaglMallocFn *)0x001caf68)
// XBE_GLOBAL(0x001caf6c, 4)
#define EaglFree (*(EaglFreeFn *)0x001caf6c)

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

// ---- the matrices render methods read by name (gpViewMatrix ...): the XBE's pointers to them stay the game's

// XBE_GLOBAL(0x0023f950, 0x40)
#define ViewMatrix ((float *)0x0023f950)
// XBE_GLOBAL(0x0023f990, 0x40)
#define ModelViewMatrix ((float *)0x0023f990)
// XBE_GLOBAL(0x0023f9d0, 0x40)
#define ModelViewProjectionMatrix ((float *)0x0023f9d0)
// XBE_GLOBAL(0x0023fa10, 0x40)
#define ProjectionMatrix ((float *)0x0023fa10)
// XBE_GLOBAL(0x0023fa50, 0x40)
#define ModelMatrix ((float *)0x0023fa50)
// XBE_GLOBAL(0x0023fa90, 0x40)
#define ViewProjectionMatrix ((float *)0x0023fa90)

// ---- the loader's pools, which the static initialisers construct where they are

#define GlobalPool (*(SymbolPool *)0x0023fb8c)
// XBE_GLOBAL(0x0023fbb8, 0x28)
#define TheRuntimeAllocPool (*(RuntimeAllocConstructorPool *)0x0023fbb8)
// XBE_GLOBAL(0x0023fbe0, 0x28)
#define TheConstructorPool (*(ConstructorPool *)0x0023fbe0)

// ---- textures (RenderContext.cpp, Tar.cpp, RenderMethod.cpp)

extern float LodBiasOverride;                               // 0 = each TAR's own
extern uint32_t FilterOverride;                             // 0xffffffff = each TAR's own
extern uint8_t YuvEnable;                                   // the TAR code's YUV-enable shadow
extern uint8_t ResourceRegistered;                          // a resource was registered: opcode 15 flushes the cache
// XBE_GLOBAL(0x001cbdd0, 0xba0)
#define BuiltInShapes (*(ShapeFile *)0x001cbdd0)            // EAGL's own SHPX, linked into the executable

// ---- fonts and animation (Realgraph.cpp, anim/AnimDeltaF.cpp)

extern const uint8_t *DefaultFont;
extern uint8_t ReverseDeltaSumEnabled;                      // 0: going back decodes the block again

#endif // DRIVING_EAGL_EAGLGLOBALS_H_
