#ifndef DRIVING_EAGL_RENDERMETHOD_H_
#define DRIVING_EAGL_RENDERMETHOD_H_

// EAGL's render methods (docs/driving/eagl.md 2.8, 2.9, 3.3, 4.6): the RenderMethod object and its construction,
// the interpreter that runs a render method's packet stream over a GeoPrim, the 37-entry opcode table's handlers,
// the one-line D3D8 wrappers they call (Ghidra files them under "PrintMessage"), the vertex/index buffer and shader
// wrappers, the dynamic vertex buffer ring, the CPU skinning, and the DrawArray / DrawGouraud / DrawTextured
// classes. See RenderMethod.cpp. Types in namespace EAGL; free functions global with an EAGL_ prefix (invented
// names where Ghidra has none).

#include <stdint.h>

#include "GeoPrimState.h"
#include "../gfx/D3D8.h"

class DynamicLoader;

namespace EAGL {

struct RenderMethod;
struct DynamicVertexBuffer;
struct PushBuffer;
struct DynamicModel;
struct TAR;

// One parameter of a GeoPrim: what the render method's packets read, indexed by CurrentVariation * stride.
struct GeoPrimParam {                // 8
    uint32_t count;                  // +0x00 a count or a name
    uint8_t *data;                   // +0x04
};
static_assert(sizeof(GeoPrimParam) == 8, "a GeoPrim parameter is 8 bytes");

struct GeoPrim {
    RenderMethod *method;            // +0x00
    GeoPrimParam params[1];          // +0x04 ... as many as the method's packets read
};

// ---- the packet stream (3.3)

// The opcodes: the index into the handler table at 0x001ce700. The names are the handlers'.
enum PacketOpcode : uint32_t {
    kOpSkip = 0,                     // also 10, 12 and 13 (the same handler)
    kOpSkip1 = 1,
    kOpStream = 2,
    kOpSelectShaders = 3,
    kOpDefaultShaders = 4,
    kOpVSConstants5 = 5,
    kOpVSConstants6 = 6,
    kOpVSConstants7 = 7,
    kOpVSConstants8 = 8,
    kOpInlineStreamSource = 9,
    kOpSkip10 = 10,
    kOpDynamicStream = 11,
    kOpSkip12 = 12,
    kOpSkip13 = 13,
    kOpIndexData = 14,
    kOpTAR = 15,
    kOpGeoPrimState = 16,
    kOpVSConstantPair17 = 17,
    kOpVSConstantPair18 = 18,
    kOpVSMatrix19 = 19,
    kOpVSMatrix20 = 20,
    kOpMatrixPalette21 = 21,
    kOpMatrixPalette22 = 22,
    kOpSkip23 = 23,
    kOpSkip24 = 24,
    kOpDrawVertices = 25,
    kOpDrawIndexedVertices = 26,
    kOpVSMatrix27 = 27,
    kOpVSMatrix28 = 28,
    kOpSkin29 = 29,
    kOpSkin30 = 30,
    kOpSkinSource = 31,
    kOpPSMatrix32 = 32,
    kOpPSMatrix33 = 33,
    kOpPSConstants34 = 34,
    kOpPSConstants35 = 35,
    kOpRunPushBuffer = 36,
    kOpcodeCount = 37,
};

// A packet: a header dword (opcode << 16 | length in dwords, the header counted) and its operands. A zero header
// ends the stream. A Packet is the header alone, so a Packet pointer steps in dwords: packet + Length() is the next.
struct Packet {
    uint32_t header;                 // +0x00

