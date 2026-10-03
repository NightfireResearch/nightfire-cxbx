#include "RenderMethod.h"

#include "Loader.h"
#include "Transform.h"
#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"

#include <emmintrin.h>
#include <string.h>
#include <xmmintrin.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGL's render methods (docs/driving/eagl.md 2.8, 2.9, 3.3, 4.6). A RenderMethod is a packet stream - a header
// dword (opcode << 16 | length in dwords) and operands, ended by a zero header - plus its shader variants.
// RenderMethod::Draw runs the stream over a GeoPrim: it keeps the method, the GeoPrim's parameter cursor and the
// packet cursor in three globals (0x002401bc, 0x002401c0, 0x002401c4) and calls each packet's handler through the
// opcode table at 0x001ce700. The table is left as it is (it points at the originals' addresses, which jump to the
// handlers here), so a handler stays patchable. Handlers take no arguments; most read one 8-byte parameter for the
// current variation (packet[1] * CurrentVariation + parameter.data) and step the parameter cursor.
//
// Every D3D8 call goes to the entry point's original address (our seam replaces it), with the original's
// convention: stdcall, except SetVertexShaderConstant1/4 (ECX register, EDX data) and NotInline (the same plus the
// dword count on the stack). The direct writes into D3D8's state are the stream source and shader caches EAGL keeps
// itself (0x00240470..0x002404c0); this module writes none of D3D8's own tables.
//
// The originals call EAGL::Device::Get (0x000e8a40, "mov eax, [0x0023fb60]; ret") before and after most D3D calls
// and throw the result away; those calls have no effect and are left out. Where an original tail-jumps to it, it
// returns the device in EAX, which no caller reads. Exception-handling frames (state for an allocation's
// constructor throwing) are left out too.
//
// The opcode handlers 15, 25 and 26 flush the CPU cache (WBINVD) when a vertex buffer was registered since the last
// draw (0x00240814); XboxStartup.cpp NOPs those instructions in the originals, and they are left out here - the
// flag is still cleared.
// ---------------------------------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#endif

using namespace EAGL;

