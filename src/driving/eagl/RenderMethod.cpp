#include "RenderMethod.h"

#include "EaglGlobals.h"
#include "EaglOriginals.h"
#include "Loader.h"
#include "Model.h"
#include "Tar.h"
#include "Transform.h"
#include "View.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"
#include "../platform/X87.h"
#include "../../helpers.h"

#include <emmintrin.h>
#include <stddef.h>
#include <string.h>
#include <xmmintrin.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGL's render methods (docs/driving/eagl.md 2.8, 2.9, 3.3, 4.6). A RenderMethod is a packet stream - a header
// dword (opcode << 16 | length in dwords) and operands, ended by a zero header - plus its shader variants.
// RenderMethod::Draw runs the stream over a GeoPrim: it keeps the method, the GeoPrim's parameter cursor and the
// packet cursor in three globals (Interp, at 0x002401bc) and calls each packet's handler through the opcode table
// at 0x001ce700. The table is left as it is (it points at the originals' addresses, which jump to the handlers
// here), so a handler stays patchable. Handlers take no arguments; most read one 8-byte parameter for the current
// variation (parameter.data + packet.variationStride * CurrentVariation) and step the parameter cursor.
//
// Every D3D8 call goes to the seam (../gfx/D3D8.h), with the original's convention: stdcall, except
// SetVertexShaderConstant1/4 (ECX register, EDX data) and NotInline (the same plus the dword count on the stack). The direct writes into D3D8's state are the stream source and shader caches EAGL keeps
// itself (Cache, 0x00240470..0x00240508); this module writes none of D3D8's own tables.
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

// ---- the interpreter's globals

// The interpreter's state, at 0x002401bc: Draw's method and cursors, and opcode 2's stream description
struct InterpreterState {
    RenderMethod *method;            // +0x00 the render method running
    GeoPrimParam *param;             // +0x04 the parameter cursor
    Packet *packet;                  // +0x08 the packet cursor
    uint32_t streamVertexCount;      // +0x0c the current stream (opcode 2): vertex count
    uint32_t streamNoCopy;           // +0x10   nonzero: opcode 11 does not copy the vertices
    uint32_t streamUser;             // +0x14   nonzero: opcode 11 draws from the caller's memory
};
static_assert(sizeof(InterpreterState) == 0x18, "the interpreter's globals run to 0x002401d4");

// What EAGL last sent D3D8, at 0x00240470: the wrappers skip a call that would send the same again
struct DeviceCache {
    uint32_t primitiveType;          // +0x00 opcode 16's, for the draws
    uint32_t pixelShader;            // +0x04 the handles in use
    uint32_t vertexShader;           // +0x08
    uint32_t unknown0c;              // +0x0c
    D3DResource *streams[16];        // +0x10 the stream sources
    uint32_t strides[16];            // +0x50 their strides
    const uint16_t *indexData;       // +0x90 opcode 14's index data
    uint32_t indexBase;              // +0x94
};
static_assert(offsetof(DeviceCache, streams) == 0x10, "the stream sources are at 0x00240480");
static_assert(offsetof(DeviceCache, strides) == 0x50, "the strides are at 0x002404c0");
static_assert(offsetof(DeviceCache, indexData) == 0x90, "the index data is at 0x00240500");
static_assert(sizeof(DeviceCache) == 0x98, "the device cache runs to 0x00240508");

#define Interp (*(InterpreterState *)0x002401bc)
#define Cache (*(DeviceCache *)0x00240470)
#define SkinSource (*(const EAGL::SkinVertex **)0x00240804)   // opcode 31's skinning records
#define SkinCount I32_AT(0x00240808)                        //   and their count
#define PaletteScratch ((float *)0x00240830)                // the matrix palette scratch, 16 floats a matrix
#define D3DPushBufferCursor (*(const uint16_t **)0x00175420)   // D3D8's push-buffer pointer

// The handlers, called by opcode
typedef void (*OpcodeHandler)();
#define OpcodeTable ((const OpcodeHandler *)0x001ce700)

// The original's allocation names, passed by address as it does (the allocator hook is given the pointer; the
// shader objects' are EaglGlobals.h's)
#define NamePCode ((const char *)0x001cda70)     // "EAGL::RenderMethod(RenderMethod &parent) allocating PCode block"
#define NameVSArray ((const char *)0x001cdb00)   // "EAGL::VertexShader *"
#define NamePSArray ((const char *)0x001cdb18)   // "EAGL::PixelShader *"
#define NameUserVar ((const char *)0x001ce3a0)   // "mUserVarData"
#define NameVBNew ((const char *)0x001ce798)     // "EAGL::VertexBuffer new"
#define NameD3DVB ((const char *)0x001ce7b0)     // "IDirect3DVertexBuffer8"

// DrawGouraud's and DrawTextured's shaders, in the original's data
#define GouraudVSDeclaration ((const void *)0x001ce218)
#define GouraudVSFunction ((const void *)0x001ce228)
#define GouraudPSDefinition ((const void *)0x001ce2b0)
#define TexturedVSDeclaration ((const void *)0x001ce078)
#define TexturedVSFunction ((const void *)0x001ce090)
#define TexturedPSDefinition ((const void *)0x001ce128)