    uint32_t Opcode() const { return header >> 16; }
    uint32_t Length() const { return header & 0xffff; }
};
static_assert(sizeof(Packet) == 4, "a Packet pointer steps in dwords");

// A packet that reads a GeoPrim parameter: the data for the current variation is data + variationStride *
// CurrentVariation. Opcodes 0-3, 10, 12-14, 23, 24 and 31 have no more operands.
struct ParamPacket : Packet {
    uint32_t variationStride;        // +0x04
};

// Opcodes 5-8, 17-22, 27-30 and 32-35: shader constants.
struct ConstantsPacket : ParamPacket {
    int32_t reg;                     // +0x08 the (first) register; the matrix opcodes have no more
    int32_t count;                   // +0x0c 5-8, 34, 35: count >> 2 registers; 17-22, 29, 30: registers
    int32_t reg1;                    // +0x10 17, 18, 21, 22, 29, 30: the second block
    int32_t count1;                  // +0x14
};
static_assert(sizeof(ConstantsPacket) == 0x18, "a constants packet is 6 dwords");

// Opcode 9: a stream source described inline in the parameter data.
struct InlineStreamPacket : ParamPacket {
    uint32_t stream;                 // +0x08
    uint32_t elementSize;            // +0x0c bytes per counted element ahead of the description
};
static_assert(sizeof(InlineStreamPacket) == 0x10, "an inline stream packet is 4 dwords");

// Opcode 11: the CPU vertex array and the dynamic vertex buffer it is copied into, kept in the packet. The last three
// words are all ones while there is no buffer (EAGL_ReleaseDynamicBuffers).
struct DynamicStreamPacket : ParamPacket {
    uint32_t stream;                 // +0x08
    uint32_t vertexStride;           // +0x0c
    DynamicVertexBuffer *buffer;     // +0x10
    int32_t capacity;                // +0x14 the count * 4 the buffer was made for
    uint8_t *source;                 // +0x18 the array the buffer was made for
};
static_assert(sizeof(DynamicStreamPacket) == 0x1c, "a dynamic stream packet is 7 dwords");

// Opcode 15: a TAR and its texture stage.
struct StagePacket : ParamPacket {
    uint32_t stage;                  // +0x08
};
static_assert(sizeof(StagePacket) == 0xc, "a TAR packet is 3 dwords");

// Opcodes 25, 26: the draws (no parameter).
struct DrawPacket : Packet {
    uint32_t unknown04;              // +0x04
    uint32_t enabled;                // +0x08 0: no draw
};
static_assert(sizeof(DrawPacket) == 0xc, "a draw packet is 3 dwords");

// Opcode 36: a push buffer run (no parameter).
struct PushBufferPacket : Packet {
    uint32_t enabled;                // +0x04 0: not run
    PushBuffer *pushBuffer;          // +0x08
};
static_assert(sizeof(PushBufferPacket) == 0xc, "a push buffer packet is 3 dwords");

// Opcode 2's parameter data, and DrawArray's own copy of one.
struct StreamDescription {           // 0x10
    uint32_t vertexCount;            // +0x00
    uint32_t unknown04;              // +0x04
    uint32_t user;                   // +0x08 nonzero: opcode 11 draws from the caller's memory
    uint32_t noCopy;                 // +0x0c nonzero: opcode 11 does not copy the vertices
};
static_assert(sizeof(StreamDescription) == 0x10, "a stream description is 4 dwords");

// A skinning record (opcodes 29-31): three blend weights, each a float whose bit pattern's low byte is also the
// index of its palette matrix - kept as bits, as the code reads both.
struct SkinVertex {                  // 0x10
    uint32_t weights[3];             // +0x00
    uint32_t unknown0c;              // +0x0c
};
static_assert(sizeof(SkinVertex) == 0x10, "a skinning record is 16 bytes");

// ---- D3D8 objects as EAGL sees them

// The D3D8 resource header (Common, Data, Lock) every vertex, index and push buffer starts with - its data the
// data's address, an offset into the image until registered - and D3DPushBuffer, that and Size and AllocationSize
// (../gfx/D3D8.h).
using ::D3DResource;
using ::D3DPushBuffer;

struct VertexShader {                // 4, "EAGL::VertexShader new"
    uint32_t handle;                 // +0x00 D3D8 vertex shader handle

