#ifndef DRIVING_RENDER_DRAW_H_
#define DRIVING_RENDER_DRAW_H_

// ---------------------------------------------------------------------------------------------------------------
// Simple drawing in screen space (Ghidra: Draw, SimpleDraw): sprites, boxes and quads through the two simple
// materials SimpleDraw::Init makes, at a depth and under a model matrix the caller sets, and two batches - textured
// quads and sprites - that collect up to 120 vertices of one texture before they are drawn. Also the colour
// conversion the static initialisers apply to their colour constants. See Draw.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "VectorMaths.hpp"                // Vec4
#include "../../common/xbeClass.h"        // LAYOUT_32

struct MATRIX4;
class USimpleMaterial;
class USimpleTexturedMaterial;
namespace EAGL {
struct GeoPrimState;
struct TAR;
}

// A colour as the vertex arrays hold it, 0xAARRGGBB (the name is ours); its constructor clears it
struct PackedColour {
    uint32_t value;

    PackedColour* Construct();                                                                  // 0x00076240
};

// The colour through YIQ with the luma brightened and the chroma strengthened, clamped; alpha kept
uint32_t ColourConvertXBoxToPS2(uint32_t colour);                                               // 0x00075c40

class SimpleDraw {
public:
    // Makes the two materials
    static void Init();                                                                         // 0x00075e80
    static void InitThunk();            // a second entry, a jump to Init                       // 0x00075f20
};

// A sprite's texture coordinates: the top left's (u, v) in the first's x and y, the bottom right's in the second's
struct SpriteTexCoords {
    Coord4 topLeft;
    Coord4 bottomRight;
};

class Draw {
public:
    // Every draw's transform (NULL: the material's model's own)
    static void SetModelMatrix(MATRIX4 *matrix);                                                // 0x00075f30
    // The z of every vertex built here; SetZDepthNear puts it back to 1
    static void SetZDepth(float z);                                                             // 0x00075f40
    static void SetZDepthNear();                                                                // 0x00075f50

    // A w x h rectangle at (x, y): textured, or filled with a colour
    static void DrawSprite(float x, float y, float width, float height, uint32_t colour,
                           const SpriteTexCoords *texCoords, EAGL::TAR *texture);               // 0x00075f60
    static void DrawSprite(float x, float y, float width, float height, uint32_t colour,
                           EAGL::TAR *texture);         // the whole texture                    // 0x00076250
    static void DrawBox(float x, float y, float width, float height, uint32_t colour);          // 0x000760e0
    // Four vertices as a strip, with their colours or one colour
    static void DrawQuadC(const Coord4 *positions, const uint32_t *colours);                    // 0x000761c0
    static void DrawQuad(const Coord4 *positions, uint32_t colour);                             // 0x000762d0

    static void SetNormalBlendMode(EAGL::GeoPrimState *material);                               // 0x00076200
    static void SetAdditiveBlendMode(EAGL::GeoPrimState *material);                             // 0x00076220

    // Adds a quad (the triangles 0 1 2 and 1 2 3) or a sprite (four vertices) to its batch. The batch is drawn
    // first if the texture differs from the batch's or it holds more than 120 vertices; a NULL texture just draws
    // it.
    static void DrawBatchedTexturedQuad(const Coord4 *p0, const Coord4 *p1, const Coord4 *p2, const Coord4 *p3,
                                        uint32_t colour, const Coord4 *texCoords,
                                        EAGL::TAR *texture);    // four texture coordinates     // 0x00076330
    static void DrawBatchedSprite(float x, float y, float width, float height, uint32_t colour,
                                  const SpriteTexCoords *texCoords, EAGL::TAR *texture);        // 0x000765e0
};

// A batch's vertices (the statics of a batched draw, the name is ours). The colours are a function-local static
// array constructed on first use: their guard's bit 0 says they have been.
struct VertexBatch {
    Coord4 texCoords[128];              // +0x0000
    Coord4 positions[128];              // +0x0800
    int32_t count;                      // +0x1000
    uint32_t unknown1004[3];
    EAGL::TAR *texture;                 // +0x1010 the one every vertex in the batch has
    uint32_t unknown1014[3];
    PackedColour colours[128];          // +0x1020
    uint32_t coloursGuard;              // +0x1220
};
LAYOUT_32(offsetof(VertexBatch, count) == 0x1000 && offsetof(VertexBatch, texture) == 0x1010 &&
          offsetof(VertexBatch, colours) == 0x1020 && offsetof(VertexBatch, coloursGuard) == 0x1220,
          "VertexBatch layout");

extern VertexBatch QuadBatch;               // DrawBatchedTexturedQuad's
extern VertexBatch SpriteBatch;             // DrawBatchedSprite's
extern MATRIX4 *DrawModelMatrix;
extern float DrawZ;

extern USimpleMaterial *SimpleMaterial;
extern USimpleTexturedMaterial *SimpleTexturedMaterial;

#endif // DRIVING_RENDER_DRAW_H_
