#include "RuntimeAlloc.h"

#include "GeoPrimState.h"
#include "Loader.h"
#include "Profiler.h"
#include "Realgraph.h"
#include "Tar.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The RUNTIME_ALLOC:: constructors (docs/driving/eagl.md 3.1, 9.2 step 4). A symbol RUNTIME_ALLOC::<properties> of
// class EAGL::TAR or EAGL::GeoPrimState is built by the loader from its property text, such as
// "SHAPENAME=abcd,2;SetClampMode=EAGL::CM_CLAMP;XBOXEXTOBJ_SetAnisotropy=4": Properties splits it ("name=value,
// value;..."), and the constructors map each property to a field through the value parsers below.
//
// PROVISIONAL AND UNTESTED: no object on the disc has a RUNTIME_ALLOC symbol and only the loader refers to the
// prefix, so the shipped game never runs this. It is ported from the listings without a shadow test (the user's
// call: not worth a synthetic one), and each constructor says so loudly the first time it runs. The original's
// quirks are kept: GetAddr's answer is looked up and thrown away, the property and value arrays are freed with
// sizes 0xc and 4, a TAR array's later elements are set up field by field and then overwritten by a copy of the
// first, the shape names are built in the original's own static strings. The C runtime calls (strcmp, atol, atof,
// sscanf) are the host's.
// ---------------------------------------------------------------------------------------------------------------

typedef void *(*EaglMallocFn)(uint32_t size, const char *name);
typedef void (*EaglFreeFn)(void *data, uint32_t size);

#define EaglMalloc (*(EaglMallocFn *)0x001caf68)
#define EaglFree (*(EaglFreeFn *)0x001caf6c)
#define GlobalPool (*(SymbolPool *)0x0023fb8c)
#define BuiltInShapes ((ShapeFile *)0x001cbdd0)   // the SHPX file linked into the executable: a TAR's fallback image
#define TarShapeName ((char *)0x001cc980)         // "shape_" and room for four characters, filled in per use
#define TarClutName ((char *)0x001cc998)          // the same, for CLUTNAME

using EAGL::PrintMessage;

