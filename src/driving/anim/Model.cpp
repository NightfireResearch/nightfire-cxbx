#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "Model.h"

#include "Manager.h"
#include "../data/Tree.h"               // kOutOfRangeVtable
#include "../eagl/EaglGlobals.h"
#include "../eagl/Loader.h"
#include "../eagl/Tar.h"
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../../helpers.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// ActModel, ActModelDatabase, ActTextureDatabase and their URefCounters' trees (0x00017450-0x000183d0,
// 0x00019e30-0x0001a9a0), ported from the listings.
//
// A character's models are its object files 1.dat..3.dat, each loaded with its symbols file (1.rel..), whose
// "Model" symbols are the character's name and the file's number; a character of type C or A has only the first,
// used for all three. The trees' code is RefCounterTree's (engine/URefCounter.cpp), each compiled copy an entry here.
// ---------------------------------------------------------------------------------------------------------------

// ---- the C runtime's, at their addresses

#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)
#define CRT_printf ((int (*)(const char *format, ...))0x00132192)

static const char kActorShapes[] = "data\\Render\\Actors.xsh";
constexpr int kSymbolFileFlags = 0x100;   // FileLoadz's flags for the symbols file, as the original passes them

// ---- ActModel

// FUNC_AT(0x000177b0)
ActModel* ActModel::Construct(ActCharacterInfo *info) {
    count = 0;
    for (int i = 0; i < kMaxModels; i++) {
        models[i] = NULL;
        files[i] = NULL;
        loaders[i] = NULL;
        for (int tar = 0; tar < kTarCount; tar++)
            tars[i][tar] = NULL;
    }
    Load(info);
    return this;
}

// Object file `i` and its symbols, its loader and TARs, and its "Model" symbol `name`; the symbols file is freed.
static void LoadObject(ActModel *model, ActCharacterInfo *info, int i, const char *name) {
    model->files[i] = UFileLoader::FileLoadz(info->GetModelFileName(i), 0);
    size_t size = UMemory::Size(model->files[i]);
    void *symbols = UFileLoader::FileLoadz(info->GetModelSymbolFileName(i), kSymbolFileFlags);
    DynamicLoader *memory = static_cast<DynamicLoader *>(EaglMalloc(sizeof(DynamicLoader), "EAGL::DynamicLoader new"));
    model->loaders[i] = memory != NULL
        ? memory->ConstructSplit(model->files[i], size, symbols, 0, ActManager::GetSymbolResolver())
        : NULL;
    model->FindTars(i, model->loaders[i]);
    void *symbol;
    model->loaders[i]->GetAddr("Model", name, &symbol);
    model->models[i] = static_cast<EAGL::Model *>(symbol);
    model->loaders[i]->Release();
    UMemory::Free(symbols);
}

// FUNC_AT(0x00017590)
void ActModel::Load(ActCharacterInfo *info) {
    char name[0x20];
    if (info->type == kCharacterC || info->type == kCharacterA) {
        count = 1;
        sprintf(name, "%s1", info->name);
        LoadObject(this, info, 0, name);
        models[2] = models[1] = models[0];
        return;
    }
    count = kMaxModels;
    for (int i = 0; i < kMaxModels; i++) {
        sprintf(name, "%s%d", info->name, i + 1);
        LoadObject(this, info, i, name);
    }
}

// FUNC_AT(0x000177f0)
void ActModel::UnLoad() {
    for (int i = 0; i < kMaxModels; i++) {
        if (files[i] == NULL)
            continue;
        if (loaders[i] != NULL) {
            loaders[i]->Destruct();
            EaglFree(loaders[i], sizeof(DynamicLoader));
        }
        UMemory::Free(files[i]);
        files[i] = NULL;
    }
}