    VertexShader* Construct(const void *declaration, const void *function);    // 0x000f6fa0
    void Destruct();                                                           // 0x000f6fc0
};
static_assert(sizeof(VertexShader) == 4, "a VertexShader is a handle");

struct PixelShader {                 // 4, "EAGL::PixelShader new"
    uint32_t handle;                 // +0x00 D3D8 pixel shader handle

    PixelShader* Construct(const void *definition);                            // 0x000f6ff0
    void Destruct();                                                           // 0x000f7010
};
static_assert(sizeof(PixelShader) == 4, "a PixelShader is a handle");

struct RenderMethod {                // 0x34 (PS2: EAGL::RenderMethod)
    Packet *packets;                 // +0x00 packet stream; an owned copy when cloned (its size in bytes at [-1])
    int numVariants;                 // +0x04 number of shader variants
    const void *declaration;         // +0x08 vertex declaration
    const void **vsMicrocode;        // +0x0c vertex shader programs, one per variant
    VertexShader **vertexShaders;    // +0x10 created vertex shaders
    const uint8_t **psDefinitions;   // +0x14 pixel shader definitions
    PixelShader **pixelShaders;      // +0x18 created pixel shaders
    RenderMethod *next;              // +0x1c list at 0x0023ff20 (constructed) / 0x0023ff24 (children waiting)
    const char **paramNames;         // +0x20 parameter names, NULL-terminated (DrawArray looks them up)
    uint32_t variantHalf;            // +0x24 0: the second half of each pixel shader definition is created
    uint32_t cloned;                 // +0x28 1 when made from a parent
    RenderMethod *parent;            // +0x2c
    const char *name;                // +0x30

    RenderMethod* Construct(Packet *packets, int numVariants, const void *declaration, const void **vsMicrocode,
                            VertexShader **vertexShaders, const uint8_t **psDefinitions,
                            PixelShader **pixelShaders, const char **paramNames, uint32_t variantHalf,
                            const char *name);                                 // 0x000f0f60
    void Draw(GeoPrim *prim);                                                  // 0x000f0fc0 (PS2: Draw(GeoPrim *))
    void Nop1020(uint32_t unused);                                             // 0x000f1020 (empty, unreferenced)
    bool CanOptimize(void *geoPrim, uint32_t unused);                          // 0x000f1030 (invented; true)
    void Nop1040(uint32_t unused);                                             // 0x000f1040 (empty, unreferenced)
    void CreateShaders();                                                      // 0x000f1050 (invented)
    void SetVertexShaderTable(VertexShader **const *table);                    // 0x000f1180 (invented, unreferenced)
    VertexShader ***GetVertexShaderTable();                                    // 0x000f1190 (invented, unreferenced)
    void SetPixelShaderTable(PixelShader **const *table);                      // 0x000f11a0 (invented, unreferenced)
    PixelShader ***GetPixelShaderTable();                                      // 0x000f11b0 (invented, unreferenced)
    RenderMethod* ConstructChild(RenderMethod *parent);                        // 0x000f1270 (invented)
    void Destruct();                                                           // 0x000f12c0
    void DeleteShaders();                                                      // 0x000f1300 (invented)
};
static_assert(sizeof(RenderMethod) == 0x34, "a RenderMethod is 0x34 bytes");

// The loader's "VertexBuffer" object.
struct LoadedVertexBuffer {
    D3DResource *header;             // +0x00 the D3D8 vertex buffer header (relocated pointer)
    uint32_t unknown04[3];           // +0x04
    uint32_t fileOffset;             // +0x10 the data's offset in the ELF image
};

// A vertex buffer the CPU fills once (0x14). Unreferenced in this game.
struct StaticVertexBuffer {
    D3DResource *buffer;             // +0x00 D3D8 vertex buffer (or a 0xc-byte stand-in for user memory)
    uint32_t size;                   // +0x04
    uint32_t stride;                 // +0x08 read by EAGL_SetStreamSource
    uint8_t user;                    // +0x0c
    uint8_t pad0d[3];
    uint32_t unknown10;              // +0x10

