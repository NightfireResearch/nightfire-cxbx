#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "RCARPFile.h"
#include "Carp.h"
#include "../eagl/GameCallbacks.h"
#include "../eagl/Loader.h"
#include "../engine/UFileLoader.h"
#include "../platform/RealMemory.h"
#include "../platform/RealPrint.h"
#include "../../common/xbeOverload.h"
#include "../../helpers.h"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// RCARPFile, its loader, the global symbol table and the symbol callbacks. See RCARPFile.h.
// ---------------------------------------------------------------------------------------------------------------

// Other packages' functions not ported yet, called at their addresses.

// The renderer's (not ported yet).
#define RRenderer_Flush ((void (__fastcall *)(void *, int, int))0x0007d070)
#define RTextureContextManager_GetContext ((RTextureContext *(*)(int))0x000940c0)
#define RTextureContextManager_FindOrCreateTexture ((void *(__fastcall *)(void *, int, uint32_t, int))0x00093fb0)
#define RTextureContextManager_NewContext ((RTextureContext *(__fastcall *)(void *, int, const char *, int))0x000951e0)
#define RTextureContextManager_KillContext ((void (__fastcall *)(void *, int, RTextureContext *))0x00094c90)
#define RTextureContext_FindOrCreateTexture ((void *(__fastcall *)(RTextureContext *, int, uint32_t, int))0x00093ba0)
#define RReflection_GetReflectionMapWarpageData ((void *(__fastcall *)(void *, int))0x00098210)
#define RReflection_TextureSpecular ((void *(__fastcall *)(void *, int))0x00098490)
#define RReflection_GetReflectionCarPos ((void *(__fastcall *)(void *, int))0x00098470)
#define RReflection_Texture ((void *(__fastcall *)(void *, int))0x000984a0)
#define RReflection_TextureWeaponEnvMap ((void *(__fastcall *)(void *, int))0x00098480)
#define RReflection_GetReflectionMatrix ((void *(__fastcall *)(void *, int))0x000984b0)
#define RReflection_LightingProps ((void *(__fastcall *)(void *, int, int, const char *))0x00098380)
#define RReflection_GetReflectionData2 ((void *(__fastcall *)(void *, int, const char *))0x00098410)
#define RReflection_GetNull ((void *(__fastcall *)(void *, int))0x000f7330)
#define CarpPathConcat ((void (__fastcall *)(char *, int, const char *, const char *, const char *))0x00051e90)
#define EhVectorConstructor ((void (__stdcall *)(void *, uint32_t, uint32_t, uint32_t))0x00022770)
#define Crt_printf ((int (*)(const char *, ...))0x00132192)
#define Crt_atexit ((int (*)(void (*)()))0x00132a7b)

#define Symbols (*(USymbolTable **)0x001ebcdc)
#define CurrentCarpName (*(const char **)0x001ebce0)   // the file being resolved: lighting is per file
#define MaterialData PTR_AT(0x001ebce4)
#define Callbacks ((SymbolCallback *)0x001ebce8)       // [16]
#define CharNamespace (*(UCharNamespace *)0x001ebd60)
#define CharNamespaceGuard U32_AT(0x001ebd64)
#define Bond (*(uint8_t **)0x001e464c)                 // its symbol table pointer at 0x64
#define EaglAllocate (*(void *(**)(uint32_t, const char *))0x001caf68)   // EAGL's allocator hooks
#define EaglFree (*(void (**)(void *, uint32_t))0x001caf6c)
#define TextureManager PTR_AT(0x001f2a30)
#define Reflection PTR_AT(0x001f2dfc)
#define Renderer (*(uint8_t **)0x001ebff4)
#define Lighting (*(uint8_t **)0x001ec260)
#define Fog (*(uint8_t **)0x001ec004)
#define UVAnimation (*(uint8_t **)0x00201844)
#define Colours (*(uint32_t **)0x001ebd2c)
#define ShadowAnim ((float *)0x001ebd50)               // [3]
#define CarShadowColour ((uint32_t *)0x001ebd30)       // [4], a function-local static
#define CarShadowColourGuard U32_AT(0x001ebd40)
#define TextureNames ((const char **)0x001c3ec0)       // "TEX0".."TEX9"
#define MaterialFiles ((const char **)0x001c3f04)      // "eaglrm.o", "bondrm.o", null

