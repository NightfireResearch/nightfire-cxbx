#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// The mapping table: each render method the PS2's models name, the Xbox render method its GeoPrims are drawn with,
// and that method's parameters in the order its packets read them (docs/ps2-driving.md, "The mapping table"). The
// parameter lists are the Xbox models' own for the method (their GeoPrims in the Xbox disc's tracks), with the
// GeoPrim's data and the PS2 program's TAR and state in their places.

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

#define STATE              { kParamState, 0, 1, NULL, NULL }
#define TEXTURE(n)         { kParamTexture, n, 1, NULL, NULL }
#define STREAM             { kParamStream, 0, 1, NULL, NULL }
#define INDICES            { kParamIndices, 0, 0, NULL, NULL }
#define POSITIONS          { kParamPositions, 0, 0, NULL, NULL }
#define TEXCOORDS          { kParamTexCoords, 0, 0, NULL, NULL }
#define COLOURS            { kParamColours, 0, 0, NULL, NULL }
#define VARIABLE(name, n)  { kParamVariable, 0, n, name, NULL }
#define CONSTANT(values)   { kParamConstant, 0, 1, NULL, values }

// ParentRM_BondTexturePacked and ParentRM_BondTexture (the same packets)
const Ps2Param kTextureParams[] = {
    STATE, TEXTURE(0), STREAM, INDICES,
    VARIABLE("EAGL::ViewPort::gpModelViewProjectionMatrix", 1), VARIABLE("EAGL::ViewPort::gpModelMatrix", 1),
    VARIABLE("EAGL::ViewPort::gpModelViewMatrix", 1), VARIABLE("EAGL::ViewPort::gpProjectionMatrix", 1),
    VARIABLE("GAME::FishEyeParams", 1), VARIABLE("GAME::PositionalLights", 8), CONSTANT(kPackedTexCoords),
    POSITIONS, TEXCOORDS, COLOURS,
};

// ParentRM_BondReflectPacked: the texture, then the environment map
const Ps2Param kReflectParams[] = {
    STATE, TEXTURE(0), VARIABLE("EAGL::EnvironmentMap", 1), STREAM, INDICES,
    VARIABLE("EAGL::ViewPort::gpModelMatrix", 1), VARIABLE("EAGL::ViewPort::gpModelViewMatrix", 1),
    VARIABLE("EAGL::ViewPort::gpProjectionMatrix", 1), VARIABLE("GAME::FishEyeParams", 1),
    VARIABLE("GAME::PositionalLights", 8), VARIABLE("EAGL::CameraPos", 1), CONSTANT(kReflection),
    CONSTANT(kPackedTexCoords), POSITIONS, TEXCOORDS, COLOURS,
};

#define PARAMS(list) list, int(sizeof(list) / sizeof(list[0]))

// The 26 render methods the PS2's model objects name (tools/ps2driving_check.py objects). Those drawn with a
// simpler Xbox method than their own twin are so until their extra streams and constants are converted.
Ps2RenderMethod RenderMethods[] = {
    { "MatBondTexturePacked",        "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },
    { "MatBondTexture",              "ParentRM_BondTexture",       PARAMS(kTextureParams), NULL },
    { "MatBondReflectTexturePacked", "ParentRM_BondReflectPacked", PARAMS(kReflectParams), NULL },
    { "MatDynamicTexturePacked",     "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondDynTexture
    { "MatBondScrollTexture",        "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondScrollTex1
    { "MatBondScrollTexturePacked",  "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondScrollTex1
    { "MatBondScrollTexture2",       "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondScrollTex2
    { "MatBondScrollTexture2Packed", "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondScrollTex2
    { "MatBondScrollTexture3Packed", "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondScrollTex3
    { "MatBondFrameAnimPacked",      "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondFrameAnim
    { "MatBondWater",                "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondWater
    { "MatBondWaterPacked",          "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondWater
    { "BondUnderWater",              "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondUnderWater
    { "BondUnderWaterPacked",        "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondUnderWaterPacked
    { "BondKelp",                    "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondKelp
    { "BondGouraud",                 "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondGouraud
    { "MatCarTexturePacked",         "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondCarTexture
    { "MatCarTextureDullPacked",     "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondCarTextureDull
    { "MatCarTextureSwitchPacked",   "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondCarTextureSwitch
    { "MatCarGouraudDullPacked",     "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondCarGouraudDull
    { "MatCarChrome",                "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondCarChrome
    { "MatCarGlassPacked",           "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },   // twin: BondCarGlass
    { "MatCarDash",                  "ParentRM_BondTexturePacked", PARAMS(kTextureParams), NULL },
    { "BondCarShadow",               NULL, NULL, 0, NULL },
    { "BondLitTextureAndShadow_Skin", NULL, NULL, 0, NULL },
    { "BondLitTextureAndShadowSpecular_Skin", NULL, NULL, 0, NULL },
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