    void Nop6b80(uint32_t unused);                                             // 0x000f6b80 (empty)
    void Nop6ba0(uint32_t unused);                                             // 0x000f6ba0 (empty)
    StaticVertexBuffer* Construct(uint32_t size, uint32_t stride);             // 0x000f6bb0
    StaticVertexBuffer* ConstructUser(uint32_t size, uint32_t stride, uint32_t unused);  // 0x000f6be0
    void Destruct();                                                           // 0x000f6c20
    void Nop6c90(uint32_t unused);                                             // 0x000f6c90 (empty)
    uint32_t GetSize();                                                        // 0x000f6ca0
    uint32_t GetStride();                                                      // 0x000f6cb0
    void Lock(uint32_t offset, uint32_t size, uint8_t **out);                  // 0x000f6cc0
    void Unlock();                                                             // 0x000f6ce0 (empty)
    bool IsBusy();                                                             // 0x000f6cf0
};
static_assert(sizeof(StaticVertexBuffer) == 0x14, "a StaticVertexBuffer is 0x14 bytes");

// The dynamic vertex buffer (0x30, "EAGL::VertexBuffer new"): three D3D8 buffers rotated per lock, or a header over
// the caller's memory. Opcode 11 keeps one per packet (packet +0x10).
struct DynamicVertexBuffer {
    D3DResource *current;            // +0x00 the buffer in use
    D3DResource *buffers[3];         // +0x04
    D3DResource userHeader;          // +0x10 D3D8 vertex buffer header over user memory
    int index;                       // +0x1c which of the three
    uint32_t size;                   // +0x20
    uint32_t stride;                 // +0x24 read by EAGL_SetDynamicStreamSource
    uint8_t user;                    // +0x28 1: over user memory
    uint8_t pad29[3];
    uint32_t unknown2c;              // +0x2c

    DynamicVertexBuffer* Construct(uint32_t size, uint32_t stride);            // 0x000f6d00
    DynamicVertexBuffer* ConstructUser(void *data, uint32_t size, uint32_t stride);  // 0x000f6d50
    void Destruct();                                                           // 0x000f6da0
    uint32_t GetSize();                                                        // 0x000f6e70
    uint32_t GetStride();                                                      // 0x000f6e80
    void Lock(uint32_t offset, uint32_t size, void **out);                     // 0x000f6e90
    void Unlock();                                                             // 0x000f6ed0 (empty)
    bool IsBusy();                                                             // 0x000f6ee0
};
static_assert(sizeof(DynamicVertexBuffer) == 0x30, "a DynamicVertexBuffer is 0x30 bytes");

// An index buffer (0xc). Unreferenced in this game.
struct IndexBuffer {
    D3DResource *buffer;             // +0x00 D3D8 index buffer
    uint32_t size;                   // +0x04
    uint8_t user;                    // +0x08
    uint8_t pad09[3];

    IndexBuffer* Construct(uint32_t size);                                     // 0x000f6f00
    IndexBuffer* ConstructUser(uint32_t size, uint32_t unused);                // 0x000f6f20
    void Destruct();                                                           // 0x000f6f30
    uint32_t GetSize();                                                        // 0x000f6f50
    void Lock(uint32_t offset, uint32_t size, uint8_t **out);                  // 0x000f6f60
    void Unlock();                                                             // 0x000f6f80 (empty)
    bool IsBusy();                                                             // 0x000f6f90
};
static_assert(sizeof(IndexBuffer) == 0xc, "an IndexBuffer is 0xc bytes");

// A push buffer built in place and run (0x24). Unreferenced.
struct PushBuffer {
    uint8_t notOwned;                // +0x00
    uint8_t pad01[3];
    D3DPushBuffer header;            // +0x04
    uint32_t unknown18;              // +0x18 Construct's Common argument
    uint32_t unknown1c;              // +0x1c Construct's Size argument
    uint32_t unknown20;              // +0x20 Construct's AllocationSize argument