// An opcode-11 packet's slots while it has no buffer: all bits set
template <class T> T *Unset() {
    return reinterpret_cast<T *>(0xffffffff);
}

// ---- the interpreter's state

template <class T> T *CurrentPacket() {
    return static_cast<T *>(Interp.packet);
}
// parameter.data + packet.variationStride * CurrentVariation: the current parameter's data for this variation
template <class T = uint8_t> T *VariationData() {
    const ParamPacket *packet = CurrentPacket<ParamPacket>();
    return reinterpret_cast<T *>(Interp.param->data + packet->variationStride * CurrentVariation);
}
inline void NextParam() {
    Interp.param++;
}
// The flush the draw handlers do first: WBINVD in the original (NOPed by XboxStartup.cpp), then the flag cleared.
inline void FlushIfRegistered() {
    if (ResourceRegistered != 0)
        ResourceRegistered = 0;
}

// Opcodes 5-8: count >> 2 constant registers (SAR) from the parameter at packet.reg
inline void VSConstantsOp() {
    const ConstantsPacket *packet = CurrentPacket<ConstantsPacket>();
    EAGL_SetVertexShaderConstant(packet->reg, VariationData(), packet->count >> 2);
    NextParam();
}

// Opcodes 17, 18: two constant blocks from one parameter, the second packet.count registers on
inline void VSConstantPairOp() {
    const ConstantsPacket *packet = CurrentPacket<ConstantsPacket>();
    int reg1 = packet->reg1, count1 = packet->count1, reg0 = packet->reg;
    uint8_t *data = VariationData();
    int count0 = packet->count;
    EAGL_SetVertexShaderConstant(reg0, data, count0);
    EAGL_SetVertexShaderConstant(reg1, data + (count0 << 4), count1);
    NextParam();
}

// Opcodes 19, 20, 27, 28: a matrix, transposed, into 4 vertex shader constants
inline void VSMatrixOp() {
    alignas(16) float t[16];
    int reg = CurrentPacket<ConstantsPacket>()->reg;
    VU0_MATRIX4_transpose(t, VariationData());
    EAGL_SetVertexShaderConstant(reg, t, 4);
    NextParam();
}

// Opcodes 21, 22: the matrix palette
inline void MatrixPaletteOp() {
    const ConstantsPacket *packet = CurrentPacket<ConstantsPacket>();
    EAGL_UploadMatrixPalette(packet->reg, packet->count, packet->reg1, packet->count1, VariationData<float>());
    NextParam();
}

// Opcodes 29, 30: CPU skinning from the source opcode 31 set
inline void SkinOp() {
    const ConstantsPacket *packet = CurrentPacket<ConstantsPacket>();
    EAGL_SkinAndUpload(SkinSource, SkinCount, packet->reg, packet->count, packet->reg1, packet->count1,
                       VariationData<float>());
    NextParam();
}

// Opcodes 32, 33: a matrix, transposed, into 4 pixel shader constants
inline void PSMatrixOp() {
    alignas(16) float t[16];
    uint32_t reg = CurrentPacket<ConstantsPacket>()->reg;
    VU0_MATRIX4_transpose(t, VariationData());
    EAGL_SetPixelShaderConstant(reg, t, 4);
    NextParam();
}

// Opcodes 34, 35: count >> 2 pixel shader constants (SAR)
inline void PSConstantsOp() {
    const ConstantsPacket *packet = CurrentPacket<ConstantsPacket>();
    EAGL_SetPixelShaderConstant(packet->reg, VariationData(), packet->count >> 2);
    NextParam();
}