namespace {

inline uint32_t &U32(uint32_t address) {
    return *(uint32_t *)(uintptr_t)address;
}

inline uint8_t &U8(uint32_t address) {
    return *(uint8_t *)(uintptr_t)address;
}

inline uint32_t Addr(const void *p) {
    return (uint32_t)(uintptr_t)p;
}

// EAGL's allocator hooks
inline void *EaglMalloc(uint32_t size, uint32_t name) {
    return ((void *(*)(uint32_t, const char *))U32(0x001caf68))(size, (const char *)(uintptr_t)name);
}

inline void EaglFree(void *p, uint32_t size) {
    ((void (*)(void *, uint32_t))U32(0x001caf6c))(p, size);
}

inline void BuiltinDelete(void *p) {
    ((void (*)(void *))0x001146e0)(p);   // __builtin_delete
}

// Allocation names and strings, the original's
const uint32_t kNamePCode = 0x001cda70;          // "EAGL::RenderMethod(RenderMethod &parent) allocating PCode block"
const uint32_t kNameVSArray = 0x001cdb00;        // "EAGL::VertexShader *"
const uint32_t kNamePSArray = 0x001cdb18;        // "EAGL::PixelShader *"
const uint32_t kNameVSNew = 0x001cd144;          // "EAGL::VertexShader new"
const uint32_t kNamePSNew = 0x001cd15c;          // "EAGL::PixelShader new"
const uint32_t kNameUserVar = 0x001ce3a0;        // "mUserVarData"
const uint32_t kNameVBNew = 0x001ce798;          // "EAGL::VertexBuffer new"
const uint32_t kNameD3DVB = 0x001ce7b0;          // "IDirect3DVertexBuffer8"
const uint32_t kStrPreMorph = 0x001ce6f0;        // "PreMorph"

// Globals
const uint32_t kMethodList = 0x0023ff20;   // constructed render methods (the static initialisers')
const uint32_t kChildList = 0x0023ff24;          // child render methods waiting for their parent's shaders
const uint32_t kDeviceCreated = 0x0023ff18;
const uint32_t kVariation = 0x0023ff60;          // EAGLInternal::CurrentVariation
const uint32_t kMethod = 0x002401bc;             // interpreter: render method
const uint32_t kParams = 0x002401c0;             //              parameter cursor
const uint32_t kPackets = 0x002401c4;            //              packet cursor
const uint32_t kStreamCount = 0x002401c8;        // the current stream (opcode 2): vertex count
const uint32_t kStreamNoCopy = 0x002401cc;       //   nonzero: opcode 11 does not copy the vertices
const uint32_t kStreamUser = 0x002401d0;         //   nonzero: opcode 11 draws from the caller's memory
const uint32_t kPrimitive = 0x00240470;
const uint32_t kPixelShaderInUse = 0x00240474;
const uint32_t kVertexShaderInUse = 0x00240478;
const uint32_t kStreams = 0x00240480;            // stream sources 0..15
const uint32_t kStrides = 0x002404c0;            // their strides
const uint32_t kIndexData = 0x00240500;
const uint32_t kIndexBase = 0x00240504;
const uint32_t kSkinSource = 0x00240804;
const uint32_t kSkinCount = 0x00240808;
const uint32_t kRegistered = 0x00240814;         // a vertex buffer was registered since the last draw
const uint32_t kPalette = 0x00240830;            // matrix palette scratch, 64 bytes a matrix
const uint32_t kOpcodeTable = 0x001ce700;

// ---- D3D8 entry points, by their original addresses
inline void D3D_SetStreamSource(uint32_t stream, void *buffer, uint32_t stride) {
    ((void (__stdcall *)(uint32_t, void *, uint32_t))0x0016a9c0)(stream, buffer, stride);
}
inline void D3D_BlockUntilNotBusy(void *resource) {
    ((void (__stdcall *)(void *))0x001693d0)(resource);
}
inline void D3D_Register(void *resource, void *base) {
    ((void (__stdcall *)(void *, void *))0x001693a0)(resource, base);
}
inline void D3D_Release(void *resource) {
    ((uint32_t (__stdcall *)(void *))0x00169230)(resource);
}
inline uint32_t D3D_IsBusy(void *resource) {
    return ((uint32_t (__stdcall *)(void *))0x00169310)(resource);
}
inline void D3D_RunPushBuffer(void *pushBuffer, void *fixup) {
    ((void (__stdcall *)(void *, void *))0x0016baa0)(pushBuffer, fixup);
}
inline void D3D_SetVertexShader(uint32_t handle) {
    ((void (__stdcall *)(uint32_t))0x0016ad90)(handle);
}
inline void D3D_SetPixelShader(uint32_t handle) {
    ((void (__stdcall *)(uint32_t))0x0016af60)(handle);
}
inline void D3D_SetVertexShaderConstant1(int reg, const void *data) {
    ((void (__fastcall *)(int, const void *))0x0016a790)(reg, data);
}
inline void D3D_SetVertexShaderConstant4(int reg, const void *data) {
    ((void (__fastcall *)(int, const void *))0x0016a7f0)(reg, data);
}
inline void D3D_SetVertexShaderConstantNotInline(int reg, const void *data, uint32_t dwords) {
    ((void (__fastcall *)(int, const void *, uint32_t))0x0016a980)(reg, data, dwords);
}
inline void D3D_SetIndices(void *indices, uint32_t baseVertex) {
    ((void (__stdcall *)(void *, uint32_t))0x00166a70)(indices, baseVertex);
}
inline void D3D_SetPixelShaderConstant(uint32_t reg, const void *data, uint32_t count) {
    ((void (__stdcall *)(uint32_t, const void *, uint32_t))0x0016b160)(reg, data, count);
}
inline void D3D_DrawVertices(uint32_t primitive, uint32_t start, uint32_t count) {
    ((void (__stdcall *)(uint32_t, uint32_t, uint32_t))0x0016b620)(primitive, start, count);
}
inline void D3D_DrawIndexedVertices(uint32_t primitive, uint32_t count, const void *indices) {
    ((void (__stdcall *)(uint32_t, uint32_t, const void *))0x0016b6c0)(primitive, count, indices);
}
inline void *D3D_CreateVertexBuffer2(uint32_t length) {
    return ((void *(__stdcall *)(uint32_t))0x0016b470)(length);
}
inline void *D3D_CreateIndexBuffer2(uint32_t length) {
    return ((void *(__stdcall *)(uint32_t))0x0016b430)(length);
}
inline uint8_t *D3D_VertexBufferLock2(void *buffer, uint32_t flags) {
    return ((uint8_t *(__stdcall *)(void *, uint32_t))0x0016b4c0)(buffer, flags);
}
inline void XG_SetVertexBufferHeader(uint32_t length, uint32_t usage, uint32_t fvf, uint32_t pool, void *buffer,
                                     uint32_t data) {
    ((void (__stdcall *)(uint32_t, uint32_t, uint32_t, uint32_t, void *, uint32_t))0x0017a8d6)(
        length, usage, fvf, pool, buffer, data);
}
inline void D3D_CreateVertexShader(const void *declaration, const void *function, uint32_t *handle, uint32_t usage) {
    ((uint32_t (__stdcall *)(const void *, const void *, uint32_t *, uint32_t))0x0016a650)(declaration, function,
                                                                                         handle, usage);
}
inline void D3D_DeleteVertexShader(uint32_t handle) {
    ((void (__stdcall *)(uint32_t))0x0016ac70)(handle);
}
inline void D3D_CreatePixelShader(const void *definition, uint32_t *handle) {
    ((uint32_t (__stdcall *)(const void *, uint32_t *))0x0016aef0)(definition, handle);
}
inline void D3D_DeletePixelShader(uint32_t handle) {
    ((void (__stdcall *)(uint32_t))0x0016af40)(handle);
}
inline void D3D_Begin(uint32_t primitive) {
    ((void (__stdcall *)(uint32_t))0x0016ba20)(primitive);
}
inline void D3D_End() {
    ((void (__stdcall *)())0x0016ba60)();
}
inline uint32_t D3D_InsertFence() {
    return ((uint32_t (__stdcall *)())0x00166140)();
}
inline void D3D_BlockOnFence(uint32_t fence) {
    ((void (__stdcall *)(uint32_t))0x00165fc0)(fence);
}

// ---- other modules' functions not ported yet
inline void *DeviceGet() {
    return ((void *(*)())0x000e8a40)();   // EAGL::Device::Get
}
inline bool TarSetStage(void *extension, uint32_t stage) {
    return ((bool (__fastcall *)(void *, int, uint32_t))0x000ebd60)(extension, 0, stage);
}
inline bool TarUse(void *tar) {
    return ((bool (__fastcall *)(void *, int))0x000eb3f0)(tar, 0);   // TAR::Use (candidate)
}

// ---- the interpreter's state
inline RenderMethod *CurrentMethod() {
    return *(RenderMethod **)(uintptr_t)kMethod;
}
inline GeoPrimParam *&ParamCursor() {
    return *(GeoPrimParam **)(uintptr_t)kParams;
}
inline uint32_t *PacketCursor() {
    return *(uint32_t **)(uintptr_t)kPackets;
}
// packet[1] * CurrentVariation + parameter.data: the current parameter's data for this variation
inline uint8_t *VariationData() {
    return (uint8_t *)(uintptr_t)(PacketCursor()[1] * U32(kVariation) + Addr(ParamCursor()->data));
}
inline void NextParam() {
    ParamCursor() = ParamCursor() + 1;
}
// The flush the draw handlers do first: WBINVD in the original (NOPed by XboxStartup.cpp), then the flag cleared.
inline void FlushIfRegistered() {
    if (U8(kRegistered) != 0)
        U8(kRegistered) = 0;
}

// Opcodes 5-8: count / 4 constant registers (SAR) from the parameter at packet[2]
inline void VSConstantsOp() {
    uint32_t *c = PacketCursor();
    EAGL_SetVertexShaderConstant((int)c[2], VariationData(), (int)c[3] >> 2);
    NextParam();
}

// Opcodes 17, 18: two constant blocks from one parameter, the second packet[3] registers on
inline void VSConstantPairOp() {
    uint32_t *c = PacketCursor();
    uint32_t reg1 = c[4], count1 = c[5], reg0 = c[2];
    uint8_t *d = VariationData();
    uint32_t count0 = c[3];
    EAGL_SetVertexShaderConstant((int)reg0, d, (int)count0);
    EAGL_SetVertexShaderConstant((int)reg1, d + (count0 << 4), (int)count1);
    NextParam();
}

// Opcodes 19, 20, 27, 28: a matrix, transposed, into 4 vertex shader constants
inline void VSMatrixOp() {
    alignas(16) float t[16];
    uint32_t reg = PacketCursor()[2];
    VU0_MATRIX4_transpose(t, VariationData());
    EAGL_SetVertexShaderConstant((int)reg, t, 4);
    NextParam();
}

// Opcodes 21, 22: the matrix palette
inline void MatrixPaletteOp() {
    uint32_t *c = PacketCursor();
    EAGL_UploadMatrixPalette((int)c[2], (int)c[3], (int)c[4], (int)c[5], VariationData());
    NextParam();
}

// Opcodes 29, 30: CPU skinning from the source opcode 31 set
inline void SkinOp() {
    uint32_t *c = PacketCursor();
    EAGL_SkinAndUpload((const uint8_t *)(uintptr_t)U32(kSkinSource), (int)U32(kSkinCount), (int)c[2], (int)c[3],
                       (int)c[4], (int)c[5], VariationData());
    NextParam();
}

// Opcodes 32, 33: a matrix, transposed, into 4 pixel shader constants
inline void PSMatrixOp() {
    alignas(16) float t[16];
    uint32_t reg = PacketCursor()[2];
    VU0_MATRIX4_transpose(t, VariationData());
    EAGL_SetPixelShaderConstant(reg, t, 4);
    NextParam();
}

// Opcodes 34, 35: count / 4 pixel shader constants (SAR)
inline void PSConstantsOp() {
    uint32_t *c = PacketCursor();
    EAGL_SetPixelShaderConstant(c[2], VariationData(), (uint32_t)((int)c[3] >> 2));
    NextParam();
}

// An identity matrix, as DrawGouraud::Init and DrawTextured::Init store it
inline void Identity(float *m) {
    for (int i = 0; i < 16; i++)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------------------------

// A child render method takes its parent's description and an owned copy of its packets. The copy's block starts
// with its size in bytes (the dword before the packets), which Destruct frees by.
// FUNC_AT(0x000f0e50)
void EAGL_CopyParentPackets(EAGL::RenderMethod *method) {
    RenderMethod *parent = method->parent;
    method->numVariants = parent->numVariants;
    method->declaration = parent->declaration;
    method->vsMicrocode = parent->vsMicrocode;
    method->psDefinitions = parent->psDefinitions;
    method->variantHalf = parent->variantHalf;
    method->paramNames = parent->paramNames;
    method->vertexShaders = parent->vertexShaders;
    method->pixelShaders = parent->pixelShaders;
    method->name = parent->name;
    // The stream's length in dwords: the walk ends at a packet of length 0 (the zero header)
    const uint32_t *source = parent->packets;
    uint32_t n = 0, length;
    do {
        length = source[n] & 0xffff;
        n += length;
    } while (length != 0);
    uint32_t bytes = n * 4 + 4;
    uint32_t block = bytes + 4;
    uint32_t *memory = (uint32_t *)EaglMalloc(block, kNamePCode);
    method->packets = memory;
    memory[0] = block;
    method->packets = memory + 1;
    MEM_copy(memory + 1, parent->packets, (int)bytes);
}

// The loader's callback for a VertexBuffer symbol: point the D3D8 header at the data and register it against the
// image (D3DResource_Register adds the base).
// FUNC_AT(0x000f0ee0)
void EAGL_VertexBufferConstructor(EAGL::LoadedVertexBuffer *buffer, DynamicLoader *loader) {
    uint32_t offset = buffer->fileOffset;
    void *base = loader->GetElfData();
    buffer->header[1] = offset - Addr(base);
    D3D_Register(buffer->header, base);
    U8(kRegistered) = 1;
}

// FUNC_AT(0x000f0f10)
void EAGL_VertexBufferDestructor(EAGL::LoadedVertexBuffer *buffer) {
    for (uint32_t stream = 0; stream < 16; stream++) {
        if (U32(kStreams + stream * 4) == Addr(buffer->header)) {
            U32(kStreams + stream * 4) = 0;
            D3D_SetStreamSource(stream, NULL, 0);
        }
    }
    D3D_BlockUntilNotBusy(buffer->header);
}

// The static initialisers' constructor (bondrm render methods the game builds in code)
// FUNC_AT(0x000f0f60)
EAGL::RenderMethod* EAGL::RenderMethod::Construct(uint32_t *packets_, int numVariants_, const void *declaration_,
                                                  const void **vsMicrocode_, VertexShader **vertexShaders_,
                                                  const uint8_t **psDefinitions_, PixelShader **pixelShaders_,
                                                  const char **paramNames_, uint32_t variantHalf_,
                                                  const char *name_) {
    packets = packets_;
    numVariants = numVariants_;
    declaration = declaration_;
    vsMicrocode = vsMicrocode_;
    vertexShaders = vertexShaders_;
    psDefinitions = psDefinitions_;
    pixelShaders = pixelShaders_;
    paramNames = paramNames_;
    variantHalf = variantHalf_;
    cloned = 0;
    parent = NULL;
    name = name_;
    next = *(RenderMethod **)(uintptr_t)kMethodList;
    *(RenderMethod **)(uintptr_t)kMethodList = this;
    return this;
}

// The interpreter (3.3). The length is the header's, read before the handler runs; the cursor is re-read after.
// FUNC_AT(0x000f0fc0)
void EAGL::RenderMethod::Draw(GeoPrim *prim) {
    *(RenderMethod **)(uintptr_t)kMethod = this;
    uint32_t *cursor = packets;
    *(uint32_t **)(uintptr_t)kPackets = cursor;
    ParamCursor() = prim->params;
    uint32_t header = *cursor;
    while (header != 0) {
        uint32_t length = header & 0xffff;
        // No bounds check: an opcode above 36 calls whatever follows the table, as the original does
        ((void (*)())(uintptr_t)U32(kOpcodeTable + (header >> 16) * 4))();
        cursor = PacketCursor() + length;
        *(uint32_t **)(uintptr_t)kPackets = cursor;
        header = *cursor;
    }
}

// FUNC_AT(0x000f1020)
void EAGL::RenderMethod::Nop1020(uint32_t) {
}

// Model::Optimize asks whether a GeoPrim's method allows it; on the Xbox, always.
// FUNC_AT(0x000f1030)
bool EAGL::RenderMethod::CanOptimize(void *, uint32_t) {
    return true;
}

// FUNC_AT(0x000f1040)
void EAGL::RenderMethod::Nop1040(uint32_t) {
}

// One vertex and one pixel shader per variant. The pixel shader definition is taken 4 bytes in when variantHalf
// is 0.
// FUNC_AT(0x000f1050)
void EAGL::RenderMethod::CreateShaders() {
    int half = variantHalf == 0 ? 1 : 0;
    vertexShaders = (VertexShader **)EaglMalloc((uint32_t)numVariants << 2, kNameVSArray);
    pixelShaders = (PixelShader **)EaglMalloc((uint32_t)numVariants * 4, kNamePSArray);
    for (int i = 0; i < numVariants; i++) {
        VertexShader *vs = (VertexShader *)EaglMalloc(4, kNameVSNew);
        vs = vs != NULL ? vs->Construct(declaration, vsMicrocode[i]) : NULL;
        vertexShaders[i] = vs;
        PixelShader *ps = (PixelShader *)EaglMalloc(4, kNamePSNew);
        ps = ps != NULL ? ps->Construct(psDefinitions[i] + half * 4) : NULL;
        pixelShaders[i] = ps;
    }
}

// FUNC_AT(0x000f1180)
void EAGL::RenderMethod::SetVertexShaderTable(VertexShader **const *table) {
    vertexShaders = *table;
}

// FUNC_AT(0x000f1190)
EAGL::VertexShader*** EAGL::RenderMethod::GetVertexShaderTable() {
    return &vertexShaders;
}

// FUNC_AT(0x000f11a0)
void EAGL::RenderMethod::SetPixelShaderTable(PixelShader **const *table) {
    pixelShaders = *table;
}

// FUNC_AT(0x000f11b0)
EAGL::PixelShader*** EAGL::RenderMethod::GetPixelShaderTable() {
    return &pixelShaders;
}

// The loader's callback for a RenderMethod symbol: a method with a parent (and variantHalf 0) shares the parent's
// shaders, creating them first if need be; any other creates its own unless it has them.
// FUNC_AT(0x000f11c0)
void EAGL_RenderMethodConstructor(EAGL::RenderMethod *method) {
    RenderMethod *parent = method->parent;
    if (parent != NULL && method->variantHalf == 0) {
        if (parent->vertexShaders == NULL)
            parent->CreateShaders();
        method->vertexShaders = parent->vertexShaders;
        method->pixelShaders = parent->pixelShaders;
        return;
    }
    if (method->vertexShaders == NULL)
        method->CreateShaders();
}

// Every opcode-11 packet's dynamic vertex buffer destroyed and its three words reset to -1. A packet of length 0
// with a nonzero header would loop forever, as in the original.
// FUNC_AT(0x000f1210)
void EAGL_ReleaseDynamicBuffers(EAGL::RenderMethod *method) {
    uint32_t *p = method->packets;
    if (*p == 0)
        return;
    do {
        uint32_t header = *p;
        uint32_t length = header & 0xffff;
        if ((header & 0xffff0000) == 0xb0000 && p[4] != 0xffffffffu) {
            DynamicVertexBuffer *buffer = (DynamicVertexBuffer *)(uintptr_t)p[4];
            if (buffer != NULL) {
                buffer->Destruct();
                EaglFree(buffer, 0x30);
            }
            p[4] = 0xffffffffu;
            p[5] = 0xffffffffu;
            p[6] = 0xffffffffu;
        }
        p += length;
    } while (*p != 0);
}

// A render method that inherits from a parent (the game's materials and effects). Until the parent has its
// shaders it waits in the list at 0x0023ff24, which RenderContext::SetupFrameBuffers copies.
// FUNC_AT(0x000f1270)
EAGL::RenderMethod* EAGL::RenderMethod::ConstructChild(RenderMethod *parent_) {
    next = NULL;
    cloned = 1;
    parent = parent_;
    if (parent_->vertexShaders != NULL) {
        EAGL_CopyParentPackets(this);
        return this;
    }
    next = *(RenderMethod **)(uintptr_t)kChildList;
    *(RenderMethod **)(uintptr_t)kChildList = this;
    return this;
}

// A child frees its packet copy (after its dynamic buffers); any other deletes its shader arrays with
// __builtin_delete, though they came from EAGLMalloc (the original's mismatch, kept).
// FUNC_AT(0x000f12c0)
void EAGL::RenderMethod::Destruct() {
    if (cloned != 0) {
        EAGL_ReleaseDynamicBuffers(this);
        packets = packets - 1;
        uint32_t *block = packets;
        EaglFree(block, *block);
        return;
    }
    BuiltinDelete(vertexShaders);
    BuiltinDelete(pixelShaders);
}

// FUNC_AT(0x000f1300)
void EAGL::RenderMethod::DeleteShaders() {
    for (int i = 0; i < numVariants; i++) {
        VertexShader *vs = vertexShaders[i];
        if (vs != NULL) {
            vs->Destruct();
            EaglFree(vs, 4);
        }
        PixelShader *ps = pixelShaders[i];
        if (ps != NULL) {
            ps->Destruct();
            EaglFree(ps, 4);
        }
    }
    EaglFree(vertexShaders, (uint32_t)numVariants << 2);
    EaglFree(pixelShaders, (uint32_t)numVariants << 2);
    vertexShaders = NULL;
    pixelShaders = NULL;
}

// FUNC_AT(0x000f1390)
void EAGL_RenderMethodDestructor(EAGL::RenderMethod *method) {
    if (method->parent == NULL && method->vertexShaders != NULL)
        method->DeleteShaders();
}

// 0x000f13b0, Transform::Invert's and Determinant's determinant (EAX = matrix, answered in ST0, other registers
// kept): the same instructions as the maths library's Determinant4x4, which is ours (Transform.cpp says so too).
// A register argument, so an adaptor tagged AUTOLTCG under Ghidra's name (FUNC_AT refuses EAX inputs).
// AUTOLTCG
__declspec(naked) void FUN_000f13b0() {
    __asm {
        push ecx
        push edx
        push eax
        call Determinant4x4
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// a3 a4 / a6 a7 ... the 3x3 determinant of nine floats on the stack, by the first row's cofactors (x87 order)
// FUNC_AT(0x000f1490)
double EAGL_Determinant3x3(float a0, float a1, float a2, float a3, float a4, float a5, float a6, float a7,
                           float a8) {
    double r = ((double)a3 * (double)a7 - (double)a4 * (double)a6) * (double)a2;
    r = r + ((double)a5 * (double)a6 - (double)a3 * (double)a8) * (double)a1;
    r = r + ((double)a4 * (double)a8 - (double)a5 * (double)a7) * (double)a0;
    return r;
}

// FUNC_AT(0x000f14e0)
void* EAGL::RenderMatrix::MultiplyInto(const float *rhs, float *out) {
    return VU0_MATRIX4_mult(out, m, rhs);
}

// ---------------------------------------------------------------------------------------------------------------
// The D3D8 wrappers (0x000f4340..0x000f4580). The stream source and shader wrappers skip a call when the value is
// the one they last sent (their caches at 0x00240474..0x002404c0).
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000f4340)
void EAGL_RunPushBuffer(EAGL::PushBuffer *buffer) {
    D3D_RunPushBuffer(buffer->header, NULL);
}

// FUNC_AT(0x000f4350)
void EAGL_SetVertexShader(const EAGL::VertexShader *shader) {
    if (U32(kVertexShaderInUse) == shader->handle)
        return;
    U32(kVertexShaderInUse) = shader->handle;
    D3D_SetVertexShader(shader->handle);
}

// FUNC_AT(0x000f4380)
void EAGL_SetPixelShader(const EAGL::PixelShader *shader) {
    if (U32(kPixelShaderInUse) == shader->handle)
        return;
    U32(kPixelShaderInUse) = shader->handle;
    D3D_SetPixelShader(shader->handle);
}

// The register is biased by 96 as D3D8's inline functions do (constants -96..95 -> 0..191). count 1 and 4 go
// through the inline entry points, anything else through NotInline with a dword count (count 0 included).
// FUNC_AT(0x000f43a0)
void EAGL_SetVertexShaderConstant(int reg, const void *data, int count) {
    if (count == 1)
        D3D_SetVertexShaderConstant1(reg + 0x60, data);
    else if (count == 4)
        D3D_SetVertexShaderConstant4(reg + 0x60, data);
    else
        D3D_SetVertexShaderConstantNotInline(reg + 0x60, data, (uint32_t)count << 2);
}

// FUNC_AT(0x000f43e0)
void EAGL_SetStreamSource(uint32_t stream, const EAGL::StaticVertexBuffer *buffer) {
    if (U32(kStreams + stream * 4) == Addr(buffer->buffer) && U32(kStrides + stream * 4) == buffer->stride)
        return;
    U32(kStreams + stream * 4) = Addr(buffer->buffer);
    U32(kStrides + stream * 4) = buffer->stride;
    D3D_SetStreamSource(stream, buffer->buffer, buffer->stride);
}

// FUNC_AT(0x000f4420)
void EAGL_SetDynamicStreamSource(uint32_t stream, const EAGL::DynamicVertexBuffer *buffer) {
    if (U32(kStreams + stream * 4) == Addr(buffer->current) && U32(kStrides + stream * 4) == buffer->stride)
        return;
    U32(kStreams + stream * 4) = Addr(buffer->current);
    U32(kStrides + stream * 4) = buffer->stride;
    D3D_SetStreamSource(stream, buffer->current, buffer->stride);
}

// FUNC_AT(0x000f4460)
void EAGL_SetIndices(uint32_t baseVertex, const EAGL::IndexBuffer *buffer) {
    D3D_SetIndices(buffer->buffer, baseVertex);
}

// Opcode 14's: the index data DrawIndexedVertices reads (no D3D call)
// FUNC_AT(0x000f4480)
void EAGL_SetIndexData(uint32_t base, const uint8_t *indices) {
    U32(kIndexData) = Addr(indices);
    U32(kIndexBase) = base;
}

// FUNC_AT(0x000f44a0)
void EAGL_SetPixelShaderConstant(uint32_t reg, const void *data, uint32_t count) {
    D3D_SetPixelShaderConstant(reg, data, count);
}

// FUNC_AT(0x000f44c0)
void EAGL_DrawVertices(uint32_t start, uint32_t count) {
    D3D_DrawVertices(U32(kPrimitive), start, count);
}

// Unreferenced: indices from D3D8's push-buffer pointer (0x00175420) instead of the stored index data.
// FUNC_AT(0x000f44e0)
void EAGL_DrawIndexedVerticesPushBuffer(uint32_t, uint32_t, uint32_t start, uint32_t count) {
    uint32_t indices = U32(0x00175420) + start * 2;
    D3D_DrawIndexedVertices(U32(kPrimitive), count, (const void *)(uintptr_t)indices);
}

// FUNC_AT(0x000f4500)
void EAGL_DrawIndexedVertices(uint32_t start, uint32_t count) {
    uint32_t indices = U32(kIndexData) + start * 2;
    D3D_DrawIndexedVertices(U32(kPrimitive), count, (const void *)(uintptr_t)indices);
}

// 0x000f4520 ("rdtsc; ret") is XboxTimer.cpp's: it redirects that one already.

// CVTSS2SI: rounded by MXCSR (round to nearest, the default)
// FUNC_AT(0x000f4530)
int EAGL_RoundToInt(float value) {
    return _mm_cvt_ss2si(_mm_set_ss(value));
}

// Unreferenced code taking EAX, ECX and EDX: [EDX + ECX * 4 + 0x20] = EAX, answers true. In its own instructions:
// no C++ convention passes EAX.
// FUNC_AT(0x000f4540)
__declspec(naked) void EAGL_StoreRegisterArgs() {
    __asm {
        mov dword ptr [edx + ecx * 4 + 0x20], eax
        mov al, 1
        ret
    }
}

// FUNC_AT(0x000f4550)
EAGL::PairedArrays32* EAGL::PairedArrays32::Construct() {
    countA = 0;
    countB = 0;
    for (int i = 0; i < 32; i++) {
        b[i] = 0;
        a[i] = 0;
    }
    return this;
}

// ---------------------------------------------------------------------------------------------------------------
// DrawArray (dead in this game: only the dead DynamicModel::SetPrimitiveType makes one)
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000f55a0)
bool EAGL::DrawArray::SetGeoPrim(GeoPrim *prim) {
    geoPrim = prim;
    ((bool (__fastcall *)(void *, int, GeoPrim *))0x000e9890)(model, 0, prim);   // DynamicModel::AddGeoPrim
    paramCount = 0;
    while (geoPrim->method->paramNames[paramCount] != NULL)
        paramCount++;
    userVars = (UserVar *)EaglMalloc((uint32_t)paramCount << 3, kNameUserVar);
    if (userVars == NULL)
        return false;
    for (int i = 0; i < paramCount; i++) {
        userVars[i].data = geoPrim->params[i].data;
        userVars[i].count = geoPrim->params[i].count;
    }
    return true;
}

// FUNC_AT(0x000f5640)
bool EAGL::DrawArray::SetPrimitiveType(int type) {
    switch (type) {   // the jump table at 0x000f5698
    case -1: case 1: case 2: case 5:
        primitiveClass = 0;
        break;
    case 4:
        primitiveClass = 1;
        break;
    case 6:
        primitiveClass = 2;
        break;
    default:
        primitiveClass = -1;
        break;
    }
    if (primitiveClass == -1)
        return false;
    unk24 = 0xffffu - (uint32_t)primitiveClass;
    maxVerts = 0xffff;
    unk28 = 0xffff;
    primitiveType = (uint32_t)type;
    return true;
}

// FUNC_AT(0x000f56c0)
int EAGL::DrawArray::GetIndexFromName(const char *name_) {
    int count = paramCount;
    if (count <= 0)
        return -1;
    const char **names = geoPrim->method->paramNames;
    for (int i = 0; i < count; i++)
        if (strcmp(name_, names[i]) == 0)
            return i;
    return -1;
}

// FUNC_AT(0x000f5730)
const char* EAGL::DrawArray::GetNameFromIndex(int index) {
    return geoPrim->method->paramNames[index];
}

// FUNC_AT(0x000f5750)
bool EAGL::DrawArray::SetParamName(const char *name_) {
    int index = GetIndexFromName(name_);
    streamParam = index;
    return index != -1;
}

// mode 0: the parameter points at the data with a count of 1; otherwise only the count is set, and only once a
// count other than -1 has been given (keepCounts).
// FUNC_AT(0x000f5770)
bool EAGL::DrawArray::SetVar(int index, uint8_t *data, int mode, uint32_t count) {
    if (count != 0xffffffffu)
        keepCounts = 1;
    if (index == -1)
        return false;
    userVars[index].data = data;
    if (mode == 0) {
        GeoPrimParam *p = &geoPrim->params[index];
        p->data = userVars[index].data;
        p->count = 1;
        return true;
    }
    if (keepCounts != 0)
        geoPrim->params[index].count = count;
    return true;
}

// FUNC_AT(0x000f57d0)
bool EAGL::DrawArray::Lock() {
    locked = 1;
    dirty = 1;
    return true;
}

// FUNC_AT(0x000f57e0)
bool EAGL::DrawArray::Unlock() {
    locked = 0;
    return true;
}

// FUNC_AT(0x000f57f0)
bool EAGL::DrawArray::SetNumVerts(uint32_t count) {
    numVerts = count;
    return true;
}

// FUNC_AT(0x000f5800)
bool EAGL::DrawArray::SetLocalMatrix(const float *matrix) {
    ((void (__fastcall *)(void *, int, const float *))0x000e9a80)(model, 0, matrix);   // DynamicModel::SetModelMatrix
    return true;
}

// FUNC_AT(0x000f5820)
void EAGL::DrawArray::SetUpGeoPrim(uint32_t) {
    int verts = drawVerts;
    if (verts > maxVerts)
        verts = maxVerts;
    GeoPrimParam *params = geoPrim->params;
    for (int i = 0; i < paramCount; i++) {
        params[i].data = userVars[i].data;
        if (keepCounts == 0)
            params[i].count = (uint32_t)verts;
    }
    if (streamParam != -1) {
        stream[0] = (uint32_t)verts;
        stream[1] = 0;
        stream[2] = 0;
        stream[3] = dirty == 0 ? 1 : 0;
        params[streamParam].data = (uint8_t *)stream;
        params[streamParam].count = 1;
    }
}

// FUNC_AT(0x000f5c30)
void EAGL::DrawArray::Destruct() {
    if (locked != 0)
        locked = 0;
    if (userVars != NULL)
        EaglFree(userVars, (uint32_t)paramCount << 3);
    void *dynamicModel = model;
    if (dynamicModel != NULL) {
        ((void (__fastcall *)(void *, int))0x000ea720)(dynamicModel, 0);   // DynamicModel::~DynamicModel
        EaglFree(dynamicModel, 0x58);
    }
}

// FUNC_AT(0x000f5c80)
bool EAGL::DrawArray::SetVarByName(const char *name_, uint8_t *data, int mode, uint32_t count) {
    return SetVar(GetIndexFromName(name_), data, mode, count);
}

// FUNC_AT(0x000f5cb0)
bool EAGL::DrawArray::Draw(int count) {
    numVerts = (uint32_t)count;
    if (locked != 0)
        return false;
    drawVerts = count;
    SetUpGeoPrim(0);
    ((void (__fastcall *)(void *, int))0x000e99e0)(model, 0);   // DynamicModel::Draw
    dirty = 0;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// DrawGouraud (the profiler's) and DrawTextured (the movie player's, dead)
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000f58b0)
EAGL::DrawGouraud* EAGL::DrawGouraud::Construct() {
    primitiveType = 1;
    state.Construct();
    VertexShader *vs = (VertexShader *)EaglMalloc(4, kNameVSNew);
    vs = vs != NULL ? vs->Construct((const void *)0x001ce218u, (const void *)0x001ce228u) : NULL;
    vertexShader = vs;
    PixelShader *ps = (PixelShader *)EaglMalloc(4, kNamePSNew);
    ps = ps != NULL ? ps->Construct((const void *)0x001ce2b0u) : NULL;
    pixelShader = ps;
    return this;
}

// FUNC_AT(0x000f5970)
bool EAGL::DrawGouraud::Init() {
    state.SetTextureEnable(false);
    state.SetShading(1);
    state.SetAlphaBlendMode(0);
    state.SetCullEnable(false);
    begun = 0;
    Identity(matrix);
    return true;
}

// FUNC_AT(0x000f59f0)
void EAGL::DrawGouraud::InternalFlush() {
    if (begun == 0)
        return;
    D3D_End();
    D3D_Begin(primitiveType);
}

// FUNC_AT(0x000f5a10)
EAGL::DrawTextured* EAGL::DrawTextured::Construct() {
    primitiveType = 1;
    state.Construct();
    return this;
}

// FUNC_AT(0x000f5a30)
bool EAGL::DrawTextured::Init() {
    state.SetTextureEnable(true);
    state.SetTextureCoordType(0xffffffffu);
    state.SetShading(1);
    state.SetAlphaBlendMode(0);
    state.SetCullEnable(false);
    VertexShader *vs = (VertexShader *)EaglMalloc(4, kNameVSNew);
    vs = vs != NULL ? vs->Construct((const void *)0x001ce078u, (const void *)0x001ce090u) : NULL;
    vertexShader = vs;
    PixelShader *ps = (PixelShader *)EaglMalloc(4, kNamePSNew);
    ps = ps != NULL ? ps->Construct((const void *)0x001ce128u) : NULL;
    pixelShader = ps;
    tar = NULL;
    begun = 0;
    Identity(matrix);
    return true;
}

// FUNC_AT(0x000f5b60)
void EAGL::DrawTextured::Destruct() {
    VertexShader *vs = vertexShader;
    if (vs != NULL) {
        vs->Destruct();
        EaglFree(vs, 4);
    }
    PixelShader *ps = pixelShader;
    if (ps != NULL) {
        ps->Destruct();
        EaglFree(ps, 4);
    }
    state.Destruct();
}

// FUNC_AT(0x000f5be0)
void EAGL::DrawTextured::InternalFlush() {
    if (begun == 0)
        return;
    D3D_End();
    D3D_Begin(primitiveType);
}

// FUNC_AT(0x000f5c00)
void EAGL::DrawTextured::SetTAR(void *tar_) {
    if (begun != 0) {
        D3D_End();
        D3D_Begin(primitiveType);
    }
    tar = tar_;
}

// FUNC_AT(0x000f5cf0)
void EAGL::DrawGouraud::Destruct() {
    VertexShader *vs = vertexShader;
    if (vs != NULL) {
        vs->Destruct();
        EaglFree(vs, 4);
    }
    PixelShader *ps = pixelShader;
    if (ps != NULL) {
        ps->Destruct();
        EaglFree(ps, 4);
    }
    state.Destruct();
}

// The view-projection of the current viewport (render-to-texture first) times the object's matrix, transposed,
// into constants 0-3; the state applied, the TAR bound, Begin.
// FUNC_AT(0x0014cf10)
void EAGL::DrawTextured::Begin(uint32_t type) {
    if (type != primitiveType) {
        InternalFlush();
        primitiveType = type;
        state.SetPrimitiveType(type);
    }
    EAGL_SetVertexShader(vertexShader);
    EAGL_SetPixelShader(pixelShader);
    if (DeviceGet() != NULL) {
        typedef void *(__fastcall *Getter)(void *, int);
        void *view;
        if (((Getter)0x000e89f0)(DeviceGet(), 0) != NULL) {   // Device::GetCurrentTextureRenderContext
            void *context = ((Getter)0x000e89f0)(DeviceGet(), 0);
            view = ((Getter)0x000f3520)(context, 0);            // TextureRenderContext::GetCurrentViewPort
        } else {
            void *context = ((Getter)0x000e89e0)(DeviceGet(), 0);   // Device::GetCurrentRenderContext
            view = ((Getter)0x000ee080)(context, 0);                // RenderContext::GetCurrentViewPort
        }
        const float *viewProjection = (const float *)((Getter)0x000f39b0)(view, 0);   // view + 0xc0
        alignas(16) Transform t;
        t.BuildMatrix(viewProjection);
        t.PrependMatrix(matrix);
        t.Transpose();
        EAGL_SetVertexShaderConstant(0, t.m, 4);
    }
    state.Apply();
    if (tar != NULL)
        TarUse(tar);
    D3D_Begin(primitiveType);
    begun = 1;
}

// ---------------------------------------------------------------------------------------------------------------
// Helpers of the handlers
// ---------------------------------------------------------------------------------------------------------------

// Model::Call's: before and after a morph, wait for the GPU when the name is "PreMorph" and the device exists.
// It ignores its `this`, so stdcall (the same bytes popped).
// FUNC_AT(0x000f5d70)
void __stdcall EAGL_ModelCallFence(const char *name) {
    if (strcmp(name, (const char *)(uintptr_t)kStrPreMorph) == 0 && U32(kDeviceCreated) != 0)
        D3D_BlockOnFence(D3D_InsertFence());
    U8(kRegistered) = 1;
}

// FUNC_AT(0x000f5de0)
bool __stdcall EAGL_ReturnFalse(uint32_t) {
    return false;
}

// Opcodes 21, 22: count0 / 4 and count1 / 4 matrices (signed division, toward zero) transposed into the palette
// scratch, then both blocks uploaded - the second even when count1 is 0 (NotInline with 0 dwords).
// FUNC_AT(0x000f5df0)
void EAGL_UploadMatrixPalette(int reg0, int count0, int reg1, int count1, const uint8_t *matrices) {
    int n0 = count0 / 4;
    int n1 = count1 / 4;
    uint8_t *palette = (uint8_t *)(uintptr_t)kPalette;
    for (int i = 0; i < n0; i++)
        VU0_MATRIX4_transpose(palette + i * 0x40, matrices + i * 0x40);
    for (int j = 0; j < n1; j++)
        VU0_MATRIX4_transpose(palette + (n0 + j) * 0x40, matrices + (n0 + j) * 0x40);
    EAGL_SetVertexShaderConstant(reg0, palette, count0);
    EAGL_SetVertexShaderConstant(reg1, palette + n0 * 0x40, count1);
}

// One skinned matrix: up to three palette matrices blended by weight. A 16-byte vertex record holds three weights
// as floats, and each weight's low byte is also its bone index (the bit pattern's low mantissa byte). The second
// and third count only when their dword is nonzero (the third only after the second). SSE as the original: the
// rows of the first matrix times the first weight, then each further row times its weight added, lane for lane.
// The first weight goes through the x87 (fld/fst) before its index is taken, so a signalling NaN would be quieted
// first; that is reproduced.
// FUNC_AT(0x000f5eb0)
void EAGL_SkinMatrix(const uint8_t *vertices, int index, const uint8_t *palette, float *out) {
    const uint8_t *v = vertices + (index << 4);
    uint32_t w0, w1, w2;
    memcpy(&w0, v, 4);
    memcpy(&w1, v + 4, 4);
    memcpy(&w2, v + 8, 4);
    if ((w0 & 0x7f800000u) == 0x7f800000u && (w0 & 0x007fffffu) != 0 && (w0 & 0x00400000u) == 0)
        w0 |= 0x00400000u;   // FLD of a signalling NaN
    const float *m0 = (const float *)(palette + ((w0 & 0xff) << 6));
    const float *m1 = (const float *)(palette + ((w1 & 0xff) << 6));
    const float *m2 = (const float *)(palette + ((w2 & 0xff) << 6));
    __m128 r0 = _mm_load_ps(m0);
    __m128 r1 = _mm_load_ps(m0 + 4);
    __m128 r2 = _mm_load_ps(m0 + 8);
    __m128 r3 = _mm_load_ps(m0 + 12);
    __m128 s = _mm_load_ss((const float *)&w0);
    s = _mm_shuffle_ps(s, s, 0);
    r0 = _mm_mul_ps(r0, s);
    r1 = _mm_mul_ps(r1, s);
    r2 = _mm_mul_ps(r2, s);
    r3 = _mm_mul_ps(r3, s);
    if (w1 != 0) {
        s = _mm_load_ss((const float *)&w1);
        s = _mm_shuffle_ps(s, s, 0);
        r0 = _mm_add_ps(r0, _mm_mul_ps(_mm_load_ps(m1), s));
        r1 = _mm_add_ps(r1, _mm_mul_ps(_mm_load_ps(m1 + 4), s));
        r2 = _mm_add_ps(r2, _mm_mul_ps(_mm_load_ps(m1 + 8), s));
        r3 = _mm_add_ps(r3, _mm_mul_ps(_mm_load_ps(m1 + 12), s));
        if (w2 != 0) {
            s = _mm_load_ss((const float *)&w2);
            s = _mm_shuffle_ps(s, s, 0);
            r0 = _mm_add_ps(r0, _mm_mul_ps(_mm_load_ps(m2), s));
            r1 = _mm_add_ps(r1, _mm_mul_ps(_mm_load_ps(m2 + 4), s));
            r2 = _mm_add_ps(r2, _mm_mul_ps(_mm_load_ps(m2 + 8), s));
            r3 = _mm_add_ps(r3, _mm_mul_ps(_mm_load_ps(m2 + 12), s));
        }
    }
    _mm_store_ps(out, r0);
    _mm_store_ps(out + 4, r1);
    _mm_store_ps(out + 8, r2);
    _mm_store_ps(out + 12, r3);
}

// The inline helpers the handlers are built from, left as functions after a RET (unreferenced)
// FUNC_AT(0x000f5fd0)
uint8_t* EAGL_RMParamData() {
    return ParamCursor()->data;
}

// FUNC_AT(0x000f5fe0)
uint32_t EAGL_RMPacketStride() {
    return PacketCursor()[1];
}

// FUNC_AT(0x000f5ff0)
uint8_t* EAGL_RMVariationData() {
    return VariationData();
}

// Opcodes 29, 30: skin count0 / 4 matrices into the palette scratch (clamped to the vertex count), and as many of
// count1 / 4 more as still fit, then upload them (the second block only when there is one).
// FUNC_AT(0x000f6aa0)
void EAGL_SkinAndUpload(const uint8_t *vertices, int count, int reg0, int count0, int reg1, int count1,
                        const uint8_t *palette) {
    int n0 = count0 / 4;
    int n1 = count1 / 4;
    if (n0 >= count) {
        n0 = count;
        n1 = 0;
    } else if (n1 + n0 >= count) {
        n1 = count - n0;
    }
    float *scratch = (float *)(uintptr_t)kPalette;
    for (int i = 0; i < n0; i++)
        EAGL_SkinMatrix(vertices, i, palette, scratch + i * 16);
    for (int j = 0; j < n1; j++)
        EAGL_SkinMatrix(vertices, j + n0, palette, scratch + (n0 + j) * 16);
    EAGL_SetVertexShaderConstant(reg0, scratch, count0);
    if (n1 > 0)
        EAGL_SetVertexShaderConstant(reg1, scratch + n0 * 16, count1);
}

// Unreferenced code taking EAX (an object whose +4 is a data pointer) and EDX (the output): [EDX] = [EAX + 4] +
// the first stack argument, RET 0xc. In its own instructions.
// FUNC_AT(0x000f6b90)
__declspec(naked) void EAGL_LockRegisterArgs() {
    __asm {
        mov ecx, dword ptr [eax + 4]
        add ecx, dword ptr [esp + 4]
        mov dword ptr [edx], ecx
        ret 0xc
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The opcode handlers (3.3)
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000f6010)
void EAGL_RMOp_Skip() {
    NextParam();
}

// FUNC_AT(0x000f6020)
void EAGL_RMOp_Skip1() {
    NextParam();
}

// The stream description for the variation: vertex count (+0), the user-memory flag (+8), the no-copy flag (+0xc)
// FUNC_AT(0x000f6030)
void EAGL_RMOp_Stream() {
    const uint8_t *d = VariationData();
    U32(kStreamCount) = *(const uint32_t *)d;
    U32(kStreamNoCopy) = *(const uint32_t *)(d + 0xc);
    U32(kStreamUser) = *(const uint32_t *)(d + 8);
    NextParam();
}

// The shader variant a 16-bit index in the parameter picks
// FUNC_AT(0x000f6090)
void EAGL_RMOp_SelectShaders() {
    int index = *(const int16_t *)VariationData();
    RenderMethod *method = CurrentMethod();
    PixelShader **ps = method->pixelShaders + index;
    EAGL_SetVertexShader(method->vertexShaders[index]);
    EAGL_SetPixelShader(*ps);
    NextParam();
}

// Variant 0; no parameter
// FUNC_AT(0x000f60f0)
void EAGL_RMOp_DefaultShaders() {
    RenderMethod *method = CurrentMethod();
    PixelShader **ps = method->pixelShaders;
    EAGL_SetVertexShader(method->vertexShaders[0]);
    EAGL_SetPixelShader(*ps);
}

// FUNC_AT(0x000f6120)
void EAGL_RMOp_VSConstants5() {
    VSConstantsOp();
}

// FUNC_AT(0x000f6160)
void EAGL_RMOp_VSConstants6() {
    VSConstantsOp();
}

// FUNC_AT(0x000f61a0)
void EAGL_RMOp_VSConstants7() {
    VSConstantsOp();
}

// FUNC_AT(0x000f61e0)
void EAGL_RMOp_VSConstants8() {
    VSConstantsOp();
}

// A stream source described inline in the parameter data, after packet[3] * count bytes rounded up to 4, 0x10 in
// FUNC_AT(0x000f6220)
void EAGL_RMOp_InlineStreamSource() {
    uint32_t *c = PacketCursor();
    GeoPrimParam *p = ParamCursor();
    uint32_t skip = c[3] * p->count;
    uint32_t variation = c[1] * U32(kVariation);
    uint32_t address = ((skip + 3) & 0xfffffffcu) + variation + Addr(p->data) + 0x10;
    EAGL_SetStreamSource(c[2], (const StaticVertexBuffer *)(uintptr_t)address);
    NextParam();
}

// FUNC_AT(0x000f6270)
void EAGL_RMOp_IndexData() {
    EAGL_SetIndexData(0, VariationData());
    NextParam();
}

// A TAR: its stage (packet[2]) set through its extension (+0x48), then bound
// FUNC_AT(0x000f62b0)
void EAGL_RMOp_TAR() {
    FlushIfRegistered();
    uint32_t stage = PacketCursor()[2];
    TarSetStage(VariationData() + 0x48, stage);
    TarUse(VariationData());
    NextParam();
}

// A GeoPrimState applied, and its primitive type noted for the draws
// FUNC_AT(0x000f6320)
void EAGL_RMOp_GeoPrimState() {
    ((GeoPrimState *)VariationData())->Apply();
    uint32_t type;
    ((GeoPrimState *)VariationData())->GetPrimitiveType(&type);
    U32(kPrimitive) = type;
    NextParam();
}

// FUNC_AT(0x000f6380)
void EAGL_RMOp_VSConstantPair17() {
    VSConstantPairOp();
}

// FUNC_AT(0x000f63e0)
void EAGL_RMOp_VSConstantPair18() {
    VSConstantPairOp();
}

// FUNC_AT(0x000f6440)
void EAGL_RMOp_VSMatrix19() {
    VSMatrixOp();
}

// FUNC_AT(0x000f64a0)
void EAGL_RMOp_VSMatrix20() {
    VSMatrixOp();
}

// FUNC_AT(0x000f6500)
void EAGL_RMOp_MatrixPalette21() {
    MatrixPaletteOp();
}

// FUNC_AT(0x000f6550)
void EAGL_RMOp_MatrixPalette22() {
    MatrixPaletteOp();
}

// FUNC_AT(0x000f65a0)
void EAGL_RMOp_Skip23() {
    NextParam();
}

// FUNC_AT(0x000f65b0)
void EAGL_RMOp_Skip24() {
    NextParam();
}

// DrawVertices of the stream's vertex count when packet[2] is set; no parameter
// FUNC_AT(0x000f65c0)
void EAGL_RMOp_DrawVertices() {
    FlushIfRegistered();
    if (PacketCursor()[2] != 0)
        EAGL_DrawVertices(0, U32(kStreamCount));
}

// FUNC_AT(0x000f6600)
void EAGL_RMOp_DrawIndexedVertices() {
    FlushIfRegistered();
    if (PacketCursor()[2] != 0)
        EAGL_DrawIndexedVertices(0, U32(kStreamCount));
}

// FUNC_AT(0x000f6640)
void EAGL_RMOp_VSMatrix27() {
    VSMatrixOp();
}

// FUNC_AT(0x000f66a0)
void EAGL_RMOp_VSMatrix28() {
    VSMatrixOp();
}

// The skinning source: the parameter's data for the variation and its count
// FUNC_AT(0x000f6700)
void EAGL_RMOp_SkinSource() {
    U32(kSkinSource) = Addr(VariationData());
    U32(kSkinCount) = ParamCursor()->count;
    NextParam();
}

// FUNC_AT(0x000f6730)
void EAGL_RMOp_PSMatrix32() {
    PSMatrixOp();
}

// FUNC_AT(0x000f6790)
void EAGL_RMOp_PSMatrix33() {
    PSMatrixOp();
}

// FUNC_AT(0x000f67f0)
void EAGL_RMOp_PSConstants34() {
    PSConstantsOp();
}

// FUNC_AT(0x000f6830)
void EAGL_RMOp_PSConstants35() {
    PSConstantsOp();
}

// The push buffer at packet[2] when packet[1] is set; no parameter. Never reached on the disc (docs 8.5).
// FUNC_AT(0x000f6870)
void EAGL_RMOp_RunPushBuffer() {
    uint32_t *c = PacketCursor();
    if (c[1] != 0)
        EAGL_RunPushBuffer((PushBuffer *)(uintptr_t)c[2]);
}

// The CPU vertex array into the packet's dynamic vertex buffer (packet +0x10, capacity +0x14, source +0x18),
// re-created when the count outgrows the capacity, and bound to stream packet[2]. Quirks kept: the buffer is
// created 4 times the array's size, and the capacity is stored as count * 4 against which a count is compared.
// FUNC_AT(0x000f6890)
void EAGL_RMOp_DynamicStream() {
    uint32_t *c = PacketCursor();
    uint32_t count = ParamCursor()->count;
    uint32_t stride = c[3];
    int32_t capacity = (int32_t)c[5];
    uint32_t stream = c[2];
    DynamicVertexBuffer *buffer = (DynamicVertexBuffer *)(uintptr_t)c[4];
    uint32_t bytes = count * stride;
    // The original's lock output is a stack slot that also took the allocation; with user memory Lock leaves it
    // alone, and when no buffer was made it holds whatever was there (here NULL). Only reached with flags the
    // disc does not combine (no-copy 0 and user memory set).
    void *slot = NULL;
    if (!(capacity > (int32_t)count)) {
        if (Addr(buffer) != 0xffffffffu && buffer != NULL) {
            buffer->Destruct();
            EaglFree(buffer, 0x30);
        }
        if (U32(kStreamUser) != 0) {
            slot = EaglMalloc(0x30, kNameVBNew);
            buffer = slot != NULL
                ? ((DynamicVertexBuffer *)slot)->ConstructUser(ParamCursor()->data, bytes * 4, stride) : NULL;
        } else {
            slot = EaglMalloc(0x30, kNameVBNew);
            buffer = slot != NULL ? ((DynamicVertexBuffer *)slot)->Construct(bytes * 4, stride) : NULL;
        }
        PacketCursor()[4] = Addr(buffer);
        PacketCursor()[5] = ParamCursor()->count << 2;
        PacketCursor()[6] = Addr(ParamCursor()->data);
    }
    if (U32(kStreamNoCopy) == 0) {
        buffer->Lock(0, bytes, &slot);
        MEM_copy(slot, ParamCursor()->data, (int)bytes);
        buffer->Unlock();
    }
    EAGL_SetDynamicStreamSource(stream, buffer);
    NextParam();
}

// FUNC_AT(0x000f6a00)
void EAGL_RMOp_Skin29() {
    SkinOp();
}

// FUNC_AT(0x000f6a50)
void EAGL_RMOp_Skin30() {
    SkinOp();
}

// ---------------------------------------------------------------------------------------------------------------
// Vertex buffers, index buffers, shaders, push buffers
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000f6b80)
void EAGL::StaticVertexBuffer::Nop6b80(uint32_t) {
}

// FUNC_AT(0x000f6ba0)
void EAGL::StaticVertexBuffer::Nop6ba0(uint32_t) {
}

// FUNC_AT(0x000f6bb0)
EAGL::StaticVertexBuffer* EAGL::StaticVertexBuffer::Construct(uint32_t size_, uint32_t stride_) {
    stride = stride_;
    size = size_;
    user = 0;
    unk10 = 0;
    buffer = D3D_CreateVertexBuffer2(size_);
    return this;
}

// The user-memory form: a 0xc-byte stand-in header; the third argument is not used.
// FUNC_AT(0x000f6be0)
EAGL::StaticVertexBuffer* EAGL::StaticVertexBuffer::ConstructUser(uint32_t a, uint32_t b, uint32_t) {
    size = a;
    stride = b;
    user = 1;
    unk10 = 0;
    buffer = EaglMalloc(0xc, kNameD3DVB);
    return this;
}

// FUNC_AT(0x000f6c20)
void EAGL::StaticVertexBuffer::Destruct() {
    for (uint32_t stream = 0; stream < 16; stream++) {
        if (U32(kStreams + stream * 4) == Addr(buffer)) {
            U32(kStreams + stream * 4) = 0;
            D3D_SetStreamSource(stream, NULL, 0);
        }
    }
    D3D_BlockUntilNotBusy(buffer);
    if (user != 0) {
        EaglFree(buffer, 0xc);
        buffer = NULL;
        return;
    }
    D3D_Release(buffer);
    buffer = NULL;
}

// FUNC_AT(0x000f6c90)
void EAGL::StaticVertexBuffer::Nop6c90(uint32_t) {
}

// FUNC_AT(0x000f6ca0)
uint32_t EAGL::StaticVertexBuffer::GetSize() {
    return size;
}

// FUNC_AT(0x000f6cb0)
uint32_t EAGL::StaticVertexBuffer::GetStride() {
    return stride;
}

// FUNC_AT(0x000f6cc0)
void EAGL::StaticVertexBuffer::Lock(uint32_t offset, uint32_t, uint8_t **out) {
    *out = D3D_VertexBufferLock2(buffer, 0) + offset;
}

// FUNC_AT(0x000f6ce0)
void EAGL::StaticVertexBuffer::Unlock() {
}

// FUNC_AT(0x000f6cf0)
bool EAGL::StaticVertexBuffer::IsBusy() {
    return D3D_IsBusy(buffer) != 0;
}

// FUNC_AT(0x000f6d00)
EAGL::DynamicVertexBuffer* EAGL::DynamicVertexBuffer::Construct(uint32_t size_, uint32_t stride_) {
    size = size_;
    stride = stride_;
    user = 0;
    unk2c = 0;
    buffers[0] = D3D_CreateVertexBuffer2(size_);
    buffers[1] = D3D_CreateVertexBuffer2(size);
    buffers[2] = D3D_CreateVertexBuffer2(size);
    current = buffers[0];
    index = 0;
    return this;
}

// Over the caller's memory: a vertex buffer header whose data is the pointer's physical form (+0x80000000, which
// the seam's XGSetVertexBufferHeader takes back off)
// FUNC_AT(0x000f6d50)
EAGL::DynamicVertexBuffer* EAGL::DynamicVertexBuffer::ConstructUser(void *data, uint32_t size_, uint32_t stride_) {
    stride = stride_;
    size = size_;
    user = 1;
    unk2c = 0;
    userHeader[0] = 0;
    userHeader[1] = 0;
    userHeader[2] = 0;
    XG_SetVertexBufferHeader(0, 0, 0, 0, userHeader, Addr(data) + 0x80000000u);
    current = userHeader;
    return this;
}

// FUNC_AT(0x000f6da0)
void EAGL::DynamicVertexBuffer::Destruct() {
    if (user != 0) {
        D3D_BlockUntilNotBusy(userHeader);
        userHeader[0] = 0;
        userHeader[1] = 0;
        userHeader[2] = 0;
        return;
    }
    for (uint32_t stream = 0; stream < 16; stream++) {
        if (U32(kStreams + stream * 4) == Addr(buffers[0])) {
            U32(kStreams + stream * 4) = 0;
            D3D_SetStreamSource(stream, NULL, 0);
        }
        if (U32(kStreams + stream * 4) == Addr(buffers[1])) {
            U32(kStreams + stream * 4) = 0;
            D3D_SetStreamSource(stream, NULL, 0);
        }
        if (U32(kStreams + stream * 4) == Addr(buffers[2])) {
            U32(kStreams + stream * 4) = 0;
            D3D_SetStreamSource(stream, NULL, 0);
        }
    }
    D3D_BlockUntilNotBusy(buffers[0]);
    D3D_BlockUntilNotBusy(buffers[1]);
    D3D_BlockUntilNotBusy(buffers[2]);
    D3D_Release(buffers[0]);
    D3D_Release(buffers[1]);
    D3D_Release(buffers[2]);
    current = NULL;
    buffers[0] = NULL;
    buffers[1] = NULL;
    buffers[2] = NULL;
}

// FUNC_AT(0x000f6e70)
uint32_t EAGL::DynamicVertexBuffer::GetSize() {
    return size;
}

// FUNC_AT(0x000f6e80)
uint32_t EAGL::DynamicVertexBuffer::GetStride() {
    return stride;
}

// The next of the three buffers (signed modulo), locked. Over user memory: nothing, and *out is not written.
// FUNC_AT(0x000f6e90)
void EAGL::DynamicVertexBuffer::Lock(uint32_t offset, uint32_t, void **out) {
    if (user != 0)
        return;
    int next = (index + 1) % 3;
    index = next;
    current = buffers[next];
    *out = D3D_VertexBufferLock2(buffers[next], 0) + offset;
}

// FUNC_AT(0x000f6ed0)
void EAGL::DynamicVertexBuffer::Unlock() {
}

// FUNC_AT(0x000f6ee0)
bool EAGL::DynamicVertexBuffer::IsBusy() {
    if (user != 0)
        return false;
    return D3D_IsBusy(current) != 0;
}

// FUNC_AT(0x000f6f00)
EAGL::IndexBuffer* EAGL::IndexBuffer::Construct(uint32_t size_) {
    size = size_;
    user = 0;
    buffer = D3D_CreateIndexBuffer2(size_);
    return this;
}

// The user-memory form leaves the buffer pointer as it was.
// FUNC_AT(0x000f6f20)
EAGL::IndexBuffer* EAGL::IndexBuffer::ConstructUser(uint32_t size_, uint32_t) {
    size = size_;
    user = 1;
    return this;
}

// FUNC_AT(0x000f6f30)
void EAGL::IndexBuffer::Destruct() {
    if (user == 0)
        D3D_Release(buffer);
    buffer = NULL;
}

// FUNC_AT(0x000f6f50)
uint32_t EAGL::IndexBuffer::GetSize() {
    return size;
}

// The resource's data pointer (+4) plus the offset
// FUNC_AT(0x000f6f60)
void EAGL::IndexBuffer::Lock(uint32_t offset, uint32_t, uint8_t **out) {
    *out = (uint8_t *)(uintptr_t)(((uint32_t *)buffer)[1] + offset);
}

// FUNC_AT(0x000f6f80)
void EAGL::IndexBuffer::Unlock() {
}

// FUNC_AT(0x000f6f90)
bool EAGL::IndexBuffer::IsBusy() {
    return D3D_IsBusy(buffer) != 0;
}

// FUNC_AT(0x000f6fa0)
EAGL::VertexShader* EAGL::VertexShader::Construct(const void *declaration, const void *function) {
    D3D_CreateVertexShader(declaration, function, &handle, 0);
    return this;
}

// FUNC_AT(0x000f6fc0)
void EAGL::VertexShader::Destruct() {
    if (U32(kVertexShaderInUse) == handle) {
        D3D_SetVertexShader(0);
        U32(kVertexShaderInUse) = 0;
    }
    D3D_DeleteVertexShader(handle);
}

// FUNC_AT(0x000f6ff0)
EAGL::PixelShader* EAGL::PixelShader::Construct(const void *definition) {
    D3D_CreatePixelShader(definition, &handle);
    return this;
}

// FUNC_AT(0x000f7010)
void EAGL::PixelShader::Destruct() {
    if (U32(kPixelShaderInUse) == handle) {
        D3D_SetPixelShader(0);
        U32(kPixelShaderInUse) = 0;
    }
    D3D_DeletePixelShader(handle);
}

// A push buffer header over existing memory, registered against base and run at once (unreferenced). The header's
// words are written as the original writes them, Common included.
// FUNC_AT(0x000f7040)
EAGL::PushBuffer* EAGL::PushBuffer::Construct(uint32_t a, uint32_t b, uint32_t c, uint32_t base) {
    unk1c = a;
    notOwned = 1;
    unk20 = b;
    unk18 = c;
    header[0] = 0;
    header[1] = 0;
    header[2] = 0;
    header[3] = 0;
    header[4] = 0;
    header[3] = a;
    header[0] = c;
    header[4] = b;
    D3D_Register(header, (void *)(uintptr_t)base);
    D3D_RunPushBuffer(header, NULL);
    return this;
}

// FUNC_AT(0x000f70a0)
void EAGL::PushBuffer::Destruct() {
    if (notOwned == 0)
        D3D_Release(header);
}
