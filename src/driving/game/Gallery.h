#ifndef DRIVING_GAME_GALLERY_H_
#define DRIVING_GAME_GALLERY_H_

// ---------------------------------------------------------------------------------------------------------------
// GGallery (0xc0 bytes): a gallery file (.gal, a UGroup tree) and the 2D drawing done through it - the HUD's
// "hudNTSC.gal" is the one the game makes (GHud's constructor). A gallery holds its texture files ('tx' data, up
// to 15, the "MAIN" image of each made a TAR) and its canvases: groups of named puppets, each puppet a run of keys
// ('gk' data). A key is a position, a time and an orientation, then the data of the puppet's kind: a sprite, a
// line of text or a quad. A draw takes the state the last SetState call copied from a key (position,
// orientation, scale, colour) and draws in a 640 x 480 frame. See Gallery.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"         // Coord3, Coord4
#include "../render/Particles.h"          // RParticleSystemData

class UGroup;
class USimpleMaterial;
struct FNTXFont;
struct ShapeFile;
namespace EAGL {
struct TAR;
}

// A sprite key's data (the field names ours). The texture rectangle is left u, right u, top v, bottom v; the
// repeat counts draw the sprite again that many times beside itself (left or right, up or down).
struct GallerySprite {
    float texture[4];                   // +0x00
    float width;                        // +0x10
    float height;                       // +0x14
    uint32_t colour;                    // +0x18 a draw given its own rectangle takes this colour
    int32_t shape;                      // +0x1c the gallery's texture
    uint32_t unknown20;
    int32_t scaleX;                     // +0x24 thousandths
    int32_t scaleY;                     // +0x28
    uint32_t flags;                     // +0x2c GallerySpriteFlags
    uint8_t unknown30[0x1b];
    int8_t repeatLeft;                  // +0x4b
    int8_t repeatRight;                 // +0x4c
    int8_t repeatUp;                    // +0x4d
    int8_t repeatDown;                  // +0x4e
    uint8_t unknown4F;
};
static_assert(sizeof(GallerySprite) == 0x50 && offsetof(GallerySprite, flags) == 0x2c &&
              offsetof(GallerySprite, repeatLeft) == 0x4b, "GallerySprite layout");

enum GallerySpriteFlags : uint32_t {
    kSpriteMirrorX = 0x1,               // drawn to the left of its position, the texture flipped
    kSpriteMirrorY = 0x2,               // drawn above it, flipped
};

// A text key's data (the field names ours)
struct GalleryText {
    uint32_t colour;                    // +0x00
    int32_t text;                       // +0x04 the locale symbol FONT_Draw shows when given no text
    int32_t scaleX;                     // +0x08 thousandths
    int32_t scaleY;                     // +0x0c
    uint8_t unknown10[0x10];
    int32_t shape;                      // +0x20
};
static_assert(offsetof(GalleryText, shape) == 0x20, "GalleryText layout");

// A quad key's data: the corners' colours (zero: the draw takes the state's colour) and positions (x, y)
struct GalleryQuad {
    uint32_t colours[4];                // +0x00
    float corners[4][2];                // +0x10
};
static_assert(sizeof(GalleryQuad) == 0x30, "GalleryQuad is 0x30 bytes");

// The effects gallery's kinds (GFXGallery, EffectsGallery.h; the field names ours). A glare key:
struct GalleryGlare {
    int32_t type;                       // +0x00 the glare's type
    float intensity;                    // +0x04 lerped between keys
    int32_t jitter;                     // +0x08 non-zero: drawn three times, each moved at random
};

// A sound key: the bank slot's patch by name, its volume and pitch in hundredths, its distances
struct GallerySound {
    int32_t volume;                     // +0x00
    uint32_t unknown04;
    int32_t bank;                       // +0x08 a slot of fgBanks
    char patch[0x20];                   // +0x0c "(null)" for none
    int32_t pitch;                      // +0x2c
    int32_t minDistance;                // +0x30
    int32_t maxDistance;                // +0x34
};
static_assert(offsetof(GallerySound, pitch) == 0x2c && offsetof(GallerySound, maxDistance) == 0x34,
              "GallerySound layout");