static void Untested(const char *what) {
    printf("[eagl] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "builds against the original.\n", what);
    fflush(stdout);
}

// ---- the value parsers: a prefix picks a table (the first that matches), the whole text an entry in it

struct NameValue {
    const char *name;
    uint32_t value;
};

struct NameBlock {
    const char *prefix;
    size_t length;
    const NameValue *entries;
    int count;
};

// Generated from the binary's strings (0x001ccd14..0x001cd97c).
static const NameValue kTarApi0[] = {
    { "EAGL::CM_CLAMP", 0x3 },
    { "EAGL::CM_WRAP", 0x1 },
    { "EAGL::CM_MIRROR", 0x2 },
};
static const NameValue kTarApi1[] = {
    { "EAGL::FM_POINT", 0x1 },
    { "EAGL::FM_BILINEAR", 0x2 },
    { "EAGL::FM_ANISOTROPIC", 0x3 },
};
static const NameValue kTarApi2[] = {
    { "EAGL::MMM_OFF", 0x0 },
    { "EAGL::MMM_NEAREST", 0x1 },
    { "EAGL::MMM_LINEAR", 0x2 },
};
static const NameBlock kTarApi[] = {
    { "EAGL::CM_", 9, kTarApi0, 3 },
    { "EAGL::FM_", 9, kTarApi1, 3 },
    { "EAGL::MMM_", 10, kTarApi2, 3 },
};

static const NameValue kTarValue0[] = {
    { "EAGL::STAGE_ZERO", 0x0 },
    { "EAGL::STAGE_ONE", 0x1 },
    { "EAGL::STAGE_TWO", 0x2 },
    { "EAGL::STAGE_THREE", 0x3 },
    { "EAGL::STAGE_FOUR", 0x4 },
    { "EAGL::STAGE_FIVE", 0x5 },
    { "EAGL::STAGE_SIX", 0x6 },
    { "EAGL::STAGE_SEVEN", 0x7 },
};
static const NameValue kTarValue1[] = {
    { "EAGL::XBOXCM_CLAMP", 0x3 },
    { "EAGL::XBOXCM_WRAP", 0x1 },
    { "EAGL::XBOXCM_MIRROR", 0x2 },
    { "EAGL::XBOXCM_BORDER", 0x4 },
    { "EAGL::XBOXCM_CLAMPTOEDGE", 0x5 },
};
static const NameValue kTarValue2[] = {
    { "EAGL::XBOXTM_LINEAR", 0x0 },
    { "EAGL::XBOXTM_SWIZZLE", 0x1 },
};
static const NameBlock kTarValue[] = {
    { "EAGL::STAGE_", 12, kTarValue0, 8 },
    { "EAGL::XBOXCM_", 13, kTarValue1, 5 },
    { "EAGL::XBOXTM_", 13, kTarValue2, 2 },
};

static const NameValue kGeoPrimState0[] = {
    { "EAGL::PT_POINTLIST", 0x1 },
    { "EAGL::PT_LINELIST", 0x2 },
    { "EAGL::PT_LINESTRIP", 0x4 },
    { "EAGL::PT_LINELOOP", 0x3 },
    { "EAGL::PT_TRIANGLELIST", 0x5 },
    { "EAGL::PT_TRIANGLESTRIP", 0x6 },
    { "EAGL::PT_TRIANGLEFAN", 0x7 },
    { "EAGL::PT_QUADLIST", 0x8 },
    { "EAGL::PT_QUADSTRIP", 0x9 },
    { "EAGL::PT_POLYGON", 0xa },
    { "EAGL::PT_SPRITE", 0xffffffff },
};
static const NameValue kGeoPrimState1[] = {
    { "EAGL::S_FLAT", 0x0 },
    { "EAGL::S_GOURAUD", 0x1 },
    { "EAGL::S_SPECULAR", 0x2 },
};
static const NameValue kGeoPrimState2[] = {
    { "EAGL::ABM_OFF", 0x0 },
    { "EAGL::ABM_BLEND", 0x1 },
    { "EAGL::ABM_ADD", 0x2 },
    { "EAGL::ABM_ATTENUATE", 0x3 },
    { "EAGL::ABM_MODULATE", 0x4 },
    { "EAGL::ABM_SUBTRACT", 0x5 },
    { "EAGL::ABM_CUSTOM", 0x6 },
};
static const NameValue kGeoPrimState3[] = {
    { "EAGL::ATM_NEVER", 0x200 },
    { "EAGL::ATM_ALWAYS", 0x207 },
    { "EAGL::ATM_LESS", 0x201 },
    { "EAGL::ATM_LEQUAL", 0x203 },
    { "EAGL::ATM_EQUAL", 0x202 },
    { "EAGL::ATM_GEQUAL", 0x206 },
    { "EAGL::ATM_GREATER", 0x204 },
    { "EAGL::ATM_NOTEQUAL", 0x205 },
};
static const NameValue kGeoPrimState4[] = {
    { "EAGL::TM_OPAQUE", 0x0 },
    { "EAGL::TM_ALPHA", 0x1 },
    { "EAGL::TM_CHROMAKEY", 0x2 },
};
static const NameValue kGeoPrimState5[] = {
    { "EAGL::DTM_NEVER", 0x200 },
    { "EAGL::DTM_ALWAYS", 0x207 },
    { "EAGL::DTM_NOTEQUAL", 0x205 },
    { "EAGL::DTM_LESS", 0x201 },
    { "EAGL::DTM_LEQUAL", 0x203 },
    { "EAGL::DTM_EQUAL", 0x202 },
    { "EAGL::DTM_GEQUAL", 0x206 },
    { "EAGL::DTM_GREATER", 0x204 },
};
static const NameValue kGeoPrimState6[] = {
    { "EAGL::TCT_STQ", 0xffffffff },
    { "EAGL::TCT_UV", 0xffffffff },
};
static const NameValue kGeoPrimState7[] = {
    { "EAGL::CD_CLOCKWISE", 0x0 },
    { "EAGL::CD_COUNTERCLOCKWISE", 0x1 },
};
static const NameValue kGeoPrimState8[] = {
    { "EAGL::DBT_NONE", 0x0 },
    { "EAGL::DBT_Z", 0x1 },
    { "EAGL::DBT_W", 0x2 },
};
static const NameBlock kGeoPrimState[] = {
    { "EAGL::PT_", 9, kGeoPrimState0, 11 },
    { "EAGL::S_", 8, kGeoPrimState1, 3 },
    { "EAGL::ABM_", 10, kGeoPrimState2, 7 },
    { "EAGL::ATM_", 10, kGeoPrimState3, 8 },
    { "EAGL::TM_", 9, kGeoPrimState4, 3 },
    { "EAGL::DTM_", 10, kGeoPrimState5, 8 },
    { "EAGL::TCT_", 10, kGeoPrimState6, 2 },
    { "EAGL::CD_", 9, kGeoPrimState7, 2 },
    { "EAGL::DBT_", 10, kGeoPrimState8, 3 },
};

static const NameValue kXboxGeoPrimState0[] = {
    { "EAGL::XCD_CW", 0x900 },
    { "EAGL::XCD_CCW", 0x901 },
};
static const NameValue kXboxGeoPrimState1[] = {
    { "EAGL::XFM_POINT", 0x1b00 },
    { "EAGL::XFM_WIREFRAME", 0x1b01 },
    { "EAGL::XFM_SOLID", 0x1b02 },
};
static const NameValue kXboxGeoPrimState2[] = {
    { "EAGL::XBO_ADD", 0x8006 },
    { "EAGL::XBO_SUBTRACT", 0x800a },
    { "EAGL::XBO_REVSUBTRACT", 0x800b },
    { "EAGL::XBO_MIN", 0x8007 },
    { "EAGL::XBO_MAX", 0x8008 },
    { "EAGL::XBO_ADDSIGNED", 0xf006 },
    { "EAGL::XBO_REVSUBTRACTSIGNED", 0xf005 },
};
static const NameValue kXboxGeoPrimState3[] = {
    { "EAGL::XBF_ZERO", 0x0 },
    { "EAGL::XBF_ONE", 0x1 },
    { "EAGL::XBF_SRCCOLOR", 0x300 },
    { "EAGL::XBF_INVSRCCOLOR", 0x301 },
    { "EAGL::XBF_SRCALPHA", 0x302 },
    { "EAGL::XBF_INVSRCALPHA", 0x303 },
    { "EAGL::XBF_DESTALPHA", 0x304 },
    { "EAGL::XBF_INVDESTALPHA", 0x305 },
    { "EAGL::XBF_DESTCOLOR", 0x306 },
    { "EAGL::XBF_INVDESTCOLOR", 0x307 },
    { "EAGL::XBF_SRCALPHASAT", 0x308 },
    { "EAGL::XBF_CONSTANTCOLOR", 0x8001 },
    { "EAGL::XBF_INVCONSTANTCOLOR", 0x8002 },
    { "EAGL::XBF_CONSTANTALPHA", 0x8003 },
    { "EAGL::XBF_INVCONSTANTALPHA", 0x8004 },
};
static const NameBlock kXboxGeoPrimState[] = {
    { "EAGL::XCD_", 10, kXboxGeoPrimState0, 2 },
    { "EAGL::XFM_", 10, kXboxGeoPrimState1, 3 },
    { "EAGL::XBO_", 10, kXboxGeoPrimState2, 7 },
    { "EAGL::XBF_", 10, kXboxGeoPrimState3, 15 },
};

static uint32_t ParseNamed(const char *text, const NameBlock *blocks, int blockCount, const char *message) {
    for (int b = 0; b < blockCount; b++) {
        if (strncmp(text, blocks[b].prefix, blocks[b].length) != 0)
            continue;
        for (int i = 0; i < blocks[b].count; i++)
            if (strcmp(text, blocks[b].entries[i].name) == 0)
                return blocks[b].entries[i].value;
        break;
    }
    PrintMessage(0, message, text);
    return 0;
}

#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

// FUNC_AT(0x000ed390)
uint32_t EAGL_ParseTarApi(const char *text) {
    return ParseNamed(text, kTarApi, COUNT(kTarApi), "INTERNAL ERROR: Invalid TAR api function parameter '%s'\n");
}

// FUNC_AT(0x000ed700)
uint32_t EAGL_ParseTarValue(const char *text) {
    return ParseNamed(text, kTarValue, COUNT(kTarValue), "INTERNAL ERROR: Invalid TAR value %s\n");
}

// FUNC_AT(0x000ef7a0)
uint32_t EAGL_ParseGeoPrimState(const char *text) {
    return ParseNamed(text, kGeoPrimState, COUNT(kGeoPrimState),
                      "INTERNAL ERROR: Invalid GeoPrimState function parameter '%s'\n");
}

// FUNC_AT(0x000f04d0)
uint32_t EAGL_ParseXboxGeoPrimState(const char *text) {
    return ParseNamed(text, kXboxGeoPrimState, COUNT(kXboxGeoPrimState),
                      "INTERNAL ERROR: Invalid GeoPrimState value %s\n");
}

// FUNC_AT(0x000f0450)
bool EAGL_ParseTrue(const char *text) {
    return strcmp(text, "true") == 0;
}

// "0x%x"; the original returns whatever its stack held if the text is not hex.
// FUNC_AT(0x000f04b0)
uint32_t EAGL_ParseHex(const char *text) {
    unsigned value = 0;
    sscanf(text, "0x%x", &value);
    return value;
}

// ---- Properties

// FUNC_AT(0x000ed330)
bool Property::Is(const char *wanted, int valueCount) {
    if (valueCount != count)
        return false;
    return strcmp(wanted, name) == 0;
}

// The text copied and cut up in place: ';' ends a property, '=' its name, ',' a value. A property needs its '=';
// a second '=' in the values drops the value before it; leading separators are skipped.
// FUNC_AT(0x000edb30)
Properties* Properties::Construct(const char *text) {
    count = 0;
    list = NULL;
    int widest = 0;
    length = (int)strlen(text);
    buffer = (char *)EaglMalloc(length + 1, NULL);
    strcpy(buffer, text);
    int commas = 0, semicolons = 0;
    for (int i = 0; i < length; i++) {
        char c = buffer[i];
        if (c == ',')
            commas++;
        if (commas >= widest)
            widest = commas;
        if (c == ';') {
            semicolons++;
            commas = 0;
        }
    }
    int properties = semicolons + 1;
    widest++;
    int *block = (int *)EaglMalloc(properties * sizeof(Property) + 4, "EAGLInternal::Property new[]");
    if (block != NULL) {   // new[]: the count, then each element zeroed (0x000ed310)
        block[0] = properties;
        list = (Property *)(block + 1);
        for (int i = 0; i < properties; i++) {
            list[i].name = NULL;
            list[i].count = 0;
            list[i].values = NULL;
        }
    }
    for (int i = 0; i < properties; i++) {
        const char **values = (const char **)EaglMalloc(widest * 4, "EAGLInternal::PropertyValue new[]");
        if (values != NULL)
            memset(values, 0, widest * 4);
        list[i].values = values;
    }

    char *token = buffer;
    while (token != NULL && *token != 0 && (*token == ';' || *token == ',' || *token == '='))
        token++;
    bool inValues = false, saved = false;
    int filled = 0, value = 0;
    Property *current = list;
    for (;;) {
        char *end = token;
        char separator = 0;
        if (end != NULL) {
            while (*end != 0 && *end != ';' && *end != ',' && *end != '=')
                end++;
            separator = *end;
            inValues = saved;
            *end = 0;
        }
        switch (separator) {
        case '=':
            if (!inValues) {
                current->name = token;
                inValues = true;
                value = 0;
                saved = true;
            }
            break;
        case ',':
            if (inValues)
                current->values[value++] = token;
            break;
        case ';':
            if (inValues) {
                current->values[value++] = token;
                filled++;
                current->count = value;
                current++;
            }
            inValues = false;
            saved = false;
            break;
        case 0:
            if (inValues) {
                current->values[value++] = token;
                filled++;
                current->count = value;
                current++;
            }
            break;
        }
        token = end + 1;
        if (separator == 0)
            break;
    }
    count = filled;
    return this;
}

// The original's epilogue: each element's values (freed as 4 bytes), the array (as 0xc), the copy.
void Properties::Destruct() {
    if (list != NULL) {
        int *block = (int *)list - 1;
        for (int i = block[0]; i-- > 0;)   // the vector destructor iterator, last first (0x000ed320 each)
            EaglFree(list[i].values, 4);
        EaglFree(block, sizeof(Property));
    }
    EaglFree(buffer, length + 1);
}

// ---- EAGL::TAR from properties: SHAPENAME=<4 chars>,<count> makes an array of count TARs on that image

// An element of a TAR array: TARs are 0x50 apart in one
struct TARSlot {
    EAGL::TAR tar;
    uint32_t unknown4c;
};
static_assert(sizeof(TARSlot) == 0x50, "TAR arrays are 0x50 apart");

// FUNC_AT(0x000ed640)
void EAGL_SetTarApi(EAGL::TAR *tar, Property *property) {
    if (property->Is("SetClampMode", 1)) {
        uint32_t mode = EAGL_ParseTarApi(property->values[0]);
        tar->address0 = mode;
        tar->addressU = mode;
        tar->addressV = mode;
        tar->addressW = mode;
    } else if (property->Is("SetFilterMode", 1)) {
        tar->filter = EAGL_ParseTarApi(property->values[0]);
    } else if (property->Is("SetMIPMAPLODBias", 1)) {
        tar->lodBias = (float)atof(property->values[0]);
    } else if (property->Is("SetMIPMAPMode", 1)) {
        tar->mipFilter = EAGL_ParseTarApi(property->values[0]);
    }
}

// "shape_" and four characters, in one of the original's static buffers.
static char* ShapeName(char *buffer, const char *four) {
    buffer[6] = four[0];
    buffer[7] = four[1];
    buffer[8] = four[2];
    buffer[9] = four[3];
    buffer[10] = 0;
    return buffer;
}

// FUNC_AT(0x000ecbe0)
void* RuntimeAllocTARConstructor(const char *properties, DynamicLoader *loader, void **context, char *destroy) {
    static bool warned;
    if (!warned) {
        warned = true;
        Untested("RuntimeAllocTARConstructor");
    }
    Properties p;
    p.Construct(properties);
    TARSlot *tars = NULL;
    int count = 1;
    for (int i = 0; i < p.count; i++) {
        Property *property = &p.list[i];
        if (strcmp("UID", property->name) == 0) {
            *destroy = 1;
        } else if (strcmp("SHAPENAME", property->name) == 0) {
            if (tars != NULL)
                continue;
            const char *shape = property->values[0];
            count = atol(property->values[1]);
            char *name = ShapeName(TarShapeName, shape);
            bool found;
            void *image = GlobalPool.Search(name, &found);
            if (!found) {
                void *unused;
                loader->GetAddr("SHAPE", name, &unused);
            }
            if (image == NULL)
                image = (uint8_t *)BuiltInShapes + BuiltInShapes->entries[0].offset;
            tars = (TARSlot *)EaglMalloc(count * sizeof(TARSlot), NULL);
            if (tars != NULL)
                tars->tar.Construct((uint8_t *)image);
        } else if (tars == NULL) {
            continue;
        } else if (property->Is("CLUTNAME", 1)) {
            char *name = ShapeName(TarClutName, property->values[0]);
            bool found;
            void *clut = GlobalPool.Search(name, &found);
            if (!found) {
                void *unused;
                loader->GetAddr("SHAPE", name, &unused);
            } else if (clut != NULL) {
                tars->tar.SwapClut((uint8_t *)clut);
            }
        } else if (strncmp("XBOXEXTOBJ", property->name, 10) == 0) {
            // The extension's setters, inlined: the extension field points at the TAR itself
            EAGL::TAR *extension = tars->tar.extension;
            if (property->Is("XBOXEXTOBJ_SetStage", 1)) {
                extension->stage = EAGL_ParseTarValue(property->values[0]);
            } else if (property->Is("XBOXEXTOBJ_SetAnisotropy", 1)) {
                extension->maxAnisotropy = atol(property->values[0]);
            } else if (property->Is("XBOXEXTOBJ_SetBumpEnvMatrix", 4)) {
                float m0 = (float)atof(property->values[0]);
                float m1 = (float)atof(property->values[1]);
                float m2 = (float)atof(property->values[2]);
                float m3 = (float)atof(property->values[3]);
                // called as the original does, on the extension field (its `this` is the field's address)
                reinterpret_cast<EAGL::TARExtension *>(&tars->tar.extension)->SetBumpEnvMatrix(m0, m1, m2, m3);
            } else if (property->Is("XBOXEXTOBJ_SetClampU", 1)) {
                extension->addressU = EAGL_ParseTarValue(property->values[0]);
            } else if (property->Is("XBOXEXTOBJ_SetClampV", 1)) {
                extension->addressV = EAGL_ParseTarValue(property->values[0]);
            } else if (property->Is("XBOXEXTOBJ_SetClampW", 1)) {
                extension->addressW = EAGL_ParseTarValue(property->values[0]);
            }
        } else {
            EAGL_SetTarApi(&tars->tar, property);
        }
    }
    // The rest of the array: each element's defaults, then the first element's fields up to its extension pointer
    // copied over them, and one more reference on the shared image.
    for (int k = 1; k < count; k++) {
        EAGL::TAR *e = &tars[k].tar;
        if (e == NULL)   // the compiled placement new's check
            continue;
        e->address0 = 1;
        e->addressU = 1;
        e->addressV = 1;
        e->addressW = 1;
        e->stage = 0;
        e->lodBias = 0.0f;
        e->palette = NULL;
        e->clut = NULL;
        e->data = NULL;
        e->bumpEnv[1] = 0.0f;
        e->bumpEnv[2] = 0.0f;
        e->filter = 2;
        e->mipFilter = 2;
        e->committed = 0;
        e->maxAnisotropy = 4;
        e->bumpEnv[0] = 1.0f;
        e->bumpEnv[3] = 1.0f;
        e->extension = e;
        memcpy(e, &tars->tar, offsetof(EAGL::TAR, extension));
        e->data->refCount++;
    }
    *context = (void *)count;
    p.Destruct();
    return tars;
}

// FUNC_AT(0x000ed2d0)
void RuntimeAllocTARDestructor(void *array, int count) {
    TARSlot *tars = (TARSlot *)array;
    for (int i = 0; i < count; i++)
        tars[i].tar.Destruct();
    EaglFree(array, count * sizeof(TARSlot));
}

// ---- EAGL::GeoPrimState from properties (a GeoPrimStateExtension, 0x4c)

// FUNC_AT(0x000f0c50)
void EAGL_SetGeoPrimState(EAGL::GeoPrimState *state, Property *property) {
    if (property->Is("SetPrimitiveType", 1)) {
        state->primitiveType = EAGL_ParseGeoPrimState(property->values[0]);
    } else if (property->Is("SetShading", 1)) {
        state->shading = EAGL_ParseGeoPrimState(property->values[0]);
    } else if (property->Is("SetCullEnable", 1)) {
        state->cullEnable = EAGL_ParseTrue(property->values[0]);
    } else if (property->Is("SetTextureEnable", 1)) {
        state->textureEnable = EAGL_ParseTrue(property->values[0]);
    } else if (property->Is("SetTextureCoordType", 1)) {
        EAGL_ParseGeoPrimState(property->values[0]);   // parsed, not kept
    } else if (property->Is("SetDepthTestMethod", 1)) {
        state->depthTestMethod = EAGL_ParseGeoPrimState(property->values[0]);
    } else if (property->Is("SetTransparencyMethod", 1)) {
        state->transparencyMethod = EAGL_ParseGeoPrimState(property->values[0]);
    } else if (property->Is("SetChromaColour", 1)) {
        EAGL_ParseHex(property->values[0]);            // parsed, not kept
    } else if (property->Is("SetAlphaBlendMode", 1)) {
        state->SetAlphaBlendMode(EAGL_ParseGeoPrimState(property->values[0]));
    } else if (property->Is("SetAlphaTestEnable", 1)) {
        state->alphaTestEnable = EAGL_ParseTrue(property->values[0]);
    } else if (property->Is("SetAlphaCompareValue", 1)) {
        state->alphaCompareValue = atol(property->values[0]);
    } else if (property->Is("SetAlphaTestMethod", 1)) {
        state->alphaTestMethod = EAGL_ParseGeoPrimState(property->values[0]);
    }
}

// FUNC_AT(0x000ef4c0)
void* RuntimeAllocGeoPrimStateConstructor(const char *properties, DynamicLoader *loader, void **context,
                                          char *destroy) {
    (void)loader;
    (void)context;
    static bool warned;
    if (!warned) {
        warned = true;
        Untested("RuntimeAllocGeoPrimStateConstructor");
    }
    Properties p;
    p.Construct(properties);
    EAGL::GeoPrimStateExtension *state =
        (EAGL::GeoPrimStateExtension *)EaglMalloc(sizeof(EAGL::GeoPrimState), "EAGL::GeoPrimState new");
    if (state != NULL)
        state->Construct();
    for (int i = 0; i < p.count; i++) {
        Property *property = &p.list[i];
        if (strcmp("UID", property->name) == 0) {
            *destroy = 1;
        } else if (strncmp("XBOXEXTOBJ", property->name, 10) == 0) {
            if (property->Is("XBOXEXTOBJ_SetCullDirection", 1)) {
                state->cullDirection = EAGL_ParseXboxGeoPrimState(property->values[0]);
            } else if (property->Is("XBOXEXTOBJ_SetFillMode", 1)) {
                state->fillMode = EAGL_ParseXboxGeoPrimState(property->values[0]);
            } else if (property->Is("XBOXEXTOBJ_SetBlendOperation", 1)) {
                state->blendOperation = EAGL_ParseXboxGeoPrimState(property->values[0]);
            } else if (property->Is("XBOXEXTOBJ_SetAlphaBlend", 3)) {
                // the last value parsed first (the messages come in that order)
                uint32_t operation = EAGL_ParseXboxGeoPrimState(property->values[2]);
                uint32_t destination = EAGL_ParseXboxGeoPrimState(property->values[1]);
                uint32_t source = EAGL_ParseXboxGeoPrimState(property->values[0]);
                state->blendOperation = operation;
                state->blendSource = source;
                state->blendDestination = destination;
            } else if (property->Is("XBOXEXTOBJ_SetZOffset", 1)) {
                state->zOffset = (float)atof(property->values[0]);
            } else if (property->Is("XBOXEXTOBJ_SetZSlopeScale", 1)) {
                state->zSlopeScale = (float)atof(property->values[0]);
            } else if (property->Is("XBOXEXTOBJ_SetBlendColour", 1)) {
                state->blendColour = EAGL_ParseHex(property->values[0]);
            } else if (property->Is("XBOXEXTOBJ_SetZWritesEnable", 1)) {
                state->zWritesEnable = EAGL_ParseTrue(property->values[0]) ? 1 : 0;
            }
        } else {
            EAGL_SetGeoPrimState(state, property);
        }
    }
    p.Destruct();
    return state;
}

// FUNC_AT(0x000ef780)
void RuntimeAllocGeoPrimStateDestructor(void *state, int unused) {
    (void)unused;
    if (state != NULL)
        EaglFree(state, sizeof(EAGL::GeoPrimState));
}
