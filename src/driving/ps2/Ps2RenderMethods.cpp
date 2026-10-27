#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// The mapping table: each render method the PS2's models name, the Xbox render method its GeoPrims are drawn with,
// and that method's parameters in the order its packets read them (docs/ps2-driving.md, "The mapping table"). The
// parameter lists are the Xbox models' own for the method (their GeoPrims in the Xbox disc's tracks and cars), with
// the GeoPrim's data and the PS2 program's TARs and state in their places.

#include "Ps2RenderMethods.h"

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
// The car methods' constants, as the Xbox's cars have them
const float kCarReflection[4] = { 0.5f, 0.5f, 0.0f, 0.0f };
const float kCarSwitch[4] = { 0.5f, 1.0f, 1.0f, 1.0f };

#define STATE                     { kParamState, 0, 1, NULL, NULL, NULL }
#define TEXTURE(n)                { kParamTexture, n, 1, NULL, NULL, NULL }
#define STREAM                    { kParamStream, 0, 1, NULL, NULL, NULL }
#define INDICES                   { kParamIndices, 0, 0, NULL, NULL, NULL }
#define VARIABLE(name, n)         { kParamVariable, 0, n, name, NULL, NULL }
#define PROGRAM_VARIABLE(prefix, fallback, n) { kParamProgramVariable, 0, n, prefix, NULL, fallback }
#define CONSTANT(values)          { kParamConstant, 0, 1, NULL, values, NULL }
#define ZEROS(n)                  { kParamConstant, 0, n, NULL, NULL, NULL }
#define STREAM_OF(kind)           { kind, 0, 0, NULL, NULL, NULL }

#define MVP_MATRIX                VARIABLE("EAGL::ViewPort::gpModelViewProjectionMatrix", 1)
#define MODEL_MATRIX              VARIABLE("EAGL::ViewPort::gpModelMatrix", 1)
// The car methods' streams: position, damage vector, normal, damage normal, texture coordinates, damage weight
// and zone
#define CAR_STREAMS(texCoords)    STREAM_OF(kParamPositions), STREAM_OF(kParamDamageVectors), \
                                  STREAM_OF(kParamNormals), STREAM_OF(kParamDamageNormals), STREAM_OF(texCoords), \
                                  STREAM_OF(kParamDamageWeights), STREAM_OF(kParamDamageZones)
// The car methods' damage registers: the zones, then eight and one the Xbox's cars leave zero
#define CAR_DAMAGE                VARIABLE("EAGL::DamageZones", 16), ZEROS(8), ZEROS(1)

// ParentRM_BondTexturePacked and ParentRM_BondTexture (the same packets)
const Ps2Param kTextureParams[] = {
    STATE, TEXTURE(0), STREAM, INDICES,
    MVP_MATRIX, MODEL_MATRIX,
    VARIABLE("EAGL::ViewPort::gpModelViewMatrix", 1), VARIABLE("EAGL::ViewPort::gpProjectionMatrix", 1),
    VARIABLE("GAME::FishEyeParams", 1), VARIABLE("GAME::PositionalLights", 8), CONSTANT(kPackedTexCoords),
    STREAM_OF(kParamPositions), STREAM_OF(kParamTexCoords), STREAM_OF(kParamColours),
};

// ParentRM_BondReflectPacked: the texture, then the environment map
const Ps2Param kReflectParams[] = {
    STATE, TEXTURE(0), VARIABLE("EAGL::EnvironmentMap", 1), STREAM, INDICES,
    MODEL_MATRIX, VARIABLE("EAGL::ViewPort::gpModelViewMatrix", 1),
    VARIABLE("EAGL::ViewPort::gpProjectionMatrix", 1), VARIABLE("GAME::FishEyeParams", 1),
    VARIABLE("GAME::PositionalLights", 8), VARIABLE("EAGL::CameraPos", 1), CONSTANT(kReflection),
    CONSTANT(kPackedTexCoords),
    STREAM_OF(kParamPositions), STREAM_OF(kParamTexCoords), STREAM_OF(kParamColours),
};

// ParentRM_BondCarTextureDull
const Ps2Param kCarDullParams[] = {
    STATE, TEXTURE(0), STREAM, INDICES, MVP_MATRIX, MODEL_MATRIX,
    VARIABLE("GAME::LightSources", 1), VARIABLE("GAME::PositionalLights", 8), CAR_DAMAGE,
    CONSTANT(kPackedTexCoords), VARIABLE("GAME::CarLightingDullProps", 1),
    CAR_STREAMS(kParamTexCoords),
};