// The game's data RegisterSymbols hands out by address.
#define SwitchData ((uint8_t *)0x001f2a50)             // 16-byte entries
#define DamageZones ((void *)0x001f2b50)
#define SimStep ((void *)0x001c465c)
#define SwayData ((void *)0x001c466c)
#define CausticControl ((void *)0x001c467c)
#define TestData ((void *)0x001c45d0)
#define ReflStretch ((void *)0x001c3ef0)

constexpr uint32_t kGlobalSymbolTableVtable = 0x00190e18;
constexpr uint32_t kEAGLNamespaceVtable = 0x001913b4;
constexpr uint32_t kCharNamespaceAtExit = 0x0015ce40;
constexpr int kCallbackSlots = 16;
constexpr uint32_t kCallbackAt0007ae90 = 0x0007ae90;   // another package's callback
constexpr uint32_t kCallbackAt000a59c0 = 0x000a59c0;   // ditto
constexpr uint32_t kRegisterSymbolsAt = 0x0007b020;
constexpr uint32_t kResolveEAGLReferencesAt = 0x0007b8a0;

constexpr uint32_t kTagTextureFile = 0x736e2020;   // 'sn  ' (indexed: the TEXn number)
constexpr uint32_t kTagModel = 0x454c4664;         // 'dFLE'
constexpr uint32_t kTagModelRelocations = 0x454c4672;   // 'rFLE'
constexpr uint32_t kTagSect = 0x53656374;          // 'Sect'

// Texture tags RegisterSymbols looks up.
constexpr uint32_t kTextureWater = 0x32746177;     // 'wat2'
constexpr uint32_t kTextureReflection = 0x66657262;   // 'bref'
constexpr uint32_t kTextureSpecular = 0x63657073;  // 'spec'
constexpr uint32_t kTextureStretch = 0x68637473;   // 'stch'
constexpr uint32_t kTextureTest = 0x74736574;      // 'test'
constexpr uint32_t kTextureActorB = 0x62544341;    // 'ACTb'
constexpr uint32_t kTextureActorH = 0x68544341;    // 'ACTh'
constexpr uint32_t kTextureActorT = 0x74544341;    // 'ACTt'

// RReflection::LightingProps's material kinds.
enum LightingKind {
    kLightingGlass = 0,
    kLightingSpecular = 1,
    kLightingGlossy = 2,
    kLightingDull = 3,
    kLightingChrome = 4,
    kLightingDash = 5,
};

// What RCARPFile reads of a texture context: its shapes, for EAGL's registry.
struct TextureContextView {
    uint32_t unknown00[2];
    uint8_t *shapes;
};

static uint8_t *Shapes(RTextureContext *context) {
    return reinterpret_cast<TextureContextView *>(context)->shapes;
}

// The 'Sect' record: the sizes of the file's sections that resolving leaves unused, at its end.
struct CarpSections {
    uint8_t unknown00[0x60];
    uint32_t discardable[8];
};

static UData *DataEnd(UGroup *group) {
    return group->GetArray() + (group->GroupCount() + group->count);
}

static SymbolCallback CallbackAt(uint32_t address) {
    return reinterpret_cast<SymbolCallback>(uintptr_t(address));
}

// ---- the symbol table and the callbacks

// FUNC_AT(0x0007bd60)
void GlobalSymbolTable::Init() {
    if (Symbols != NULL)
        return;
    USymbolTable *table = static_cast<USymbolTable *>(UMemory::FastAlloc(sizeof(USymbolTable), "USymbolTable"));
    if (table != NULL) {
        table->Construct();
        table->vtable = reinterpret_cast<void *>(uintptr_t(kGlobalSymbolTableVtable));
    }
    Symbols = table;
    if (!(CharNamespaceGuard & 1)) {
        CharNamespaceGuard |= 1;
        CharNamespace.Construct();
        Crt_atexit(reinterpret_cast<void (*)()>(uintptr_t(kCharNamespaceAtExit)));
    }
    Symbols->AddNamespace("CHAR", &CharNamespace);
    Symbols->AddNamespace("DATA", &CharNamespace);
    *reinterpret_cast<USymbolTable **>(Bond + 0x64) = Symbols;
}

// FUNC_AT(0x0007af50)
void GlobalSymbolTable::Kill() {
    if (Symbols != NULL) {
        Symbols->RemoveNamespace("CHAR");
        Symbols->RemoveNamespace("DATA");
        USymbolTable *table = Symbols;
        if (table != NULL)   // the derived class's deleting destructor, slot 0
            (table->*XbeVirtual<decltype(&USymbolTable::Delete)>(table, 0))(1);
    }
    Symbols = NULL;
}

