#ifndef DRIVING_PS2_PS2RENDERMETHODS_H_
#define DRIVING_PS2_PS2RENDERMETHODS_H_

// The render methods the PS2's models name, and the Xbox render method (eaglrm.o / bondrm.o, from the Xbox disc)
// each is drawn with: the mapping table, and the parameters an Xbox GeoPrim of that method takes, in the order its
// packets read them (docs/ps2-driving.md, "The mapping table"). See Ps2RenderMethods.cpp.

#include <stdint.h>

namespace EAGL {
struct RenderMethod;
}

// What one parameter of the Xbox GeoPrim is
enum Ps2ParamKind : uint8_t {
    kParamState,          // the GeoPrimState the PS2 program names, or the one before it in the draw list
    kParamTexture,        // the object's own TAR number `index` the PS2 program names, likewise
    kParamStream,         // opcode 2's stream description: the index count
    kParamIndices,        // 16-bit indices
    kParamVariable,       // `name`, as the loader would resolve it: the game's resolver, then the registered variables
    kParamProgramVariable, // the variable the PS2 program names whose name starts with `name`, else `fallback`
    kParamConstant,       // `count` four-float registers of the method (zeros without `constant`)
    kParamPositions,      // stream: float x, y, z
    kParamTexCoords,      // stream: short u, v, in 1/256ths
    kParamTexCoordsFloat, // stream: float u, v
    kParamColours,        // stream: D3DCOLOR, one array a variation
    kParamNormals,        // stream: D3DVSDT_NORMPACKED3
    kParamDamageVectors,  // stream: float x, y, z
    kParamDamageNormals,  // stream: float x, y, z
    kParamDamageWeights,  // stream: float
    kParamDamageZones,    // stream: short
};

struct Ps2Param {
    Ps2ParamKind kind;
    uint8_t index;        // kParamTexture
    uint16_t count;       // the parameter's count (registers, for the constants and variables)
    const char *name;     // kParamVariable, kParamProgramVariable
    const float *constant; // kParamConstant
    const char *fallback; // kParamProgramVariable
};

struct Ps2RenderMethod {
    const char *ps2Name;          // as the PS2's GeoPrims name it (an undefined symbol of the model objects)
    const char *xboxName;         // the Xbox render method drawn instead, or NULL: the GeoPrims are not drawn
    const Ps2Param *params;
    int paramCount;
    uint8_t texCoordShift;        // the PS2's 16-bit texture coordinates divided by 1 << this make the Xbox's 1/256ths
    uint8_t vertexQuadwords;      // what the PS2's strip starts count a vertex in
    EAGL::RenderMethod *method;   // the child of the Xbox method the GeoPrims use, made on first use
};

// The PS2 render methods' names (and their microcode's), registered as variables whose value is their entry
// below, so that a model object's references to them resolve to it. Once.
void Ps2_RegisterRenderMethodNames();

// The entry a resolved reference points at, or NULL
Ps2RenderMethod *Ps2_FindRenderMethod(const void *reference);

// The Xbox child method for an entry: the Xbox parent's packets with each dynamic stream (opcode 11) turned into an
// inline one (opcode 9) over the GeoPrim's own arrays. NULL if the Xbox method is not loaded.
EAGL::RenderMethod *Ps2_XboxMethod(Ps2RenderMethod *entry);

// Bytes between the colour arrays' stream sources, one a variation (Ps2Model.cpp's InlineStream)
constexpr uint32_t kInlineStreamStride = 0x30;

#endif // DRIVING_PS2_PS2RENDERMETHODS_H_
