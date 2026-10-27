#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// The mapping table: each render method the GameCube's models name, its GeoPrims' fields (the method's *_FieldNames
// table in the GameCube's driving.elf), the Xbox render method its GeoPrims are drawn with, and that method's
// parameters in the order its packets read them (docs/gc-driving.md, "The mapping table"). The parameter lists are the
// Xbox models' own for the method (their GeoPrims in the Xbox disc's tracks), with the GameCube GeoPrim's data, TAR
// and state in their places.

#include "GcRenderMethods.h"

#include "../eagl/EaglGlobals.h"
#include "../eagl/Loader.h"
#include "../eagl/RenderMethod.h"

#include <stdio.h>
#include <string.h>

using EAGL::Packet;
using EAGL::RenderMethod;

namespace {

// The constant register the Xbox's packed methods scale their short texture coordinates with (x 1/256)
const float kPackedTexCoords[4] = { 0.0f, 1.0f, 2.0f, 1.0f / 256 };
// BondReflectPacked's reflection constant, as every Xbox GeoPrim of it in paris_mis01.crp has it
const float kReflection[4] = { 0.4375f, 0.5f, 5.0f, 0.0f };

#define STATE              { kParamState, 1, NULL, NULL }
#define TEXTURE            { kParamTexture, 1, NULL, NULL }
#define STREAM             { kParamStream, 1, NULL, NULL }
#define INDICES            { kParamIndices, 0, NULL, NULL }
#define POSITIONS          { kParamPositions, 0, NULL, NULL }
#define TEXCOORDS          { kParamTexCoords, 0, NULL, NULL }
#define COLOURS            { kParamColours, 0, NULL, NULL }
#define VARIABLE(name, n)  { kParamVariable, n, name, NULL }
#define CONSTANT(values)   { kParamConstant, 1, NULL, values }

// ParentRM_BondTexturePacked
const GcParam kTextureParams[] = {
    STATE, TEXTURE, STREAM, INDICES,
    VARIABLE("EAGL::ViewPort::gpModelViewProjectionMatrix", 1), VARIABLE("EAGL::ViewPort::gpModelMatrix", 1),
    VARIABLE("EAGL::ViewPort::gpModelViewMatrix", 1), VARIABLE("EAGL::ViewPort::gpProjectionMatrix", 1),
    VARIABLE("GAME::FishEyeParams", 1), VARIABLE("GAME::PositionalLights", 8), CONSTANT(kPackedTexCoords),
    POSITIONS, TEXCOORDS, COLOURS,
};

// ParentRM_BondReflectPacked: the texture, then the environment map
const GcParam kReflectParams[] = {
    STATE, TEXTURE, VARIABLE("EAGL::EnvironmentMap", 1), STREAM, INDICES,
    VARIABLE("EAGL::ViewPort::gpModelMatrix", 1), VARIABLE("EAGL::ViewPort::gpModelViewMatrix", 1),
    VARIABLE("EAGL::ViewPort::gpProjectionMatrix", 1), VARIABLE("GAME::FishEyeParams", 1),
    VARIABLE("GAME::PositionalLights", 8), VARIABLE("EAGL::CameraPos", 1), CONSTANT(kReflection),
    CONSTANT(kPackedTexCoords), POSITIONS, TEXCOORDS, COLOURS,
};

#define PARAMS(list) list, int(sizeof(list) / sizeof(list[0]))
#define TEXTURED "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL
#define REFLECT "ParentRM_BondReflectPacked", PARAMS(kReflectParams), NULL
#define NOT_DRAWN NULL, NULL, 0, NULL

// Every render method the GameCube's model objects name. Those drawn with a simpler Xbox method than their own twin
// (the twins' methods in the comments: tools' pairing of the models both discs have) are so until their extra
// streams and constants are converted. Positions: the "Packed" methods' in 1/256ths. Texture coordinates: 1/256ths
// for the packed and car methods (as the Xbox's packed methods have them), 1/16384ths for the others.
//  name                         fields coords colours uv list tex state  positions        colours        uv
GcRenderMethod RenderMethods[] = {
    { "BondTexturePacked",          10, 3, 4, 5, 6, 7, 9,  kPositionsShort, kColoursRGB565, 0, TEXTURED },
    { "BondTexture",                10, 3, 4, 5, 6, 7, 9,  kPositionsFloat, kColoursRGBA8,  6, TEXTURED },
    { "BondReflectTexturePacked",   13, 4, 5, 6, 7, 8, 12, kPositionsShort, kColoursRGB565, 0, REFLECT },
    { "BondReflectTexture",         13, 4, 5, 6, 7, 8, 12, kPositionsFloat, kColoursRGBA8,  6, REFLECT },
    { "BondDynamicTexture",         13, 3, 5, 6, 7, 8, 12, kPositionsFloat, kColoursRGBA8,  6, TEXTURED },  // BondDynTexture
    { "BondScrollTex1",             11, 4, 5, 6, 7, 8, 9,  kPositionsFloat, kColoursRGBA8,  6, TEXTURED },  // BondScrollTex1
    { "BondScrollTex1Packed",       11, 4, 5, 6, 7, 8, 9,  kPositionsShort, kColoursRGB565, 0, TEXTURED },
    { "BondScrollTex2",             14, 5, 6, 7, 8, 9, 11, kPositionsFloat, kColoursRGBA8,  6, TEXTURED },  // BondScrollTex2
    { "BondScrollTex2Packed",       14, 5, 6, 7, 8, 9, 11, kPositionsShort, kColoursRGB565, 0, TEXTURED },
    { "BondScrollTex3",             17, 6, 7, 8, 9, 10, 13, kPositionsFloat, kColoursRGBA8, 6, TEXTURED },  // BondScrollTex3
    { "BondScrollTex3Packed",       17, 6, 7, 8, 9, 10, 13, kPositionsShort, kColoursRGB565, 0, TEXTURED },
    { "BondFrameAnim",              10, 4, 5, 6, 7, 8, 9,  kPositionsFloat, kColoursRGBA8,  6, TEXTURED },  // BondFrameAnim
    { "BondFrameAnimPacked",        10, 4, 5, 6, 7, 8, 9,  kPositionsShort, kColoursRGB565, 0, TEXTURED },
    { "BondUnderWater",             12, 3, 4, 5, 7, 8, 11, kPositionsFloat, kColoursRGBA8,  6, TEXTURED },  // BondUnderWater
    { "BondUnderWaterPacked",       15, 4, 5, 6, 8, 9, 14, kPositionsShort, kColoursRGB565, 0, TEXTURED },
    { "BondWater",                  27, 12, 13, 14, 15, 16, 22, kPositionsFloat, kColoursRGBA8, 6, TEXTURED }, // BondWater
    { "BondWaterPacked",            27, 12, 13, 14, 15, 16, 22, kPositionsShort, kColoursRGB565, 0, TEXTURED },
    { "BondKelp",                   13, 3, 4, 5, 7, 8, 10, kPositionsFloat, kColoursRGBA8,  6, TEXTURED },  // BondKelp
    { "BondUnlitTexture",            8, 2, 3, 4, 5, 6, 7,  kPositionsFloat, kColoursRGBA8,  6, TEXTURED },
    { "BondCarTexture",             22, 4, -1, 6, 12, 13, 20, kPositionsFloat, kColoursNone, 0, TEXTURED },  // BondCarTexture
    { "BondCarTexturePacked",       22, 4, -1, 6, 12, 13, 20, kPositionsShort, kColoursNone, 0, TEXTURED },
    { "BondCarTextureDull",         17, 3, -1, 5, 11, 12, 13, kPositionsFloat, kColoursNone, 0, TEXTURED },  // ...Dull
    { "BondCarTextureSwitch",       19, 3, -1, 5, 11, 12, 13, kPositionsFloat, kColoursNone, 0, TEXTURED },  // ...Switch
    { "BondCarTextureNoDamage",     17, 4, -1, 6, 7, 8, 15, kPositionsFloat, kColoursNone, 0, TEXTURED },
    { "BondCarTextureNoDamagePacked", 17, 4, -1, 6, 7, 8, 15, kPositionsShort, kColoursNone, 0, TEXTURED },
    { "BondCarChrome",              22, 4, -1, 6, 12, 13, 20, kPositionsFloat, kColoursNone, 0, TEXTURED },  // BondCarChrome
    { "BondCarGlass",               23, 4, -1, 6, 12, 13, 21, kPositionsFloat, kColoursNone, 0, TEXTURED },  // BondCarGlass
    { "BondCarGouraud",             22, 4, -1, -1, 13, -1, 20, kPositionsFloat, kColoursNone, 0, NOT_DRAWN },
    { "BondCarGouraudDull",         17, 3, -1, -1, 12, -1, 13, kPositionsFloat, kColoursNone, 0, NOT_DRAWN }, // BondCarShadow
    { "BondGouraud",                 6, 2, 3, -1, 4, -1, 5,  kPositionsFloat, kColoursRGBA8,  0, NOT_DRAWN },
    { "BondGouraudPacked",           6, 2, 3, -1, 4, -1, 5,  kPositionsShort, kColoursRGB565, 0, NOT_DRAWN },
    { "BondDebris",                 12, 3, -1, 4, -1, 10, 11, kPositionsFloat, kColoursNone, 0, NOT_DRAWN },
    { "BondLitTexture_Skin",        12, 5, -1, 7, 8, 9, 10, kPositionsFloat, kColoursNone, 0, NOT_DRAWN },
    { "BondLitTextureAndShadow_Skin", 16, 5, -1, 7, 8, 9, 14, kPositionsFloat, kColoursNone, 0, NOT_DRAWN },
    { "BondLitTextureAndShadowSpecular_Skin", 23, 6, -1, 8, 9, 10, 21, kPositionsFloat, kColoursNone, 0, NOT_DRAWN },
};
constexpr int kRenderMethodCount = int(sizeof(RenderMethods) / sizeof(RenderMethods[0]));

// The packets that read no parameter
bool ReadsParameter(uint32_t opcode) {
    return opcode != EAGL::kOpDefaultShaders && opcode != EAGL::kOpDrawVertices &&
           opcode != EAGL::kOpDrawIndexedVertices && opcode != EAGL::kOpRunPushBuffer;
}

// A loaded object's symbol table (Loader.cpp's HashTable) starts with the next one in the list
struct LoadedTableLink {
    HashTable *next;
};

RenderMethod *FindXboxMethod(const char *name) {
    for (HashTable *t = LoadedTables; t != NULL; t = reinterpret_cast<LoadedTableLink *>(t)->next) {
        void *found = EAGL_LookupInObject(t, name);
        if (found != NULL)
            return static_cast<RenderMethod *>(found);
    }
    return NULL;
}

const void *XboxObjects[2];

}  // namespace