// ParentRM_BondCarTexture and ParentRM_BondCarChrome (the same packets, other lighting)
#define CAR_REFLECTIVE(lighting) \
    STATE, TEXTURE(0), VARIABLE("GAME::ReflectionMap", 1), VARIABLE("GAME::SpecularMap", 1), STREAM, INDICES, \
    MVP_MATRIX, MODEL_MATRIX, VARIABLE("GAME::SpecularMat", 1), VARIABLE("GAME::ReflMatrix", 1), \
    VARIABLE("GAME::LightSources", 1), VARIABLE("GAME::CameraPos", 1), VARIABLE("GAME::PositionalLights", 8), \
    CAR_DAMAGE, CONSTANT(kPackedTexCoords), CONSTANT(kCarReflection), VARIABLE("GAME::ReflectionInfo2", 1), \
    VARIABLE(lighting, 1), VARIABLE("GAME::ReflectionCarPos", 1), CAR_STREAMS(kParamTexCoords)
const Ps2Param kCarTextureParams[] = { CAR_REFLECTIVE("GAME::CarLightingGlossyProps") };
const Ps2Param kCarChromeParams[] = { CAR_REFLECTIVE("GAME::CarLightingChromeProps") };

// ParentRM_BondCarGlass: the texture, the reflection and specular maps, the damage texture
const Ps2Param kCarGlassParams[] = {
    STATE, TEXTURE(0), VARIABLE("GAME::ReflectionMap", 1), VARIABLE("GAME::SpecularMap", 1), TEXTURE(1), STREAM,
    INDICES, MVP_MATRIX, MODEL_MATRIX, VARIABLE("GAME::SpecularMat", 1), VARIABLE("GAME::ReflMatrix", 1),
    VARIABLE("GAME::LightSources", 1), VARIABLE("GAME::CameraPos", 1), VARIABLE("GAME::PositionalLights", 8),
    CAR_DAMAGE, CONSTANT(kPackedTexCoords), CONSTANT(kCarReflection), VARIABLE("GAME::ReflectionInfo2", 1),
    VARIABLE("GAME::CarLightingGlassProps", 1), VARIABLE("GAME::ReflectionCarPos", 1), CAR_STREAMS(kParamTexCoords),
};

// ParentRM_BondCarTextureSwitch: the switch (brake and indicator lights) the PS2 program names, float coordinates
const Ps2Param kCarSwitchParams[] = {
    STATE, TEXTURE(0), STREAM, INDICES, MVP_MATRIX, MODEL_MATRIX,
    VARIABLE("GAME::LightSources", 1), VARIABLE("GAME::PositionalLights", 8), CAR_DAMAGE,
    CONSTANT(kPackedTexCoords), CONSTANT(kCarSwitch), PROGRAM_VARIABLE("GAME::SwitchData", "GAME::SwitchData[8]", 1),
    VARIABLE("GAME::CarLightingDullProps", 1),
    CAR_STREAMS(kParamTexCoordsFloat),
};

// ParentRM_BondCarShadow: positions only
const Ps2Param kCarShadowParams[] = {
    STATE, STREAM, INDICES, MVP_MATRIX, MODEL_MATRIX, VARIABLE("GAME::LightSources", 1), CONSTANT(kPackedTexCoords),
    STREAM_OF(kParamPositions),
};

#define PARAMS(list) list, int(sizeof(list) / sizeof(list[0]))

