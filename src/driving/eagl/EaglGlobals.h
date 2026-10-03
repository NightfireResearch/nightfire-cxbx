#ifndef DRIVING_EAGL_EAGLGLOBALS_H_
#define DRIVING_EAGL_EAGLGLOBALS_H_

// EAGL's globals that more than one file of the port uses, each named once (docs/driving/eagl.md). D3D8's own tables
// and EAGL's shadows of what it sent are D3D8State.h's. The types a macro names (EAGL::RenderContext, SymbolPool...)
// come from the header of the file that defines them, included where the macro is used.

#include <stdint.h>

#include "../../helpers.h"

// ---- the allocator hooks: Device::SetNewOverride / SetDeleteOverride point them at the game's (GameCallbacks.cpp)

typedef void *(*EaglMallocFn)(uint32_t size, const char *name);
typedef void (*EaglFreeFn)(void *pointer, uint32_t size);
#define EaglMalloc (*(EaglMallocFn *)0x001caf68)
#define EaglFree (*(EaglFreeFn *)0x001caf6c)

// ---- the device and the current contexts

#define CurrentDevice (*(EAGL::Device **)0x0023fb60)
#define CurrentRenderContext (*(EAGL::RenderContext **)0x0023fb64)
#define CurrentTextureRenderContext (*(EAGL::TextureRenderContext **)0x0023fb68)
#define D3DDevicePointer PTR_AT(0x0023ff18)                 // D3D8's device, written by Direct3D_CreateDevice
#define CurrentVariation I32_AT(0x0023ff60)                 // EAGLInternal::CurrentVariation

// The render methods, linked through next: the constructed ones (the static initialisers'), and the children waiting
// for their parent's shaders.
#define ConstructedMethods (*(EAGL::RenderMethod **)0x0023ff20)
#define WaitingMethods (*(EAGL::RenderMethod **)0x0023ff24)

// ---- the matrices render methods read by name (gpViewMatrix ...)

#define ViewMatrix ((float *)0x0023f950)
#define ModelViewMatrix ((float *)0x0023f990)
#define ModelViewProjectionMatrix ((float *)0x0023f9d0)
#define ProjectionMatrix ((float *)0x0023fa10)
#define ModelMatrix ((float *)0x0023fa50)
#define ViewProjectionMatrix ((float *)0x0023fa90)

// ---- the loader's pools

#define GlobalPool (*(SymbolPool *)0x0023fb8c)
#define TheRuntimeAllocPool (*(RuntimeAllocConstructorPool *)0x0023fbb8)
#define TheConstructorPool (*(ConstructorPool *)0x0023fbe0)

// ---- textures

#define LodBiasOverride FLOAT_AT(0x0023ff0c)                // 0 = each TAR's own
#define FilterOverride U32_AT(0x001cb938)                   // 0xffffffff = each TAR's own
#define YuvEnable U8_AT(0x0023ffe0)                         // the TAR code's YUV-enable shadow
#define ResourceRegistered U8_AT(0x00240814)                // a resource was registered: opcode 15 flushes the cache
#define BuiltInShapes (*(ShapeFile *)0x001cbdd0)            // EAGL's own SHPX, linked into the executable

// ---- fonts and animation

#define DefaultFont (*(const uint8_t **)0x00241be0)
#define ReverseDeltaSumEnabled U8_AT(0x001ceb4c)            // 0: going back decodes the block again

// ---- allocation names, passed as the original's strings

#define NameTARNew ((const char *)0x0018a348)               // "EAGL::TAR new"
#define NameVertexShaderNew ((const char *)0x001cd144)      // "EAGL::VertexShader new"
#define NamePixelShaderNew ((const char *)0x001cd15c)       // "EAGL::PixelShader new"

#endif // DRIVING_EAGL_EAGLGLOBALS_H_