// An identity matrix, as DrawGouraud::Init and DrawTextured::Init store it
inline void Identity(float *m) {
    for (int i = 0; i < 16; i++)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

// Every stream source that is this buffer unset (the destructors')
inline void UnbindStreams(const D3DResource *buffer) {
    for (uint32_t stream = 0; stream < 16; stream++) {
        if (Cache.streams[stream] == buffer) {
            Cache.streams[stream] = NULL;
            D3DDevice_SetStreamSource(stream, NULL, 0);
        }
    }
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
    const Packet *source = parent->packets;
    uint32_t n = 0, length;
    do {
        length = source[n].Length();
        n += length;
    } while (length != 0);
    uint32_t bytes = n * 4 + 4;
    uint32_t block = bytes + 4;
    uint32_t *memory = static_cast<uint32_t *>(EaglMalloc(block, NamePCode));
    method->packets = reinterpret_cast<Packet *>(memory);
    memory[0] = block;
    method->packets = reinterpret_cast<Packet *>(memory + 1);
    MEM_copy(memory + 1, parent->packets, bytes);
}

// The loader's callback for a VertexBuffer symbol: point the D3D8 header at the data and register it against the
// image (D3DResource_Register adds the base).
// FUNC_AT(0x000f0ee0)
void EAGL_VertexBufferConstructor(EAGL::LoadedVertexBuffer *buffer, DynamicLoader *loader) {
    uint32_t offset = buffer->fileOffset;
    void *base = loader->GetElfData();
    buffer->header->data = offset - reinterpret_cast<uintptr_t>(base);
    D3DResource_Register(buffer->header, base);
    ResourceRegistered = 1;
}

// FUNC_AT(0x000f0f10)
void EAGL_VertexBufferDestructor(EAGL::LoadedVertexBuffer *buffer) {
    UnbindStreams(buffer->header);
    D3DResource_BlockUntilNotBusy(buffer->header);
}

// The static initialisers' constructor (bondrm render methods the game builds in code)
// FUNC_AT(0x000f0f60)
EAGL::RenderMethod* EAGL::RenderMethod::Construct(Packet *packets_, int numVariants_, const void *declaration_,
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
    next = ConstructedMethods;
    ConstructedMethods = this;
    return this;
}

// The interpreter (3.3). The length is the header's, read before the handler runs; the cursor is re-read after.
// FUNC_AT(0x000f0fc0)
void EAGL::RenderMethod::Draw(GeoPrim *prim) {
    Interp.method = this;
    Packet *cursor = packets;
    Interp.packet = cursor;
    Interp.param = prim->params;
    uint32_t header = cursor->header;
    while (header != 0) {
        uint32_t length = header & 0xffff;
        // No bounds check: an opcode above 36 calls whatever follows the table, as the original does
        OpcodeTable[header >> 16]();
        cursor = Interp.packet + length;
        Interp.packet = cursor;
        header = cursor->header;
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
    vertexShaders = static_cast<VertexShader **>(EaglMalloc(numVariants << 2, NameVSArray));
    pixelShaders = static_cast<PixelShader **>(EaglMalloc(numVariants * 4, NamePSArray));
    for (int i = 0; i < numVariants; i++) {
        VertexShader *vs = static_cast<VertexShader *>(EaglMalloc(4, NameVertexShaderNew));
        vs = vs != NULL ? vs->Construct(declaration, vsMicrocode[i]) : NULL;
        vertexShaders[i] = vs;
        PixelShader *ps = static_cast<PixelShader *>(EaglMalloc(4, NamePixelShaderNew));
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

// Every opcode-11 packet's dynamic vertex buffer destroyed and its three slots reset to all ones. A packet of
// length 0 with a nonzero header would loop forever, as in the original.
// FUNC_AT(0x000f1210)
void EAGL_ReleaseDynamicBuffers(EAGL::RenderMethod *method) {
    Packet *p = method->packets;
    if (p->header == 0)
        return;
    do {
        uint32_t length = p->Length();
        if (p->Opcode() == kOpDynamicStream) {
            DynamicStreamPacket *packet = static_cast<DynamicStreamPacket *>(p);
            if (packet->buffer != Unset<DynamicVertexBuffer>()) {
                DynamicVertexBuffer *buffer = packet->buffer;
                if (buffer != NULL) {
                    buffer->Destruct();
                    EaglFree(buffer, sizeof(DynamicVertexBuffer));
                }
                packet->buffer = Unset<DynamicVertexBuffer>();
                packet->capacity = -1;
                packet->source = Unset<uint8_t>();
            }
        }
        p += length;
    } while (p->header != 0);
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
    next = WaitingMethods;
    WaitingMethods = this;
    return this;
}

// A child frees its packet copy (after its dynamic buffers) by the size word before it; any other deletes its
// shader arrays with __builtin_delete, though they came from EAGLMalloc (the original's mismatch, kept).
// FUNC_AT(0x000f12c0)
void EAGL::RenderMethod::Destruct() {
    if (cloned != 0) {
        EAGL_ReleaseDynamicBuffers(this);
        packets = packets - 1;
        uint32_t *block = &packets->header;
        EaglFree(block, *block);
        return;
    }
    OperatorDelete(vertexShaders);
    OperatorDelete(pixelShaders);
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
    EaglFree(vertexShaders, numVariants << 2);
    EaglFree(pixelShaders, numVariants << 2);
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

// a3 a4 / a6 a7 ... the 3x3 determinant of nine floats on the stack, by the first row's cofactors (x87 order, all
// in double: each product of two floats is exact there)
// FUNC_AT(0x000f1490)
double EAGL_Determinant3x3(float a0, float a1, float a2, float a3, float a4, float a5, float a6, float a7,
                           float a8) {
    double r = (double(a3) * a7 - double(a4) * a6) * a2;
    r = r + (double(a5) * a6 - double(a3) * a8) * a1;
    r = r + (double(a4) * a8 - double(a5) * a7) * a0;
    return r;
}

// FUNC_AT(0x000f14e0)
void* EAGL::RenderMatrix::MultiplyInto(const float *rhs, float *out) {
    return VU0_MATRIX4_mult(out, m, rhs);
}

// ---------------------------------------------------------------------------------------------------------------
// The D3D8 wrappers (0x000f4340..0x000f4580). The stream source and shader wrappers skip a call when the value is
// the one they last sent (Cache).
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000f4340)
void EAGL_RunPushBuffer(EAGL::PushBuffer *buffer) {
    D3DDevice_RunPushBuffer(&buffer->header, NULL);
}

// FUNC_AT(0x000f4350)
void EAGL_SetVertexShader(const EAGL::VertexShader *shader) {
    if (Cache.vertexShader == shader->handle)
        return;
    Cache.vertexShader = shader->handle;
    D3DDevice_SetVertexShader(shader->handle);
}

// FUNC_AT(0x000f4380)
void EAGL_SetPixelShader(const EAGL::PixelShader *shader) {
    if (Cache.pixelShader == shader->handle)
        return;
    Cache.pixelShader = shader->handle;
    D3DDevice_SetPixelShader(shader->handle);
}

// The register is biased by 96 as D3D8's inline functions do (constants -96..95 -> 0..191). count 1 and 4 go
// through the inline entry points, anything else through NotInline with a dword count (count 0 included).
// FUNC_AT(0x000f43a0)
void EAGL_SetVertexShaderConstant(int reg, const void *data, int count) {
    if (count == 1)
        D3DDevice_SetVertexShaderConstant1(reg + 0x60, data);
    else if (count == 4)
        D3DDevice_SetVertexShaderConstant4(reg + 0x60, data);
    else
        D3DDevice_SetVertexShaderConstantNotInline(reg + 0x60, data, count << 2);
}

// FUNC_AT(0x000f43e0)
void EAGL_SetStreamSource(uint32_t stream, const EAGL::StaticVertexBuffer *buffer) {
    if (Cache.streams[stream] == buffer->buffer && Cache.strides[stream] == buffer->stride)
        return;
    Cache.streams[stream] = buffer->buffer;
    Cache.strides[stream] = buffer->stride;
    D3DDevice_SetStreamSource(stream, buffer->buffer, buffer->stride);
}

// FUNC_AT(0x000f4420)
void EAGL_SetDynamicStreamSource(uint32_t stream, const EAGL::DynamicVertexBuffer *buffer) {
    if (Cache.streams[stream] == buffer->current && Cache.strides[stream] == buffer->stride)
        return;
    Cache.streams[stream] = buffer->current;
    Cache.strides[stream] = buffer->stride;
    D3DDevice_SetStreamSource(stream, buffer->current, buffer->stride);
}

// FUNC_AT(0x000f4460)
void EAGL_SetIndices(uint32_t baseVertex, const EAGL::IndexBuffer *buffer) {
    D3DDevice_SetIndices(buffer->buffer, baseVertex);
}

// Opcode 14's: the index data DrawIndexedVertices reads (no D3D call)
// FUNC_AT(0x000f4480)
void EAGL_SetIndexData(uint32_t base, const void *indices) {
    Cache.indexData = static_cast<const uint16_t *>(indices);
    Cache.indexBase = base;
}

// FUNC_AT(0x000f44a0)
void EAGL_SetPixelShaderConstant(uint32_t reg, const void *data, uint32_t count) {
    D3DDevice_SetPixelShaderConstant(reg, data, count);
}

// FUNC_AT(0x000f44c0)
void EAGL_DrawVertices(uint32_t start, uint32_t count) {
    D3DDevice_DrawVertices(Cache.primitiveType, start, count);
}

// Unreferenced: indices from D3D8's push-buffer pointer instead of the stored index data.
// FUNC_AT(0x000f44e0)
void EAGL_DrawIndexedVerticesPushBuffer(uint32_t, uint32_t, uint32_t start, uint32_t count) {
    D3DDevice_DrawIndexedVertices(Cache.primitiveType, count, D3DPushBufferCursor + start);
}

// FUNC_AT(0x000f4500)
void EAGL_DrawIndexedVertices(uint32_t start, uint32_t count) {
    D3DDevice_DrawIndexedVertices(Cache.primitiveType, count, Cache.indexData + start);
}

// 0x000f4520 ("rdtsc; ret") is XboxTimer.cpp's: it redirects that one already.

// CVTSS2SI: rounded by MXCSR (round to nearest, the default)
// FUNC_AT(0x000f4530)
int EAGL_RoundToInt(float value) {
    return RoundToInt(value);
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
    model->AddGeoPrim(prim);
    paramCount = 0;
    while (geoPrim->method->paramNames[paramCount] != NULL)
        paramCount++;
    userVars = static_cast<UserVar *>(EaglMalloc(paramCount << 3, NameUserVar));
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
    unknown24 = 0xffff - primitiveClass;
    maxVerts = 0xffff;
    unknown28 = 0xffff;
    primitiveType = type;
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
    if (count != 0xffffffff)
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
    model->SetModelMatrix(matrix);
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
            params[i].count = verts;
    }
    if (streamParam != -1) {
        stream.vertexCount = verts;
        stream.unknown04 = 0;
        stream.user = 0;
        stream.noCopy = dirty == 0 ? 1 : 0;
        params[streamParam].data = reinterpret_cast<uint8_t *>(&stream);
        params[streamParam].count = 1;
    }
}

// FUNC_AT(0x000f5c30)
void EAGL::DrawArray::Destruct() {
    if (locked != 0)
        locked = 0;
    if (userVars != NULL)
        EaglFree(userVars, paramCount << 3);
    DynamicModel *dynamicModel = model;
    if (dynamicModel != NULL) {
        dynamicModel->Destruct();
        EaglFree(dynamicModel, 0x58);
    }
}

// FUNC_AT(0x000f5c80)
bool EAGL::DrawArray::SetVarByName(const char *name_, uint8_t *data, int mode, uint32_t count) {
    return SetVar(GetIndexFromName(name_), data, mode, count);
}

// FUNC_AT(0x000f5cb0)
bool EAGL::DrawArray::Draw(int count) {
    numVerts = count;
    if (locked != 0)
        return false;
    drawVerts = count;
    SetUpGeoPrim(0);
    model->Draw();
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
    VertexShader *vs = static_cast<VertexShader *>(EaglMalloc(4, NameVertexShaderNew));
    vs = vs != NULL ? vs->Construct(GouraudVSDeclaration, GouraudVSFunction) : NULL;
    vertexShader = vs;
    PixelShader *ps = static_cast<PixelShader *>(EaglMalloc(4, NamePixelShaderNew));
    ps = ps != NULL ? ps->Construct(GouraudPSDefinition) : NULL;
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
    D3DDevice_End();
    D3DDevice_Begin(primitiveType);
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
    state.SetTextureCoordType(0xffffffff);
    state.SetShading(1);
    state.SetAlphaBlendMode(0);
    state.SetCullEnable(false);
    VertexShader *vs = static_cast<VertexShader *>(EaglMalloc(4, NameVertexShaderNew));
    vs = vs != NULL ? vs->Construct(TexturedVSDeclaration, TexturedVSFunction) : NULL;
    vertexShader = vs;
    PixelShader *ps = static_cast<PixelShader *>(EaglMalloc(4, NamePixelShaderNew));
    ps = ps != NULL ? ps->Construct(TexturedPSDefinition) : NULL;
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
    D3DDevice_End();
    D3DDevice_Begin(primitiveType);
}

// FUNC_AT(0x000f5c00)
void EAGL::DrawTextured::SetTAR(TAR *tar_) {
    if (begun != 0) {
        D3DDevice_End();
        D3DDevice_Begin(primitiveType);
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
    if (Device::Get() != NULL) {
        ViewPort *view;
        if (Device::Get()->GetCurrentTextureRenderContext() != NULL)
            view = Device::Get()->GetCurrentTextureRenderContext()->GetCurrentViewPort();
        else
            view = Device::Get()->GetCurrentRenderContext()->GetCurrentViewPort();
        const float *viewProjection = view->GetViewProjectionMatrix();
        alignas(16) Transform t;
        t.BuildMatrix(viewProjection);
        t.PrependMatrix(matrix);
        t.Transpose();
        EAGL_SetVertexShaderConstant(0, t.m, 4);
    }
    state.Apply();
    if (tar != NULL)
        tar->Use();
    D3DDevice_Begin(primitiveType);
    begun = 1;
}

// ---------------------------------------------------------------------------------------------------------------
// Helpers of the handlers
// ---------------------------------------------------------------------------------------------------------------

// Model::Call's: before and after a morph, wait for the GPU when the name is "PreMorph" and the device exists.
// It ignores its `this`, so stdcall (the same bytes popped).
// FUNC_AT(0x000f5d70)
void __stdcall EAGL_ModelCallFence(const char *name) {
    if (strcmp(name, "PreMorph") == 0 && D3DDevicePointer != NULL)
        D3DDevice_BlockOnFence(D3DDevice_InsertFence());
    ResourceRegistered = 1;
}

// FUNC_AT(0x000f5de0)
bool __stdcall EAGL_ReturnFalse(uint32_t) {
    return false;
}

// Opcodes 21, 22: count0 / 4 and count1 / 4 matrices (signed division, toward zero) transposed into the palette
// scratch, then both blocks uploaded - the second even when count1 is 0 (NotInline with 0 dwords).
// FUNC_AT(0x000f5df0)
void EAGL_UploadMatrixPalette(int reg0, int count0, int reg1, int count1, const float *matrices) {
    int n0 = count0 / 4;
    int n1 = count1 / 4;
    float *palette = PaletteScratch;
    for (int i = 0; i < n0; i++)
        VU0_MATRIX4_transpose(palette + i * 16, matrices + i * 16);
    for (int j = 0; j < n1; j++)
        VU0_MATRIX4_transpose(palette + (n0 + j) * 16, matrices + (n0 + j) * 16);
    EAGL_SetVertexShaderConstant(reg0, palette, count0);
    EAGL_SetVertexShaderConstant(reg1, palette + n0 * 16, count1);
}

// One skinned matrix: up to three palette matrices blended by weight. A 16-byte vertex record holds three weights
// as floats, and each weight's low byte is also its bone index (the bit pattern's low mantissa byte). The second
// and third count only when their dword is nonzero (the third only after the second). SSE as the original: the
// rows of the first matrix times the first weight, then each further row times its weight added, lane for lane.
// The first weight goes through the x87 (fld/fst) before its index is taken, so a signalling NaN would be quieted
// first; that is reproduced. The weights are loaded from their bits with MOVSS as the original does (hence the
// float views of the integers).
// FUNC_AT(0x000f5eb0)
void EAGL_SkinMatrix(const EAGL::SkinVertex *vertices, int index, const float *palette, float *out) {
    const SkinVertex &v = vertices[index];
    uint32_t w0 = v.weights[0], w1 = v.weights[1], w2 = v.weights[2];
    if ((w0 & 0x7f800000) == 0x7f800000 && (w0 & 0x007fffff) != 0 && (w0 & 0x00400000) == 0)
        w0 |= 0x00400000;   // FLD of a signalling NaN
    const float *m0 = palette + ((w0 & 0xff) << 4);
    const float *m1 = palette + ((w1 & 0xff) << 4);
    const float *m2 = palette + ((w2 & 0xff) << 4);
    __m128 r0 = _mm_load_ps(m0);
    __m128 r1 = _mm_load_ps(m0 + 4);
    __m128 r2 = _mm_load_ps(m0 + 8);
    __m128 r3 = _mm_load_ps(m0 + 12);
    __m128 s = _mm_load_ss(reinterpret_cast<const float *>(&w0));
    s = _mm_shuffle_ps(s, s, 0);
    r0 = _mm_mul_ps(r0, s);
    r1 = _mm_mul_ps(r1, s);
    r2 = _mm_mul_ps(r2, s);
    r3 = _mm_mul_ps(r3, s);
    if (w1 != 0) {
        s = _mm_load_ss(reinterpret_cast<const float *>(&w1));
        s = _mm_shuffle_ps(s, s, 0);
        r0 = _mm_add_ps(r0, _mm_mul_ps(_mm_load_ps(m1), s));
        r1 = _mm_add_ps(r1, _mm_mul_ps(_mm_load_ps(m1 + 4), s));
        r2 = _mm_add_ps(r2, _mm_mul_ps(_mm_load_ps(m1 + 8), s));
        r3 = _mm_add_ps(r3, _mm_mul_ps(_mm_load_ps(m1 + 12), s));
        if (w2 != 0) {
            s = _mm_load_ss(reinterpret_cast<const float *>(&w2));
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
    return Interp.param->data;
}

// FUNC_AT(0x000f5fe0)
uint32_t EAGL_RMPacketStride() {
    return CurrentPacket<ParamPacket>()->variationStride;
}

// FUNC_AT(0x000f5ff0)
uint8_t* EAGL_RMVariationData() {
    return VariationData();
}

// Opcodes 29, 30: skin count0 / 4 matrices into the palette scratch (clamped to the vertex count), and as many of
// count1 / 4 more as still fit, then upload them (the second block only when there is one).
// FUNC_AT(0x000f6aa0)
void EAGL_SkinAndUpload(const EAGL::SkinVertex *vertices, int count, int reg0, int count0, int reg1, int count1,
                        const float *palette) {
    int n0 = count0 / 4;
    int n1 = count1 / 4;
    if (n0 >= count) {
        n0 = count;
        n1 = 0;
    } else if (n1 + n0 >= count) {
        n1 = count - n0;
    }
    float *scratch = PaletteScratch;
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

// The stream description for the variation
// FUNC_AT(0x000f6030)
void EAGL_RMOp_Stream() {
    const StreamDescription *description = VariationData<StreamDescription>();
    Interp.streamVertexCount = description->vertexCount;
    Interp.streamNoCopy = description->noCopy;
    Interp.streamUser = description->user;
    NextParam();
}

// The shader variant a 16-bit index in the parameter picks
// FUNC_AT(0x000f6090)
void EAGL_RMOp_SelectShaders() {
    int index = *VariationData<int16_t>();
    RenderMethod *method = Interp.method;
    PixelShader **ps = method->pixelShaders + index;
    EAGL_SetVertexShader(method->vertexShaders[index]);
    EAGL_SetPixelShader(*ps);
    NextParam();
}

// Variant 0; no parameter
// FUNC_AT(0x000f60f0)
void EAGL_RMOp_DefaultShaders() {
    RenderMethod *method = Interp.method;
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

// A stream source described inline in the parameter data (read as a StaticVertexBuffer: its buffer and stride),
// 0x10 in after packet.elementSize * count bytes rounded up to 4
// FUNC_AT(0x000f6220)
void EAGL_RMOp_InlineStreamSource() {
    const InlineStreamPacket *packet = CurrentPacket<InlineStreamPacket>();
    GeoPrimParam *param = Interp.param;
    uint32_t skip = packet->elementSize * param->count;
    uint32_t variation = packet->variationStride * CurrentVariation;
    const uint8_t *description = param->data + (((skip + 3) & 0xfffffffc) + variation + 0x10);
    EAGL_SetStreamSource(packet->stream, reinterpret_cast<const StaticVertexBuffer *>(description));
    NextParam();
}

// FUNC_AT(0x000f6270)
void EAGL_RMOp_IndexData() {
    EAGL_SetIndexData(0, VariationData());
    NextParam();
}

// A TAR: its stage set through its extension (the TAR's +0x48 field is the extension's `this`), then bound
// FUNC_AT(0x000f62b0)
void EAGL_RMOp_TAR() {
    FlushIfRegistered();
    uint32_t stage = CurrentPacket<StagePacket>()->stage;
    reinterpret_cast<TARExtension *>(&VariationData<TAR>()->extension)->SetStage(stage);
    VariationData<TAR>()->Use();
    NextParam();
}

// A GeoPrimState applied, and its primitive type noted for the draws
// FUNC_AT(0x000f6320)
void EAGL_RMOp_GeoPrimState() {
    VariationData<GeoPrimState>()->Apply();
    uint32_t type;
    VariationData<GeoPrimState>()->GetPrimitiveType(&type);
    Cache.primitiveType = type;
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

// DrawVertices of the stream's vertex count when the packet is enabled; no parameter
// FUNC_AT(0x000f65c0)
void EAGL_RMOp_DrawVertices() {
    FlushIfRegistered();
    if (CurrentPacket<DrawPacket>()->enabled != 0)
        EAGL_DrawVertices(0, Interp.streamVertexCount);
}

// FUNC_AT(0x000f6600)
void EAGL_RMOp_DrawIndexedVertices() {
    FlushIfRegistered();
    if (CurrentPacket<DrawPacket>()->enabled != 0)
        EAGL_DrawIndexedVertices(0, Interp.streamVertexCount);
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
    SkinSource = VariationData<SkinVertex>();
    SkinCount = Interp.param->count;
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

// The packet's push buffer when the packet is enabled; no parameter. Never reached on the disc (docs 8.5).
// FUNC_AT(0x000f6870)
void EAGL_RMOp_RunPushBuffer() {
    const PushBufferPacket *packet = CurrentPacket<PushBufferPacket>();
    if (packet->enabled != 0)
        EAGL_RunPushBuffer(packet->pushBuffer);
}

// The CPU vertex array into the packet's dynamic vertex buffer, re-created when the count outgrows the capacity,
// and bound to the packet's stream. Quirks kept: the buffer is created 4 times the array's size, and the capacity
// is stored as count * 4 against which a count is compared.
// FUNC_AT(0x000f6890)
void EAGL_RMOp_DynamicStream() {
    DynamicStreamPacket *packet = CurrentPacket<DynamicStreamPacket>();
    uint32_t count = Interp.param->count;
    uint32_t stride = packet->vertexStride;
    int32_t capacity = packet->capacity;
    uint32_t stream = packet->stream;
    DynamicVertexBuffer *buffer = packet->buffer;
    uint32_t bytes = count * stride;
    // The original's lock output is a stack slot that also took the allocation; with user memory Lock leaves it
    // alone, and when no buffer was made it holds whatever was there (here NULL). Only reached with flags the
    // disc does not combine (no-copy 0 and user memory set).
    void *slot = NULL;
    if (!(capacity > int32_t(count))) {   // a signed comparison, as the original's
        if (buffer != Unset<DynamicVertexBuffer>() && buffer != NULL) {
            buffer->Destruct();
            EaglFree(buffer, sizeof(DynamicVertexBuffer));
        }
        if (Interp.streamUser != 0) {
            slot = EaglMalloc(sizeof(DynamicVertexBuffer), NameVBNew);
            buffer = slot != NULL
                ? static_cast<DynamicVertexBuffer *>(slot)->ConstructUser(Interp.param->data, bytes * 4, stride)
                : NULL;
        } else {
            slot = EaglMalloc(sizeof(DynamicVertexBuffer), NameVBNew);
            buffer = slot != NULL ? static_cast<DynamicVertexBuffer *>(slot)->Construct(bytes * 4, stride) : NULL;
        }
        packet->buffer = buffer;
        packet->capacity = Interp.param->count << 2;
        packet->source = Interp.param->data;
    }
    if (Interp.streamNoCopy == 0) {
        buffer->Lock(0, bytes, &slot);
        MEM_copy(slot, Interp.param->data, bytes);
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
    unknown10 = 0;
    buffer = D3DDevice_CreateVertexBuffer2(size_);
    return this;
}

// The user-memory form: a 0xc-byte stand-in header; the third argument is not used.
// FUNC_AT(0x000f6be0)
EAGL::StaticVertexBuffer* EAGL::StaticVertexBuffer::ConstructUser(uint32_t size_, uint32_t stride_, uint32_t) {
    size = size_;
    stride = stride_;
    user = 1;
    unknown10 = 0;
    buffer = static_cast<D3DResource *>(EaglMalloc(sizeof(D3DResource), NameD3DVB));
    return this;
}

// FUNC_AT(0x000f6c20)
void EAGL::StaticVertexBuffer::Destruct() {
    UnbindStreams(buffer);
    D3DResource_BlockUntilNotBusy(buffer);
    if (user != 0) {
        EaglFree(buffer, sizeof(D3DResource));
        buffer = NULL;
        return;
    }
    D3DResource_Release(buffer);
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
    *out = D3DVertexBuffer_Lock2(buffer, 0) + offset;
}

// FUNC_AT(0x000f6ce0)
void EAGL::StaticVertexBuffer::Unlock() {
}

// FUNC_AT(0x000f6cf0)
bool EAGL::StaticVertexBuffer::IsBusy() {
    return D3DResource_IsBusy(buffer) != 0;
}

// FUNC_AT(0x000f6d00)
EAGL::DynamicVertexBuffer* EAGL::DynamicVertexBuffer::Construct(uint32_t size_, uint32_t stride_) {
    size = size_;
    stride = stride_;
    user = 0;
    unknown2c = 0;
    buffers[0] = D3DDevice_CreateVertexBuffer2(size_);
    buffers[1] = D3DDevice_CreateVertexBuffer2(size);
    buffers[2] = D3DDevice_CreateVertexBuffer2(size);
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
    unknown2c = 0;
    userHeader.common = 0;
    userHeader.data = 0;
    userHeader.lock = 0;
    XGSetVertexBufferHeader(0, 0, 0, 0, &userHeader, reinterpret_cast<uintptr_t>(data) + 0x80000000);
    current = &userHeader;
    return this;
}

// FUNC_AT(0x000f6da0)
void EAGL::DynamicVertexBuffer::Destruct() {
    if (user != 0) {
        D3DResource_BlockUntilNotBusy(&userHeader);
        userHeader.common = 0;
        userHeader.data = 0;
        userHeader.lock = 0;
        return;
    }
    // One pass over the streams, each checked against the three buffers in turn
    for (uint32_t stream = 0; stream < 16; stream++) {
        for (int i = 0; i < 3; i++) {
            if (Cache.streams[stream] == buffers[i]) {
                Cache.streams[stream] = NULL;
                D3DDevice_SetStreamSource(stream, NULL, 0);
            }
        }
    }
    D3DResource_BlockUntilNotBusy(buffers[0]);
    D3DResource_BlockUntilNotBusy(buffers[1]);
    D3DResource_BlockUntilNotBusy(buffers[2]);
    D3DResource_Release(buffers[0]);
    D3DResource_Release(buffers[1]);
    D3DResource_Release(buffers[2]);
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
    *out = D3DVertexBuffer_Lock2(buffers[next], 0) + offset;
}

// FUNC_AT(0x000f6ed0)
void EAGL::DynamicVertexBuffer::Unlock() {
}

// FUNC_AT(0x000f6ee0)
bool EAGL::DynamicVertexBuffer::IsBusy() {
    if (user != 0)
        return false;
    return D3DResource_IsBusy(current) != 0;
}

// FUNC_AT(0x000f6f00)
EAGL::IndexBuffer* EAGL::IndexBuffer::Construct(uint32_t size_) {
    size = size_;
    user = 0;
    buffer = D3DDevice_CreateIndexBuffer2(size_);
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
        D3DResource_Release(buffer);
    buffer = NULL;
}

// FUNC_AT(0x000f6f50)
uint32_t EAGL::IndexBuffer::GetSize() {
    return size;
}

// The resource's data address plus the offset
// FUNC_AT(0x000f6f60)
void EAGL::IndexBuffer::Lock(uint32_t offset, uint32_t, uint8_t **out) {
    *out = reinterpret_cast<uint8_t *>(buffer->data + offset);
}

// FUNC_AT(0x000f6f80)
void EAGL::IndexBuffer::Unlock() {
}

// FUNC_AT(0x000f6f90)
bool EAGL::IndexBuffer::IsBusy() {
    return D3DResource_IsBusy(buffer) != 0;
}

// FUNC_AT(0x000f6fa0)
EAGL::VertexShader* EAGL::VertexShader::Construct(const void *declaration, const void *function) {
    D3DDevice_CreateVertexShader(declaration, function, &handle, 0);
    return this;
}

// FUNC_AT(0x000f6fc0)
void EAGL::VertexShader::Destruct() {
    if (Cache.vertexShader == handle) {
        D3DDevice_SetVertexShader(0);
        Cache.vertexShader = 0;
    }
    D3DDevice_DeleteVertexShader(handle);
}

// FUNC_AT(0x000f6ff0)
EAGL::PixelShader* EAGL::PixelShader::Construct(const void *definition) {
    D3DDevice_CreatePixelShader(definition, &handle);
    return this;
}

// FUNC_AT(0x000f7010)
void EAGL::PixelShader::Destruct() {
    if (Cache.pixelShader == handle) {
        D3DDevice_SetPixelShader(0);
        Cache.pixelShader = 0;
    }
    D3DDevice_DeletePixelShader(handle);
}

// A push buffer header over existing memory, registered against base and run at once (unreferenced). The header's
// words are written as the original writes them, Common included.
// FUNC_AT(0x000f7040)
EAGL::PushBuffer* EAGL::PushBuffer::Construct(uint32_t size, uint32_t allocationSize, uint32_t common, void *base) {
    unknown1c = size;
    notOwned = 1;
    unknown20 = allocationSize;
    unknown18 = common;
    header.common = 0;
    header.data = 0;
    header.lock = 0;
    header.size = 0;
    header.allocationSize = 0;
    header.size = size;
    header.common = common;
    header.allocationSize = allocationSize;
    D3DResource_Register(&header, base);
    D3DDevice_RunPushBuffer(&header, NULL);
    return this;
}

// FUNC_AT(0x000f70a0)
void EAGL::PushBuffer::Destruct() {
    if (notOwned == 0)
        D3DResource_Release(&header);
}