// FUNC_AT(0x00017480)
void ActModel::FindTars(int model, DynamicLoader *loader) {
    int index = 0;
    void *tar;
    while (loader->GetNextAddr("EAGL::TAR", &index, &tar)) {
        LoaderSymbol symbol;
        loader->GetSymbol(&symbol, index - 1);
        if (strncmp(symbol.name, "tar_bbbb", 8) == 0)
            tars[model][kTarB] = static_cast<EAGL::TAR *>(tar);
        if (strncmp(symbol.name, "tar_hhhh", 8) == 0)
            tars[model][kTarH] = static_cast<EAGL::TAR *>(tar);
        if (strncmp(symbol.name, "tar_tttt", 8) == 0)
            tars[model][kTarT] = static_cast<EAGL::TAR *>(tar);
    }
}

// FUNC_AT(0x00017450)
void ActModel::SetTexture(int model, int tar, EAGL::TAR *texture) {
    if (texture != NULL)
        tars[model][tar]->SwapShape(texture->GetShape());
}

// ---- ActModelDatabase

// FUNC_AT(0x000183d0)
ActModelDatabase* ActModelDatabase::Construct() {
    memset(models, 0, sizeof(models));
    count = 0;
    refs = ModelInfoRefCounter::Get();
    return this;
}

// FUNC_AT(0x00018110)
void ActModelDatabase::Destruct() {
    for (int i = 0; i < count; i++) {
        ActModelInfo *info = refs->GetReference(models[i]->name);
        refs->RemoveReference(info);
        ActModel *model = info->model;
        if (model != NULL) {
            model->UnLoad();
            UMemory::FastFree(model, sizeof(ActModel));
        }
        OperatorDelete(info);
        models[i] = NULL;
    }
    count = 0;
}

// FUNC_AT(0x000181a0)
void ActModelDatabase::LoadModel(ActCharacterInfo *info) {
    if (refs->GetReference(info->name) != NULL)
        return;
    models[count] = static_cast<ActModelInfo *>(OperatorNew(sizeof(ActModelInfo)));
    strcpy(models[count]->name, info->name);
    ActModel *memory = static_cast<ActModel *>(UMemory::FastAlloc(sizeof(ActModel), "ActModel"));
    models[count]->model = memory != NULL ? memory->Construct(info) : NULL;
    refs->AddReference(info->name, models[count]);
    count++;
}

// FUNC_AT(0x00017840)
ActModel* ActModelDatabase::UseModel(ActCharacterInfo *info) {
    return refs->GetReference(info->name)->model;
}

// ---- ActTextureDatabase

// FUNC_AT(0x0001a9a0)
ActTextureDatabase* ActTextureDatabase::Construct() {
    memset(textures, 0, sizeof(textures));
    shapes = static_cast<uint8_t *>(UFileLoader::FileLoadz(kActorShapes, 0));
    DynamicLoader::RegisterShapes(shapes);
    count = 0;
    refs = TextureInfoRefCounter::Get();
    LoadTexture("hhhh", kActorShapes);
    return this;
}

// FUNC_AT(0x0001a6c0)
void ActTextureDatabase::Destruct() {
    for (int i = 0; i < count; i++) {
        ActTextureInfo *info = refs->GetReference(textures[i]->name);
        refs->RemoveReference(info);
        EAGL::TAR *tar = info->tar;
        if (tar != NULL) {
            tar->Destruct();
            EaglFree(tar, sizeof(EAGL::TAR));
        }
        UMemory::Free(info->file);
        OperatorDelete(info);
        textures[i] = NULL;
    }
    count = 0;
    DynamicLoader::UnRegisterShapes(shapes);
    UMemory::Free(shapes);
}

// FUNC_AT(0x0001a770)
void ActTextureDatabase::LoadTexture(const char *name, const char *path) {
    if (refs->GetReference(name) != NULL)
        return;
    textures[count] = static_cast<ActTextureInfo *>(OperatorNew(sizeof(ActTextureInfo)));
    ActTextureInfo *info = textures[count];
    strcpy(info->name, name);
    info->file = static_cast<ShapeFile *>(UFileLoader::FileLoadz(path, 0));
    EAGL::TAR *memory = static_cast<EAGL::TAR *>(EaglMalloc(sizeof(EAGL::TAR), "EAGL::TAR new"));
    info->tar = memory != NULL
        ? memory->Construct(reinterpret_cast<uint8_t *>(info->file) + info->file->entries[0].offset)
        : NULL;
    refs->AddReference(name, info);
    count++;
}

