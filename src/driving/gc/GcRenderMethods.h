#ifndef DRIVING_GC_GCRENDERMETHODS_H_
#define DRIVING_GC_GCRENDERMETHODS_H_

// The render methods the GameCube's models name, the fields of their GeoPrims, and the Xbox render method (eaglrm.o /
// bondrm.o, from the Xbox disc) each is drawn with: the mapping table, and the parameters an Xbox GeoPrim of that
// method takes, in the order its packets read them (docs/gc-driving.md, "The mapping table"). See GcRenderMethods.cpp.

#include <stdint.h>

namespace EAGL {
struct RenderMethod;
}

// What one parameter of the Xbox GeoPrim is
enum GcParamKind : uint8_t {
    kParamState,          // the GameCube GeoPrim's GeoPrimState
    kParamTexture,        // its TAR
    kParamStream,         // opcode 2's stream description: the index count
    kParamIndices,        // 16-bit indices
    kParamPositions,      // stream: float x, y, z
    kParamTexCoords,      // stream: short u, v, in 1/256ths
    kParamColours,        // stream: D3DCOLOR
    kParamVariable,       // a name the loader would resolve: the game's resolver, then the registered variables
    kParamConstant,       // a four-float constant of the method
};

struct GcParam {
    GcParamKind kind;
    uint16_t count;       // kParamVariable: the parameter's count (registers)
    const char *name;     // kParamVariable
    const float *constant; // kParamConstant
};

// How a GameCube method's vertex arrays are held
enum GcPositions : uint8_t {
    kPositionsFloat,      // float x, y, z
    kPositionsShort,      // short x, y, z in 1/256ths (the "Packed" methods)
};
enum GcColours : uint8_t {
    kColoursNone,         // the car methods: worked out per frame from the damage; drawn white
    kColoursRGBA8,        // a word a vertex, R G B A
    kColoursRGB565,       // a halfword a vertex (the "Packed" methods)
};

struct GcRenderMethod {
    const char *gcName;           // as the GameCube's GeoPrims name it (an undefined symbol of the model objects)
    // The GeoPrim's fields (a count and a pointer each, from +0x08, in the method's *_FieldNames order): the number
    // of them and where the arrays the conversion reads are, -1 for none
    int8_t fieldCount, coordinates, colours, uv, displayList, texture, state;
    GcPositions positions;
    GcColours colourFormat;
    uint8_t uvShift;              // the 16-bit texture coordinates divided by 1 << this make 1/256ths
    const char *xboxName;         // the Xbox render method drawn instead, or NULL: the GeoPrims are not drawn
    const GcParam *params;
    int paramCount;
    EAGL::RenderMethod *method;   // the child of the Xbox method the GeoPrims use, made on first use
};

// The GameCube render methods' names, registered as variables whose value is their entry, so that a model object's
// references to them resolve to it. Once.
void Gc_RegisterRenderMethodNames();

// The entry a resolved reference points at, or NULL
GcRenderMethod *Gc_FindRenderMethod(const void *reference);

// The Xbox child method for an entry: the Xbox parent's packets with each dynamic stream (opcode 11) turned into an
// inline one (opcode 9) over the GeoPrim's own arrays. NULL if the Xbox method is not loaded.
EAGL::RenderMethod *Gc_XboxMethod(GcRenderMethod *entry);

// The Xbox's render-method libraries are loaded from this block (GcFiles.cpp): the object conversion leaves them be
void Gc_MarkXboxObject(const void *elf);
bool Gc_IsXboxObject(const void *elf);

// Bytes between the colour arrays' stream sources, one a variation (GcModel.cpp's InlineStream)
constexpr uint32_t kInlineStreamStride = 0x30;

#endif // DRIVING_GC_GCRENDERMETHODS_H_