// FUNC_AT(0x0007af90)
void RegisterCallback::Add(SymbolCallback callback) {
    for (int i = 0; i < kCallbackSlots; i++) {
        if (Callbacks[i] == NULL || Callbacks[i] == callback) {
            Callbacks[i] = callback;
            return;
        }
    }
}

// FUNC_AT(0x0007afc0)
void RegisterCallback::Remove(SymbolCallback callback) {
    for (int i = 0; i < kCallbackSlots; i++)
        if (Callbacks[i] == callback)
            Callbacks[i] = NULL;
}

// FUNC_AT(0x0007afe0)
void* RegisterCallback::Resolve(const char *name, bool *found) {
    void *value = NULL;
    for (int i = 0; i < kCallbackSlots; i++) {
        if (Callbacks[i] != NULL) {
            value = Callbacks[i](name, found);
            if (value != NULL)
                break;
        }
    }
    return value;
}

// FUNC_AT(0x0007be40)
void RegisterCallback::AddMajorCallbacks() {
    Add(CallbackAt(kCallbackAt0007ae90));
    Add(CallbackAt(kCallbackAt0007ae90));   // twice, as the original's inlined code does
    Add(CallbackAt(kRegisterSymbolsAt));
    Add(CallbackAt(kCallbackAt000a59c0));
}

// FUNC_AT(0x0007b8a0)
void* ResolveEAGLReferences(const char *name, bool *found) {
    int size = 0;
    void *value = Symbols->NameLookup(name, &size);
    *found = value != NULL;
    if (value == NULL)
        value = RegisterCallback::Resolve(name, found);
    return value;
}

// ---- RegisterSymbols

static bool Has(const char *name, const char *what) {
    return strstr(name, what) != NULL;
}

static bool Is(const char *name, const char *what) {
    return strcmp(name, what) == 0;   // the original's repe cmpsb over the name and its terminator
}

static void *ContextTexture(uint32_t tag) {
    return RTextureContext_FindOrCreateTexture(RTextureContextManager_GetContext(0), 0, tag, 0);
}