    PushBuffer* Construct(uint32_t size, uint32_t allocationSize, uint32_t common, void *base);  // 0x000f7040
    void Destruct();                                                           // 0x000f70a0
};
static_assert(sizeof(PushBuffer) == 0x24, "a PushBuffer is 0x24 bytes");

// Two 32-entry arrays with a count each (0x108), zeroed. Unreferenced; what it is for is unknown.
struct PairedArrays32 {
    uint32_t a[32];                  // +0x00
    uint32_t countA;                 // +0x80
    uint32_t b[32];                  // +0x84
    uint32_t countB;                 // +0x104

    PairedArrays32* Construct();                                               // 0x000f4550 (invented)
};
static_assert(sizeof(PairedArrays32) == 0x108, "0x108 bytes");

// The original's thiscall matrix product at 0x000f14e0 (unreferenced): out = this * rhs.
struct RenderMatrix {
    float m[16];

    void* MultiplyInto(const float *rhs, float *out);                          // 0x000f14e0 (invented)
};

// DrawArray (0x4c, Ghidra struct right): CPU vertex arrays drawn through a DynamicModel. Dead in this game.
struct DrawArray {
    struct UserVar { uint8_t *data; uint32_t count; };

    uint32_t primitiveType;          // +0x00
    DynamicModel *model;             // +0x04
    GeoPrim *geoPrim;                // +0x08
    UserVar *userVars;               // +0x0c "mUserVarData", one per parameter
    int paramCount;                  // +0x10
    uint32_t unknown14;              // +0x14
    uint8_t locked;                  // +0x18
    uint8_t pad19[3];
    int drawVerts;                   // +0x1c
    int maxVerts;                    // +0x20 0xffff
    uint32_t unknown24;              // +0x24 0xffff - primitive class
    uint32_t unknown28;              // +0x28 0xffff
    int streamParam;                 // +0x2c parameter index of the stream description, -1 none
    int primitiveClass;              // +0x30 0, 1, 2; -1 unsupported
    uint8_t dirty;                   // +0x34
    uint8_t keepCounts;              // +0x35
    uint8_t pad36[2];
    uint32_t numVerts;               // +0x38
    StreamDescription stream;        // +0x3c the stream description the stream parameter points at

    bool SetGeoPrim(GeoPrim *prim);                                            // 0x000f55a0
    bool SetPrimitiveType(int type);                                           // 0x000f5640
    int GetIndexFromName(const char *name);                                    // 0x000f56c0
    const char *GetNameFromIndex(int index);                                   // 0x000f5730
    bool SetParamName(const char *name);                                       // 0x000f5750
    bool SetVar(int index, uint8_t *data, int mode, uint32_t count);           // 0x000f5770
    bool Lock();                                                               // 0x000f57d0
    bool Unlock();                                                             // 0x000f57e0
    bool SetNumVerts(uint32_t count);                                          // 0x000f57f0
    bool SetLocalMatrix(const float *matrix);                                  // 0x000f5800
    void SetUpGeoPrim(uint32_t unused);                                        // 0x000f5820
    void Destruct();                                                           // 0x000f5c30
    bool SetVarByName(const char *name, uint8_t *data, int mode, uint32_t count);  // 0x000f5c80 (Ghidra: SetVar)
    bool Draw(int count);                                                      // 0x000f5cb0
};
static_assert(sizeof(DrawArray) == 0x4c, "a DrawArray is 0x4c bytes");

// Immediate-mode Gouraud triangles (the profiler's).
struct DrawGouraud {
    uint32_t primitiveType;          // +0x00
    GeoPrimState state;              // +0x04
    PixelShader *pixelShader;        // +0x50
    VertexShader *vertexShader;      // +0x54
    uint8_t begun;                   // +0x58
    uint8_t pad59[3];
    float matrix[16];                // +0x5c