// FUNC_AT(0x00019e30)
EAGL::TAR* ActTextureDatabase::UseTexture(const char *name) {
    ActTextureInfo *info = refs->GetReference(name);
    if (info == NULL)
        CRT_printf("Trying to load texture %s\n", name);
    return info->tar;
}

// ---- the trees

// FUNC_AT(0x00017860)
void ModelInfoRefTree::EraseSubtree(RefCounterNode *node) {
    RefCounterTree::EraseSubtree(node);
}

// FUNC_AT(0x000178e0)
RefCounterNode** ModelInfoRefTree::EraseAt(RefCounterNode **result, RefCounterNode *where) {
    return RefCounterTree::EraseAt(result, where);
}

// FUNC_AT(0x00017c70)
RefCounterNode** ModelInfoRefTree::InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where, const RefCounterValue *value) {
    return RefCounterTree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x00017e60)
RefCounterNode** ModelInfoRefTree::EraseRange(RefCounterNode **result, RefCounterNode *first, RefCounterNode *last) {
    return RefCounterTree::EraseRange(result, first, last);
}

// FUNC_AT(0x00017f50)
RefCounterInsertResult* ModelInfoRefTree::InsertUnique(RefCounterInsertResult *result, const RefCounterValue *value) {
    return RefCounterTree::InsertUnique(result, value);
}

// FUNC_AT(0x00018270)
void ModelInfoRefTree::DestroyRange() {
    RefCounterTree::DestroyRange();
}

// FUNC_AT(0x00019e60)
void TextureInfoRefTree::EraseSubtree(RefCounterNode *node) {
    RefCounterTree::EraseSubtree(node);
}

// FUNC_AT(0x00019eb0)
RefCounterNode** TextureInfoRefTree::EraseAt(RefCounterNode **result, RefCounterNode *where) {
    return RefCounterTree::EraseAt(result, where);
}

// FUNC_AT(0x0001a220)
RefCounterNode** TextureInfoRefTree::InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where, const RefCounterValue *value) {
    return RefCounterTree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x0001a410)
RefCounterNode** TextureInfoRefTree::EraseRange(RefCounterNode **result, RefCounterNode *first, RefCounterNode *last) {
    return RefCounterTree::EraseRange(result, first, last);
}

// FUNC_AT(0x0001a500)
RefCounterInsertResult* TextureInfoRefTree::InsertUnique(RefCounterInsertResult *result, const RefCounterValue *value) {
    return RefCounterTree::InsertUnique(result, value);
}

// FUNC_AT(0x0001a840)
void TextureInfoRefTree::DestroyRange() {
    RefCounterTree::DestroyRange();
}

// ---- std::out_of_range: only a throw reaches these (provisional, untested)

#define OutOfRangeVtable ((const void *)kOutOfRangeVtable)

static void ActModelUntested(const char *what) {
    printf("[anim] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define ACTMODEL_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            ActModelUntested(what); \
        } \
    } while (0)

// FUNC_AT(0x00017c50)
OutOfRangeError* OutOfRangeError::ConstructCopy(const OutOfRangeError *other) {
    ACTMODEL_UNTESTED("out_of_range copy constructor");
    LogicError::ConstructCopy(other);
    vtable = OutOfRangeVtable;
    return this;
}

// FUNC_AT(0x000178d0)
void OutOfRangeError::Destruct() {
    ACTMODEL_UNTESTED("out_of_range destructor");
    vtable = OutOfRangeVtable;
    LogicError::Destruct();
}

// FUNC_AT(0x000178b0)
OutOfRangeError* OutOfRangeError::Delete(unsigned flags) {
    ACTMODEL_UNTESTED("out_of_range deleting destructor");
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}