// The 26 render methods the PS2's model objects name (tools/ps2driving_check.py objects), each with the Xbox method
// the same models use on the Xbox disc where the conversion supplies what it takes, else a simpler one. The car
// methods' texture coordinates are in 1/1024ths (against the Xbox twins' 1/256ths); BondCarShadow's strip starts
// count two quadwords a vertex, the rest three.
Ps2RenderMethod RenderMethods[] = {
    { "MatBondTexturePacked",        "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },
    { "MatBondTexture",              "ParentRM_BondTexture",         PARAMS(kTextureParams), 0, 3, NULL },
    { "MatBondReflectTexturePacked", "ParentRM_BondReflectPacked",   PARAMS(kReflectParams), 0, 3, NULL },
    { "MatDynamicTexturePacked",     "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },   // twin: BondDynTexture
    { "MatBondScrollTexture",        "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },   // twin: BondScrollTex1
    { "MatBondScrollTexturePacked",  "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },   // twin: BondScrollTex1
    { "MatBondScrollTexture2",       "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },   // twin: BondScrollTex2
    { "MatBondScrollTexture2Packed", "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },   // twin: BondScrollTex2
    { "MatBondScrollTexture3Packed", "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },   // twin: BondScrollTex3
    { "MatBondFrameAnimPacked",      "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },   // twin: BondFrameAnim
    { "MatBondWater",                "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },   // twin: BondWater
    { "MatBondWaterPacked",          "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },   // twin: BondWater
    { "BondUnderWater",              "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },   // twin: BondUnderWater
    { "BondUnderWaterPacked",        "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },   // twin: BondUnderWaterPacked
    { "BondKelp",                    "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },   // twin: BondKelp
    { "BondGouraud",                 "ParentRM_BondTexturePacked",   PARAMS(kTextureParams), 0, 3, NULL },   // twin: BondGouraud
    { "MatCarTexturePacked",         "ParentRM_BondCarTexture",      PARAMS(kCarTextureParams), 2, 3, NULL },
    { "MatCarTextureDullPacked",     "ParentRM_BondCarTextureDull",  PARAMS(kCarDullParams), 2, 3, NULL },
    { "MatCarTextureSwitchPacked",   "ParentRM_BondCarTextureSwitch", PARAMS(kCarSwitchParams), 2, 3, NULL },
    { "MatCarGouraudDullPacked",     "ParentRM_BondCarShadow",       PARAMS(kCarShadowParams), 2, 3, NULL },
    { "MatCarChrome",                "ParentRM_BondCarChrome",       PARAMS(kCarChromeParams), 2, 3, NULL },
    { "MatCarGlassPacked",           "ParentRM_BondCarGlass",        PARAMS(kCarGlassParams), 2, 3, NULL },
    { "MatCarDash",                  "ParentRM_BondCarTextureDull",  PARAMS(kCarDullParams), 2, 3, NULL },   // twin: BondCarSpecularBump
    { "BondCarShadow",               "ParentRM_BondCarShadow",       PARAMS(kCarShadowParams), 0, 2, NULL },
    { "BondLitTextureAndShadow_Skin", NULL, NULL, 0, 0, 3, NULL },
    { "BondLitTextureAndShadowSpecular_Skin", NULL, NULL, 0, 0, 3, NULL },
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

}  // namespace

void Ps2_RegisterRenderMethodNames() {
    static bool registered;
    if (registered)
        return;
    registered = true;
    static char microcode[kRenderMethodCount][64];
    for (int i = 0; i < kRenderMethodCount; i++) {
        DynamicLoader::RegisterVar(RenderMethods[i].ps2Name, &RenderMethods[i]);
        snprintf(microcode[i], sizeof(microcode[i]), "%s0__EAGLMicroCode", RenderMethods[i].ps2Name);
        DynamicLoader::RegisterVar(microcode[i], &RenderMethods[i]);
    }
}

Ps2RenderMethod *Ps2_FindRenderMethod(const void *reference) {
    for (int i = 0; i < kRenderMethodCount; i++)
        if (reference == &RenderMethods[i])
            return &RenderMethods[i];
    return NULL;
}

RenderMethod *Ps2_XboxMethod(Ps2RenderMethod *entry) {
    if (entry->method != NULL || entry->xboxName == NULL)
        return entry->method;
    RenderMethod *parent = FindXboxMethod(entry->xboxName);
    if (parent == NULL || parent->vertexShaders == NULL) {
        printf("[ps2] render method %s: the Xbox's %s is not loaded\n", entry->ps2Name, entry->xboxName);
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
            // The same operands: variation stride, stream, element size
            EAGL::ParamPacket *stream = static_cast<EAGL::ParamPacket *>(p);
            stream->header = EAGL::PacketHeader(EAGL::kOpInlineStreamSource, p->Length());
            stream->variationStride = entry->params[param].kind == kParamColours ? kInlineStreamStride : 0;
        }
        param++;
    }
    if (param != entry->paramCount) {
        printf("[ps2] render method %s: %s reads %d parameters, the table has %d\n", entry->ps2Name,
               entry->xboxName, param, entry->paramCount);
        return NULL;
    }
    entry->method = child;
    return child;
}