// FUNC_AT(0x0007b020)
void* RCARPFile::RegisterSymbols(const char *name, bool *found) {
    // Every test runs; a later match replaces an earlier one ("GAME::ReflectionMap1" ends as ReflectionMap).
    void *value = NULL;
    if (Has(name, "GAME::SwitchData[")) {
        if (name[0x12] == ']')
            value = SwitchData + (name[0x11] - '0') * 16;
        else
            value = SwitchData + ((name[0x11] - '0') * 10 + (name[0x12] - '0')) * 16;
    } else if (Has(name, "GAME::SwitchData")) {
        value = SwitchData + 8 * 16;
    }
    if (Has(name, "GAME::WMap") || Has(name, "EAGL::WMap"))
        value = ContextTexture(kTextureWater);
    if (Has(name, "GAME::DamageZones") || Has(name, "EAGL::DamageZones"))
        value = DamageZones;
    if (Has(name, "GAME::FishEyeParams"))
        value = RReflection_GetReflectionMapWarpageData(Reflection, 0);
    if (Has(name, "GAME::ReflectionMap1"))
        value = RReflection_TextureSpecular(Reflection, 0);
    if (Has(name, "GAME::ReflectionCarPos"))
        value = RReflection_GetReflectionCarPos(Reflection, 0);
    if (Has(name, "GAME::ReflectionMap"))
        value = RReflection_Texture(Reflection, 0);
    if (Has(name, "GAME::WeaponReflectionMap"))
        value = RReflection_TextureWeaponEnvMap(Reflection, 0);
    if (Has(name, "GAME::ReflMatrix"))
        value = RReflection_GetReflectionMatrix(Reflection, 0);
    if (Has(name, "GAME::SpecularMap"))
        value = RReflection_TextureSpecular(Reflection, 0);
    if (Has(name, "GAME::SphereMap"))
        value = RReflection_Texture(Reflection, 0);
    if (Has(name, "GAME::CarLightingGlassProps"))
        value = RReflection_LightingProps(Reflection, 0, kLightingGlass, CurrentCarpName);
    if (Has(name, "GAME::CarLightingSpecularProps"))
        value = RReflection_LightingProps(Reflection, 0, kLightingSpecular, CurrentCarpName);
    if (Has(name, "GAME::CarLightingGlossyProps"))
        value = RReflection_LightingProps(Reflection, 0, kLightingGlossy, CurrentCarpName);
    if (Has(name, "GAME::CarLightingDashProps"))
        value = RReflection_LightingProps(Reflection, 0, kLightingDash, CurrentCarpName);
    if (Has(name, "GAME::CarLightingChromeProps"))
        value = RReflection_LightingProps(Reflection, 0, kLightingChrome, CurrentCarpName);
    if (Has(name, "GAME::CarLightingDullProps"))
        value = RReflection_LightingProps(Reflection, 0, kLightingDull, CurrentCarpName);
    if (Has(name, "GAME::CharacterLightingDullProps"))
        value = RReflection_LightingProps(Reflection, 0, kLightingDull, "Character");
    if (Has(name, "GAME::CharacterLightingGlossyProps"))
        value = RReflection_LightingProps(Reflection, 0, kLightingGlossy, "Character");
    if (Has(name, "GAME::DynamicObjectLightingProps"))
        value = RReflection_LightingProps(Reflection, 0, kLightingDull, "Dynamic Objects");
    if (Has(name, "GAME::ReflectionInfo1")) {
        // The original answers the address of a 16-byte local of its own frame, dead once it returns; a static
        // buffer stands in for it. No shipped model asks for this name.
        static uint8_t reflectionInfo1[16];
        value = reflectionInfo1;
    }
    if (Has(name, "GAME::ReflectionInfo2") || Has(name, "GAME::WorldSpecularInfo"))
        value = RReflection_GetReflectionData2(Reflection, 0, CurrentCarpName);
    if (Has(name, "GAME::CharacterReflectionInfo"))
        value = RReflection_GetReflectionData2(Reflection, 0, "Character");
    if (Is(name, "EAGL::FogData"))
        value = *reinterpret_cast<void **>(Fog + 4);
    if (Has(name, "GAME::UVScale") || Has(name, "EAGL::UVScale"))
        value = UVAnimation + (name[0xd] - 0x2c) * 16;   // the digit after the name: entries 4..
    if (Has(name, "GAME::UVScroll") || Has(name, "EAGL::UVScroll"))
        value = UVAnimation + (name[0xe] - 0x26) * 16;   // entries 10..
    if (Is(name, "GAME::WaterRotToSun") || Is(name, "EAGL::WaterRotToSun"))
        value = UVAnimation + 0x10;
    if (Is(name, "GAME::SimStep"))
        value = SimStep;
    if (Is(name, "GAME::SwayData"))
        value = SwayData;
    if (Is(name, "GAME::CausticControl"))
        value = CausticControl;
    if (Is(name, "EAGL::ShadowAnim")) {
        ShadowAnim[0] = 0.003f;
        ShadowAnim[1] = 0.15f;
        ShadowAnim[2] = 0.0f;
        value = ShadowAnim;
    }
    if (Is(name, "GAME::TestData"))
        value = TestData;
    if (Is(name, "EAGL::CarShadowColour")) {
        if (!(CarShadowColourGuard & 1)) {
            CarShadowColourGuard |= 1;
            EhVectorConstructor(CarShadowColour, 4, 4, 0x00076240);   // four colours, each constructed to 0
        }
        CarShadowColour[0] = 0x80303030;
        value = CarShadowColour;
    }
    if (Is(name, "GAME::LightVector"))
        value = Lighting + 0x10;
    if (Is(name, "GAME::LightSources"))
        value = *reinterpret_cast<void **>(Lighting + 0x78);
    if (Is(name, "GAME::ActorLightSources"))
        value = Lighting + 0xec;
    if (Is(name, "GAME::PositionalLights"))
        value = *reinterpret_cast<void **>(Lighting + 0x2a0);
    if (Is(name, "GAME::CurrentAmbientDiffuse"))
        value = Lighting + 0x2a4;
    if (Is(name, "EAGL::EnvironmentMap")) {
        void *texture = RTextureContextManager_FindOrCreateTexture(TextureManager, 0, kTextureReflection, 4);
        if (texture == NULL) {
            Crt_printf("UNABLE TO FIND REFLECTION MAP IN TEXTURE FILE\n");
            RTextureContextManager_NewContext(TextureManager, 0, "data\\render\\ext.xsh", 5);
            texture = RTextureContextManager_FindOrCreateTexture(TextureManager, 0, kTextureReflection, 5);
        }
        value = texture;
    }
    if (Is(name, "EAGL::SpecularMap")) {
        uint32_t *texture = static_cast<uint32_t *>(ContextTexture(kTextureSpecular));
        texture[1] = 3;   // its four modes (wrap or clamp, filters - not named yet)
        texture[2] = 3;
        texture[3] = 3;
        texture[4] = 3;
        value = texture;
    }
    if (Is(name, "GAME::StretchTexture")) {
        uint32_t *texture = static_cast<uint32_t *>(ContextTexture(kTextureStretch));
        texture[1] = 1;
        texture[2] = 1;
        texture[3] = 1;
        texture[4] = 1;
        value = texture;
    }
    if (Is(name, "GAME::TestTexture"))
        value = ContextTexture(kTextureTest);
    if (Is(name, "EAGL::EnvMapState2C"))
        value = RReflection_GetNull(Reflection, 0);
    if (Is(name, "EAGL::EnvMapState2G"))
        value = RReflection_GetNull(Reflection, 0);
    if (Is(name, "GAME::CameraPos") || Is(name, "EAGL::CameraPos"))
        value = Renderer + 0x30;
    if (Is(name, "GAME::SpecularMat"))
        value = Lighting + 0x20;
    if (Is(name, "EAGL::ReflStretch"))
        value = ReflStretch;
    if (Is(name, "EAGL::Colours")) {
        if (Colours == NULL) {
            Colours = static_cast<uint32_t *>(UMemory::Alloc(0x190, 0, "EAGL::Colours hack"));
            MEM_fill(Colours, 0x80808080, 0x190);
        }
        value = Colours;
    }
    if (Is(name, "texture_bbbb"))
        value = ContextTexture(kTextureActorB);
    if (Is(name, "texture_hhhh"))
        value = ContextTexture(kTextureActorH);
    if (Is(name, "texture_tttt"))
        value = ContextTexture(kTextureActorT);
    *found = value != NULL;
    return value;
}