    DrawGouraud* Construct();                                                  // 0x000f58b0
    bool Init();                                                               // 0x000f5970
    void InternalFlush();                                                      // 0x000f59f0
    void Destruct();                                                           // 0x000f5cf0 (invented: ~DrawGouraud)
};
static_assert(sizeof(DrawGouraud) == 0x9c, "the fields DrawGouraud's code touches end at 0x9c");

// Immediate-mode textured quads (the movie player's, dead since it became ours).
struct DrawTextured {
    uint32_t primitiveType;          // +0x00
    GeoPrimState state;              // +0x04
    TAR *tar;                        // +0x50
    PixelShader *pixelShader;        // +0x54
    VertexShader *vertexShader;      // +0x58
    uint8_t begun;                   // +0x5c
    uint8_t pad5d[3];
    float matrix[16];                // +0x60

    DrawTextured* Construct();                                                 // 0x000f5a10
    bool Init();                                                               // 0x000f5a30
    void Destruct();                                                           // 0x000f5b60
    void InternalFlush();                                                      // 0x000f5be0
    void SetTAR(TAR *tar);                                                     // 0x000f5c00 (invented)
    void Begin(uint32_t primitiveType);                                        // 0x0014cf10
};
static_assert(sizeof(DrawTextured) == 0xa0, "the fields DrawTextured's code touches end at 0xa0");

}  // namespace EAGL

// ---- construction (loader callbacks and helpers)
void EAGL_CopyParentPackets(EAGL::RenderMethod *method);                       // 0x000f0e50
void EAGL_VertexBufferConstructor(EAGL::LoadedVertexBuffer *buffer, DynamicLoader *loader);   // 0x000f0ee0
void EAGL_VertexBufferDestructor(EAGL::LoadedVertexBuffer *buffer);            // 0x000f0f10
void EAGL_RenderMethodConstructor(EAGL::RenderMethod *method);                 // 0x000f11c0
void EAGL_ReleaseDynamicBuffers(EAGL::RenderMethod *method);                   // 0x000f1210 (invented)
void EAGL_RenderMethodDestructor(EAGL::RenderMethod *method);                  // 0x000f1390
void FUN_000f13b0();                                                           // 0x000f13b0 determinant, EAX = matrix
double EAGL_Determinant3x3(float a0, float a1, float a2, float a3, float a4, float a5, float a6, float a7,
                           float a8);                                          // 0x000f1490 (invented, unreferenced)

// ---- the D3D8 wrappers (Ghidra: "PrintMessage")
void EAGL_RunPushBuffer(EAGL::PushBuffer *buffer);                             // 0x000f4340
void EAGL_SetVertexShader(const EAGL::VertexShader *shader);                   // 0x000f4350
void EAGL_SetPixelShader(const EAGL::PixelShader *shader);                     // 0x000f4380
void EAGL_SetVertexShaderConstant(int reg, const void *data, int count);       // 0x000f43a0
void EAGL_SetStreamSource(uint32_t stream, const EAGL::StaticVertexBuffer *buffer);      // 0x000f43e0
void EAGL_SetDynamicStreamSource(uint32_t stream, const EAGL::DynamicVertexBuffer *buffer);  // 0x000f4420
void EAGL_SetIndices(uint32_t baseVertex, const EAGL::IndexBuffer *buffer);    // 0x000f4460 (unreferenced)
void EAGL_SetIndexData(uint32_t base, const void *indices);                    // 0x000f4480
void EAGL_SetPixelShaderConstant(uint32_t reg, const void *data, uint32_t count);  // 0x000f44a0
void EAGL_DrawVertices(uint32_t start, uint32_t count);                        // 0x000f44c0
void EAGL_DrawIndexedVerticesPushBuffer(uint32_t unused0, uint32_t unused1, uint32_t start, uint32_t count);  // 0x000f44e0
void EAGL_DrawIndexedVertices(uint32_t start, uint32_t count);                 // 0x000f4500
int EAGL_RoundToInt(float value);                                              // 0x000f4530 (invented, unreferenced)
void EAGL_StoreRegisterArgs();                                                 // 0x000f4540 (EAX, ECX, EDX; unreferenced)

