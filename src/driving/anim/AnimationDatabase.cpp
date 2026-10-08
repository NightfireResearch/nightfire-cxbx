#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "AnimationDatabase.h"

#include "Manager.h"
#include "../data/AttributeSet.h"
#include "../eagl/EaglGlobals.h"
#include "../eagl/Loader.h"
#include "../eagl/anim/AnimObjects.h"   // AnimBank
#include "../eagl/anim/FnAnim.h"
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMemory.h"
#include "../world/World.h"             // fgWorld
#include "../../helpers.h"

#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// ActAnimationDatabase, its banks and ActAnimGroup (0x00013c00-0x00014440), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the C runtime's (sprintf and tolower as the game has them)

#define Crt_sprintf ((int (__cdecl *)(char *, const char *, ...))0x00132767)
#define Crt_tolower ((int (__cdecl *)(int))0x001327f9)

// ---- globals


// The banks a bank list may name; a bank's place here is its place in PrivateData::banks.
// XBE_GLOBAL(0x001b4e48, 0x32a)
static const char BankNames[PrivateData::kMaxBanks][45] = {
    "driving", "essm", "fs", "fsb", "fsmg", "mefs", "ped", "slp", "sm", "sr", "srb", "srmg", "ssm", "swmg",
    "sniper", "tfod", "ul", "worker",
};

constexpr uint32_t kFnDefaultAnimBankVtable = 0x00189f44;
constexpr int kLoadFlags = 0x100;       // UFileLoader's flags for the bank list and the .rel files (the .dat: 0)
static const char kAnimDirectory[] = "data\\actors\\anims\\";

// ---- FnDefaultAnimBank

void FnDefaultAnimBank::Construct() {
    vtable = reinterpret_cast<const void *>(uintptr_t(kFnDefaultAnimBankVtable));
    bank = NULL;
}

// The same code serves several of the renderer's setters.
// FUNC_AT(0x00013c80)
void FnDefaultAnimBank::Init(AnimBank *animBank) {
    bank = animBank;
}

// FUNC_AT(0x00013c90)
int FnDefaultAnimBank::GetNumAnims() {
    return bank->count;
}

// FUNC_AT(0x00013ca0)
uint8_t* FnDefaultAnimBank::GetAnim(int index) {
    return bank->anims[index];
}

// FUNC_AT(0x00013cb0)
const char* FnDefaultAnimBank::GetAnimName(int index) {
    return bank->names[index];
}

// FUNC_AT(0x00013cc0)
uint8_t* FnDefaultAnimBank::GetAnim(const char *name) {
    int index = bank->FindAnim(name);
    if (index == -1)
        return NULL;
    return bank->anims[index];
}

// FUNC_AT(0x00013cf0)
int FnDefaultAnimBank::GetAnimIndex(const char *name) {
    return bank->FindAnim(name);
}

// FUNC_AT(0x00013d00)
uint32_t FnDefaultAnimBank::GetUnknown08() {
    return bank->unknown08;
}