// ---- RCARPFile

// FUNC_AT(0x0007bf00)
bool RCARPFile::Load(const char *directory, const char *file, UCarpNamespace *carp, bool resolve) {
    char path[256];
    memset(path, 0, 255);   // the original clears all but the last byte
    strcat(path, directory);
    strcat(path, file);
    RegisterCallback::AddMajorCallbacks();
    data = UFileLoader::FileLoad(path, 0);
    if (data == NULL) {
        data = NULL;   // written again, as the original does
        return false;
    }
    root = UGroup::Deserialize(data, true);
    if (resolve)
        Resolve(directory, carp);
    return true;
}

// FUNC_AT(0x0007b8e0)
void RCARPFile::Resolve(const char *directory, UCarpNamespace *carp) {
    CurrentCarpName = name;
    carp->AddCarpFile(root);

    bool added[kCarpTextureFiles] = {};
    char path[256];
    for (UData *record = root->DataLocateFirst(kTagTextureFile, 0, -1); record != DataEnd(root); record++) {
        if (record->MatchTag() != kTagTextureFile)
            break;
        sprintf(path, "%s%s", directory, reinterpret_cast<char *>(record->Data()));
        RTextureContext *context = RTextureContextManager_NewContext(TextureManager, 0, path, 4);
        Symbols->AddNamespace(TextureNames[record->TagIndex()], reinterpret_cast<SymbolNamespace *>(context));
        textures[record->TagIndex()] = context;
        added[record->TagIndex()] = true;
        DynamicLoader::RegisterShapes(Shapes(context));
    }

    // The model object, as the "EAGL" namespace while the references are resolved.
    EAGLNamespace eagl;
    UData *model = root->DataLocateFirst(kTagModel, -1, -1);
    if (model != DataEnd(root)) {
        void *elf = model->Data();
        void *resolver = reinterpret_cast<void *>(uintptr_t(kResolveEAGLReferencesAt));
        UData *relocations = root->DataLocateFirst(kTagModelRelocations, -1, -1);
        DynamicLoader *created = NULL;
        if (relocations != DataEnd(root)) {
            void *second = relocations->Data();
            void *memory = EaglAllocate(sizeof(DynamicLoader), "EAGL::DynamicLoader new");
            if (memory != NULL)
                created = static_cast<DynamicLoader *>(memory)->ConstructSplit(elf, model->Size(), second, 1,
                                                                               resolver);
        } else {
            void *memory = EaglAllocate(sizeof(DynamicLoader), "EAGL::DynamicLoader new");
            if (memory != NULL)
                created = static_cast<DynamicLoader *>(memory)->Construct(elf, model->Size(), resolver);
        }
        loader = created;
        eagl.vtable = reinterpret_cast<const void *>(uintptr_t(kEAGLNamespaceVtable));
        eagl.loader = created;
        Symbols->AddNamespace("EAGL", reinterpret_cast<SymbolNamespace *>(&eagl));
    }
    CARP::ResolveSymbolicReferences(root, Symbols);
    if (loader != NULL) {
        loader->Resolve();
        loader->Release();
    }

    UData *sect = root->DataLocateTag(kTagSect);
    if (sect != DataEnd(root)) {
        CarpSections *sections = reinterpret_cast<CarpSections *>(sect->Data());
        size_t size = MEM_size(data);
        uint32_t *d = sections->discardable;
        uint32_t discard = d[7] + d[1] + d[6] + d[5] + d[4] + d[3] + d[2] + d[0];
        MEM_resize(data, int(size - discard));
    }
    for (int i = 0; i < kCarpTextureFiles; i++) {
        if (added[i])
            Symbols->RemoveNamespace(TextureNames[i]);
        if (textures[i] != NULL)
            DynamicLoader::UnRegisterShapes(Shapes(textures[i]));
    }
    if (model != DataEnd(root))
        Symbols->RemoveNamespace("EAGL");
}