// A debris key: pieces thrown in a box size times the scales across, their velocities turned at random
struct GalleryDebris {
    float size;                         // +0x00 also twice the count of pieces (at most 7 for a size up to 15)
    float lift;                         // +0x04 times size, added to the pieces' upward spread
    float spreadY;                      // +0x08 times size
    float spreadXZ;                     // +0x0c times size
    uint32_t colour;                    // +0x10
    uint32_t kind;                      // +0x14 0: one shell casing; under 20, the debris kind table's
    uint32_t unknown18;
    float turnX;                        // +0x1c the velocities' random spread about the effect's orientation
    float turnZ;                        // +0x20
    float turnY;                        // +0x24
    float gravity;                      // +0x28
    uint32_t lifetime;                  // +0x2c ticks
};
static_assert(sizeof(GalleryDebris) == 0x30, "GalleryDebris is 0x30 bytes");

// A key (Ghidra: Keys; the field names ours): 0x30 bytes of header, then extraSize bytes of its kind's data. A
// puppet's keys are KeyStride apart.
struct GalleryKey {
    Coord3 position;                    // +0x00
    int32_t time;                       // +0x0c
    Coord4 orientation;                 // +0x10 a quaternion
    uint8_t unknown20;
    uint8_t extraSize;                  // +0x21
    uint8_t unknown22[0xe];
    union {                             // +0x30
        GallerySprite sprite;
        GalleryText text;
        GalleryQuad quad;
        GalleryGlare glare;
        GallerySound sound;
        GalleryDebris debris;
        RParticleSystemData particle;   // the type as the gallery has it, before the library munges it
    };

    // The distance from one key of a puppet to the next: the header and the data, at least 0x40 bytes
    uint32_t KeyStride() const { return extraSize + 0x30u > 0x40 ? extraSize + 0x30u : 0x40; }
};
static_assert(offsetof(GalleryKey, orientation) == 0x10 && offsetof(GalleryKey, extraSize) == 0x21 &&
              offsetof(GalleryKey, sprite) == 0x30 && sizeof(GalleryKey) == 0xe0, "GalleryKey layout");

// The tags of a gallery's records
enum GalleryTag : uint32_t {
    kGalleryTagName = 0x4e616d65,       // 'Name': a canvas's or puppet's name
    kGalleryTagKeys = 0x676b3000,       // 'gk' 0x3000: a puppet's keys
    kGalleryTagTextures = 0x7478,       // 'tx' + an index: a texture file (SHPX)
    kGalleryTagStream = 0x6669,         // 'fi' + an index: GGirl's picture stream
};

// A tag of `type` (two characters) with an index in its low half; -1 for none, the low half then two spaces
inline uint32_t GalleryIndexedTag(uint32_t type, int index) {
    return index == -1 ? type << 16 | 0x2020 : type << 16 | uint32_t(index);
}

constexpr int kGalleryShapes = 15;

class GGallery {
public:
    UGroup *gallery;                    // +0x00 the file's root group
    void *file;                         // +0x04 the file as loaded
    uint32_t unknown08[2];
    EAGL::TAR *shapes[kGalleryShapes];  // +0x10 each texture file's "MAIN" image
    int32_t shapeCount;                 // +0x4c
    uint32_t colour;                    // +0x50 the next draw's
    uint32_t unknown54[3];
    Coord4 position;                    // +0x60 the next draw's, from a key
    Coord4 orientation;                 // +0x70
    Coord4 scale;                       // +0x80 x, y from a sprite key's thousandths
    EAGL::TAR *texture;                 // +0x90 the last SetState's shape; no draw reads it
    uint32_t unknown94[3];
    uint32_t unknownA0;                 // +0xa0 zero from the constructor
    uint32_t unknownA4;                 // +0xa4
    FNTXFont *font;                     // +0xa8 FONT_Load's
    void *fontFile;                     // +0xac
    bool keepRenderState;               // +0xb0 set: the draws leave the simple materials' tests alone
    uint8_t unknownB1[0xf];

    // The file through FILE_FullPathname ("gall1.gal" if it is missing) and its textures
    GGallery* Construct(const char *file);                                                     // 0x000d5d20
    void Destruct();                                                                            // 0x000d6e30
    UGroup* GET_Gallery();                                                                      // 0x000d5800
    // The index of the image named "MAIN" in a texture file, 0 if none is (does not read `this`)
    int MGMT_MainShapeIndex(const ShapeFile *shapes);                                           // 0x000d57a0
    // The 'fi' data `index` (does not read `this` beyond the gallery)
    void* GET_SomeThing(int index);                                                             // 0x000d59c0