// FUNC_AT(0x00013d10)
FnDefaultAnimBank* FnDefaultAnimBank::Delete(unsigned flags) {
    vtable = reinterpret_cast<const void *>(uintptr_t(kFnDefaultAnimBankVtable));
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// ---- BankInfo and PrivateData

// <name>.dat, loaded through an EAGL loader with its <name>.rel; the loader is kept, the .rel freed.
// FUNC_AT(0x00013d30)
void BankInfo::Load(const char *bankName) {
    strcpy(name, bankName);
    char path[100];
    Crt_sprintf(path, "%s%s.%s", kAnimDirectory, name, "dat");
    data = static_cast<uint8_t *>(UFileLoader::FileLoadz(path, 0));
    uint32_t size = MEM_size(data);
    Crt_sprintf(path, "%s%s.%s", kAnimDirectory, name, "rel");
    void *relocations = UFileLoader::FileLoadz(path, kLoadFlags);
    void *memory = EaglMalloc(sizeof(DynamicLoader), "EAGL::DynamicLoader new");
    DynamicLoader *made = NULL;
    if (memory != NULL)
        made = static_cast<DynamicLoader *>(memory)->ConstructSplit(data, size, relocations, 0,
                                                                     ActManager::GetSymbolResolver());
    loader = made;
    loader->GetAddr("AnimationBank", name, reinterpret_cast<void **>(&bank));
    FnDefaultAnimBank *wrapper = static_cast<FnDefaultAnimBank *>(OperatorNew(sizeof(FnDefaultAnimBank)));
    if (wrapper != NULL)
        wrapper->Construct();
    anims = wrapper;
    AnimVCall<void>(anims, kBankInit, bank);
    animCount = AnimVCall<int>(anims, kBankGetNumAnims);
    loader->Release();
    UMemory::Free(relocations);
}

void BankInfo::Destruct() {
    if (bank == NULL)
        return;
    if (loader != NULL) {
        loader->Destruct();
        EaglFree(loader, sizeof(DynamicLoader));
    }
    if (anims != NULL)
        AnimVCall<FnDefaultAnimBank *>(anims, kBankDelete, 1u);
    UMemory::Free(data);
}

// FUNC_AT(0x00013ba0)
PrivateData* PrivateData::Construct() {
    bankCount = 0;
    animCount = 0;
    for (int i = 0; i < kMaxBanks; i++) {
        banks[i].bank = NULL;
        banks[i].anims = NULL;
        banks[i].data = NULL;
        banks[i].loader = NULL;
        banks[i].animCount = 0;
    }
    lookup = (int32_t *)UFileLoader::FileLoadz("data\\actors\\anims\\ALookup.bin", 0);
    lookupSize = MEM_size(lookup);
    return this;
}

// FUNC_AT(0x00013e90)
void PrivateData::Destruct() {
    UMemory::Free(lookup);
    for (int i = 0; i < kMaxBanks; i++)
        banks[i].Destruct();
}

// FUNC_AT(0x00013f00)
void PrivateData::LoadBank(const char *name) {
    for (int i = 0; i < kMaxBanks; i++) {
        if (strcmp(BankNames[i], name) == 0) {
            banks[i].Load(name);
            bankCount++;
            animCount += banks[i].animCount;
            return;
        }
    }
}

// FUNC_AT(0x00013c00)
void PrivateData::GetAnimation(int bank, int index, uint8_t **anim, uint8_t **second) {
    BankInfo *info = &banks[bank];
    int32_t *entry = &lookup[lookup[bank] + index * 2];
    int secondIndex = entry[0];
    int animIndex = entry[1];
    *anim = animIndex == -1 ? NULL : AnimVCall<uint8_t *>(info->anims, kBankGetAnim, animIndex);
    *second = secondIndex == -1 ? NULL : AnimVCall<uint8_t *>(info->anims, kBankGetAnim, secondIndex);
}

// ---- ActAnimationDatabase

// FUNC_AT(0x00013f90)
ActAnimationDatabase* ActAnimationDatabase::Construct() {
    void *memory = OperatorNew(sizeof(PrivateData));
    data = memory != NULL ? static_cast<PrivateData *>(memory)->Construct() : NULL;

    const char *bankList = fgWorld->attributes.LookupValidString("ACT_ANIM_BANK", NULL);
    char *text = NULL;
    if (strcmp(bankList, "") != 0) {
        char path[52];
        strcpy(path, kAnimDirectory);
        strcat(path, bankList);
        if (UFileLoader::FileExists(path))
            text = static_cast<char *>(UFileLoader::FileLoadz(path, kLoadFlags));
    }
    if (text == NULL)
        text = static_cast<char *>(UFileLoader::FileLoadz("data\\actors\\anims\\BkDflt.txt", kLoadFlags));

    // One name a line, lower case; the line ends become terminators (two to a line)
    char *end = text + MEM_size(text);
    for (char *c = text; c < end; c++) {
        if (*c == '\r')
            *c = '\0';
        else if (*c == '\n')
            *c = '\0';
        else
            *c = char(Crt_tolower(*c));
    }
    char *line = text;
    while (strncmp(line, "start", 5) != 0 && line < end)
        line += strlen(line) + 2;
    for (line += strlen(line) + 2; strncmp(line, "end", 3) != 0 && line < end; line += strlen(line) + 2)
        data->LoadBank(line);
    UMemory::Free(text);
    return this;
}

// FUNC_AT(0x000141b0)
void ActAnimationDatabase::Destruct() {
    PrivateData *banks = data;
    if (banks != NULL) {
        banks->Destruct();
        OperatorDelete(banks);
    }
}

// FUNC_AT(0x00013c70)
void ActAnimationDatabase::GetAnimation(int bank, int index, uint8_t **anim, uint8_t **second) {
    data->GetAnimation(bank, index, anim, second);
}

// ---- ActAnimGroup

// Each channel is made again from its data; a missing one is answered with a length of 0 and keeps its old
// channel.
// FUNC_AT(0x00014240)
void ActAnimGroup::ChangeAnimation(int animBank, int animIndex) {
    bank = animBank;
    index = animIndex;
    TheActManager->animations->GetAnimation(animBank, animIndex, &animData, &secondData);
    if (animData != NULL) {
        if (anim != NULL)
            AnimPool_DeleteFnAnim(anim);
        anim = AnimPool_NewFnAnim(animData);
        AnimVCall<bool>(anim, kSlotGetLength, &length);
    } else {
        length = 0.0f;
    }
    if (secondData != NULL) {
        if (second != NULL)
            AnimPool_DeleteFnAnim(second);
        second = AnimPool_NewFnAnim(secondData);
        AnimVCall<bool>(second, kSlotGetLength, &secondLength);
    } else {
        secondLength = 0.0f;
    }
}

// FUNC_AT(0x00014310)
void ActAnimGroup::Destruct() {
    if (anim != NULL)
        AnimPool_DeleteFnAnim(anim);
    if (second != NULL)
        AnimPool_DeleteFnAnim(second);
}

// FUNC_AT(0x00014360)
ActAnimGroup* ActAnimGroup::Construct(int animBank, int animIndex) {
    anim = NULL;
    animData = NULL;
    second = NULL;
    secondData = NULL;
    index = animIndex;
    bank = animBank;
    if (animIndex >= 0)
        ChangeAnimation(animBank, animIndex);
    return this;
}

// FUNC_AT(0x000143a0)
ActAnimGroup* ActAnimGroup::Construct(const ActAnimGroup *other) {
    anim = NULL;
    animData = NULL;
    second = NULL;
    secondData = NULL;
    index = other->index;
    bank = other->bank;
    if (other->index >= 0)
        ChangeAnimation(other->bank, other->index);
    return this;
}