// FUNC_AT(0x0007bff0)
void RCARPFile::Destruct() {
    RRenderer_Flush(Renderer, 0, 0);
    for (int i = 0; i < kCarpTextureFiles; i++)
        if (textures[i] != NULL)
            RTextureContextManager_KillContext(TextureManager, 0, textures[i]);
    DynamicLoader *object = loader;
    if (object != NULL) {
        object->Destruct();
        EaglFree(object, sizeof(DynamicLoader));
    }
    loader = NULL;
    if (data != NULL) {
        MEM_free_copy(data);
        data = NULL;
    }
}

// FUNC_AT(0x0007bc90)
void RCARPFile::LoadEAGLMaterials() {
    for (const char **file = MaterialFiles; *file != NULL; file++) {
        char path[64];
        CarpPathConcat(path, 0, "data\\render\\", *file, "");
        MaterialData = UFileLoader::FileLoadz(path, 0);
        uint32_t size = UMemory::Size(MaterialData);
        void *memory = EaglAllocate(sizeof(DynamicLoader), "EAGL::DynamicLoader new");
        if (memory != NULL)   // kept loaded for the rest of the run
            static_cast<DynamicLoader *>(memory)->Construct(MaterialData, size,
                                                            reinterpret_cast<void *>(uintptr_t(kResolveEAGLReferencesAt)));
    }
}

// FUNC_AT(0x0008baa0)
void RCARPFile::LoadEAGLMaterialsThunk() {
    LoadEAGLMaterials();
}

// ---- RCARPFileLoader

// FUNC_AT(0x0007c070)
RCARPFileLoader* RCARPFileLoader::Construct() {
    UCarpNamespace::Construct();
    GlobalSymbolTable::Init();
    Symbols->AddNamespace("CARP", this);
    return this;
}

// FUNC_AT(0x0007aef0)
void RCARPFileLoader::Destruct() {
    Symbols->RemoveNamespace("CARP");
    UCarpNamespace::Destruct();
}

// FUNC_AT(0x0007c0d0)
RCARPFile* RCARPFileLoader::LoadCARPFile(const char *directory, const char *file, const char *label, bool resolve) {
    RCARPFile *carp = static_cast<RCARPFile *>(UMemory::FastAlloc(sizeof(RCARPFile), "RCARPFile"));
    if (carp != NULL) {
        carp->name = label != NULL ? label : "Default";
        carp->data = NULL;
        carp->root = NULL;
        carp->loader = NULL;
        memset(carp->textures, 0, sizeof(carp->textures));
    }
    // The original loads through the pointer even when the allocation failed.
    if (!carp->Load(directory, file, this, resolve)) {
        if (carp != NULL) {
            carp->Destruct();
            UMemory::FastFree(carp, sizeof(RCARPFile));
        }
        return NULL;
    }
    return carp;
}

// FUNC_AT(0x0007bd40)
void RCARPFileLoader::ResolveCARPFile(const char *directory, RCARPFile *file) {
    file->Resolve(directory, this);
}