    // The canvas named `name` (any case) of a gallery group, NULL if none or no gallery (does not read `this`)
    UGroup* GET_Canvas(UGroup *gallery, const char *name);                                      // 0x000d3850
    // The first key of the canvas's puppet named `name`, NULL if none; kind and unknown are not read
    GalleryKey* GET_Puppet(UGroup *canvas, const char *name, int kind, int unknown);            // 0x000d5810
    // A puppet's key `index` (does not read `this`)
    GalleryKey* GET_Key(GalleryKey *puppet, int index);                                         // 0x000d58f0
    // The key between a and b at t: the position, time and corners lerped, the rest a's
    void Get_InterpolatedKey(GalleryKey *out, const GalleryKey *a, const GalleryKey *b, float t); // 0x000d5910
    void QUAD_Interpolate(const GalleryQuad *a, const GalleryQuad *b, GalleryQuad *out, float t); // 0x000d5b70
    // Each vertex rotated by the conjugate of `orientation`, scaled and moved to `position`
    void TRANSFORM_Vertices(Coord4 *vertices, int count, const Coord4 *position, const Coord4 *orientation,
                            const float *scale);                                                // 0x000d5c60

    // The state for the draws that follow
    void SPRITE_SetState(const GalleryKey *key);                                                // 0x000d5a00
    void QUAD_SetState(const GalleryKey *key);                                                  // 0x000d5b30
    void FONT_SetState(const GalleryKey *key);                                                  // 0x000d6eb0
    void FONT_SetScale(float x, float y);                                                       // 0x000d61e0
    void FONT_Load(const char *file);                                                           // 0x000d5a80
    // The text's width and height at the font's scale
    float FONT_Width(const char *text);                                                         // 0x000d5ad0
    float FONT_Height(const char *text);                                                        // 0x000d5b00

    // The text (the key's locale symbol when NULL) at the state's position in its colour
    void FONT_Draw(const GalleryText *key, const char *text);                                   // 0x000d6200
    void FONT_Draw(const char *text, float x, float y, uint32_t colour);                        // 0x000d63a0
    void FONT_Draw(const char *text, float x, float y);   // in the state's colour             // 0x000d6520
    // A sprite key's whole texture rectangle as a quad at the state's position, orientation and scale
    void BITMAP_Draw2D(const GalleryKey *key);                                                  // 0x000d5f50
    // Points of four floats and their colours as a line strip (does not read `this`)
    void LINE_Draw2DStrip(const Coord4 *points, const uint32_t *colours, int count);            // 0x000d66a0
    // A quad key's corners, moved by the state, their alpha reduced by `alpha`
    void QUAD_Draw(const GalleryQuad *quad, float alpha);                                       // 0x000d6750
    void QUAD_Draw(const GalleryKey *key, float alpha);    // the key's quad                    // 0x000d68b0
    void QUAD_Draw2D(float x, float y, float width, float height, uint32_t colour);             // 0x000d68c0
    // The key's sprite (or `sprite`, a rectangle with its own colour) at (x, y), repeated and mirrored as its
    // data says
    void SPRITE_Draw2D(int x, int y, const GalleryKey *key, const GallerySprite *sprite);       // 0x000d69a0
    void SPRITE_Draw2D(const GalleryKey *key);             // at the key's position             // 0x000d6d70
    // The batched sprites and quads drawn
    void SPRITE_Flush();                                                                        // 0x000d6da0

    // The effects gallery's CULL_SetCanvas (GFX's code, beside GFX::CULL_GetNextCanvas)
    static void CULL_SetCanvas(int item, bool culled, float distance);                          // 0x000d3470

private:
    void DefaultRenderState();
    void DrawFontText(float x, float y, uint32_t colour, const char *text);
};
static_assert(sizeof(GGallery) == 0xc0, "GGallery is 0xc0 bytes");
static_assert(offsetof(GGallery, shapes) == 0x10 && offsetof(GGallery, colour) == 0x50 &&
              offsetof(GGallery, position) == 0x60 && offsetof(GGallery, scale) == 0x80 &&
              offsetof(GGallery, texture) == 0x90 && offsetof(GGallery, font) == 0xa8 &&
              offsetof(GGallery, keepRenderState) == 0xb0, "GGallery layout");

// LINE_Draw2DStrip's material (Gallery.cpp; StaticInit.cpp's DestroyStatic_0023f330 destroys it) and its guard
extern USimpleMaterial LineMaterial;
extern uint32_t LineMaterialMade;

#endif // DRIVING_GAME_GALLERY_H_