// ---- helpers
void __stdcall EAGL_ModelCallFence(const char *name);                          // 0x000f5d70 (Model::Call's)
bool __stdcall EAGL_ReturnFalse(uint32_t unused);                              // 0x000f5de0 (unreferenced)
void EAGL_UploadMatrixPalette(int reg0, int count0, int reg1, int count1, const float *matrices);  // 0x000f5df0
void EAGL_SkinMatrix(const EAGL::SkinVertex *vertices, int index, const float *palette, float *out);  // 0x000f5eb0
uint8_t *EAGL_RMParamData();                                                   // 0x000f5fd0 (unreferenced)
uint32_t EAGL_RMPacketStride();                                                // 0x000f5fe0 (unreferenced)
uint8_t *EAGL_RMVariationData();                                               // 0x000f5ff0 (unreferenced)
void EAGL_SkinAndUpload(const EAGL::SkinVertex *vertices, int count, int reg0, int count0, int reg1, int count1,
                        const float *palette);                                 // 0x000f6aa0
void EAGL_LockRegisterArgs();                                                  // 0x000f6b90 (EAX, EDX; unreferenced)

// ---- the opcode handlers (table at 0x001ce700, which still points at the originals' addresses)
void EAGL_RMOp_Skip();                    // 0x000f6010 ops 0, 10, 12, 13
void EAGL_RMOp_Skip1();                   // 0x000f6020 op 1
void EAGL_RMOp_Stream();                  // 0x000f6030 op 2
void EAGL_RMOp_SelectShaders();           // 0x000f6090 op 3
void EAGL_RMOp_DefaultShaders();          // 0x000f60f0 op 4
void EAGL_RMOp_VSConstants5();            // 0x000f6120 op 5
void EAGL_RMOp_VSConstants6();            // 0x000f6160 op 6
void EAGL_RMOp_VSConstants7();            // 0x000f61a0 op 7
void EAGL_RMOp_VSConstants8();            // 0x000f61e0 op 8
void EAGL_RMOp_InlineStreamSource();      // 0x000f6220 op 9
void EAGL_RMOp_IndexData();               // 0x000f6270 op 14
void EAGL_RMOp_TAR();                     // 0x000f62b0 op 15
void EAGL_RMOp_GeoPrimState();            // 0x000f6320 op 16
void EAGL_RMOp_VSConstantPair17();        // 0x000f6380 op 17
void EAGL_RMOp_VSConstantPair18();        // 0x000f63e0 op 18
void EAGL_RMOp_VSMatrix19();              // 0x000f6440 op 19
void EAGL_RMOp_VSMatrix20();              // 0x000f64a0 op 20
void EAGL_RMOp_MatrixPalette21();         // 0x000f6500 op 21
void EAGL_RMOp_MatrixPalette22();         // 0x000f6550 op 22
void EAGL_RMOp_Skip23();                  // 0x000f65a0 op 23
void EAGL_RMOp_Skip24();                  // 0x000f65b0 op 24
void EAGL_RMOp_DrawVertices();            // 0x000f65c0 op 25
void EAGL_RMOp_DrawIndexedVertices();     // 0x000f6600 op 26
void EAGL_RMOp_VSMatrix27();              // 0x000f6640 op 27
void EAGL_RMOp_VSMatrix28();              // 0x000f66a0 op 28
void EAGL_RMOp_SkinSource();              // 0x000f6700 op 31
void EAGL_RMOp_PSMatrix32();              // 0x000f6730 op 32
void EAGL_RMOp_PSMatrix33();              // 0x000f6790 op 33
void EAGL_RMOp_PSConstants34();           // 0x000f67f0 op 34
void EAGL_RMOp_PSConstants35();           // 0x000f6830 op 35
void EAGL_RMOp_RunPushBuffer();           // 0x000f6870 op 36
void EAGL_RMOp_DynamicStream();           // 0x000f6890 op 11
void EAGL_RMOp_Skin29();                  // 0x000f6a00 op 29
void EAGL_RMOp_Skin30();                  // 0x000f6a50 op 30

#endif // DRIVING_EAGL_RENDERMETHOD_H_