void Gc_RegisterRenderMethodNames() {
    static bool registered;
    if (registered)
        return;
    registered = true;
    for (int i = 0; i < kRenderMethodCount; i++)
        DynamicLoader::RegisterVar(RenderMethods[i].gcName, &RenderMethods[i]);
    // The skinned methods' fields the characters' GeoPrims name and the executable defines; they are not drawn
    static float unused[16];
    for (const char *name : { "ShadowDarkness", "ReferenceTriangle", "ModelAlphaValue", "DrawAsShadow" })
        DynamicLoader::RegisterVar(name, unused);
}

GcRenderMethod *Gc_FindRenderMethod(const void *reference) {
    for (int i = 0; i < kRenderMethodCount; i++)
        if (reference == &RenderMethods[i])
            return &RenderMethods[i];
    return NULL;
}

RenderMethod *Gc_XboxMethod(GcRenderMethod *entry) {
    if (entry->method != NULL || entry->xboxName == NULL)
        return entry->method;
    RenderMethod *parent = FindXboxMethod(entry->xboxName);
    if (parent == NULL || parent->vertexShaders == NULL) {
        printf("[gc] render method %s: the Xbox's %s is not loaded\n", entry->gcName, entry->xboxName);
        return NULL;
    }
    void *memory = EaglMalloc(sizeof(RenderMethod), "EAGL::Rendermethod new");
    RenderMethod *child = static_cast<RenderMethod *>(memory)->ConstructChild(parent);
    int param = 0;
    for (Packet *p = child->packets; p->header != 0; p += p->Length()) {
        if (!ReadsParameter(p->Opcode()))
            continue;
        if (param >= entry->paramCount) {
            param++;
            continue;
        }
        if (p->Opcode() == EAGL::kOpDynamicStream) {
            // The same operands: variation stride, stream, element size. One colour set serves every variation.
            EAGL::ParamPacket *stream = static_cast<EAGL::ParamPacket *>(p);
            stream->header = EAGL::PacketHeader(EAGL::kOpInlineStreamSource, p->Length());
            stream->variationStride = 0;
        }
        param++;
    }
    if (param != entry->paramCount) {
        printf("[gc] render method %s: %s reads %d parameters, the table has %d\n", entry->gcName,
               entry->xboxName, param, entry->paramCount);
        return NULL;
    }
    entry->method = child;
    return child;
}

void Gc_MarkXboxObject(const void *elf) {
    for (const void *&slot : XboxObjects) {
        if (slot == NULL) {
            slot = elf;
            return;
        }
    }
}

bool Gc_IsXboxObject(const void *elf) {
    for (const void *slot : XboxObjects)
        if (slot == elf)
            return true;
    return false;
}
