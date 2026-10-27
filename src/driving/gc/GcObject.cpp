#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// A GameCube EAGL object made the Xbox's before its constructors run (docs/gc-driving.md, "EAGL objects"). The
// GameCube's objects are the Xbox's container (little-endian MIPS ELF, the same sections, symbols and relocations);
// their data is big-endian, but for the relocation sites, which the tools left little-endian and the loader has just
// filled with addresses. So each symbol's data is turned round by its class, word by word or halfword by halfword,
// passing over the relocation sites, and so is what its pointers lead to: a model's draw list, TAR list, enable table
// and patch list. The GeoPrims stay as they are: GcModel.cpp reads them once the constructors have run.

#include "GcData.h"
#include "GcBytes.h"
#include "GcRenderMethods.h"

#include "../eagl/Loader.h"
#include "../eagl/Model.h"
#include "../platform/Ptr32.h"

#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <set>
#include <vector>

using EAGL::Model;

namespace {

struct ElfHeader {          // Elf32_Ehdr, the fields read here
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;         // +0x20
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;         // +0x30
    uint16_t shstrndx;
};
static_assert(sizeof(ElfHeader) == 0x34, "an ELF header is 0x34 bytes");

struct ElfSection {         // Elf32_Shdr, as DynamicLoader::Initialize leaves it: offset an address; a relocation
    uint32_t name, type, flags, addr;   //  section's info the address of the section it relocates
    uint32_t offset;
    uint32_t size;
    uint32_t link, info, align, entsize;
};

struct ElfRel {             // Elf32_Rel
    uint32_t offset;
    uint32_t info;
};

constexpr uint32_t kShtRel = 9;

// The model fields the GameCube keeps halfwords of its own in, where the Xbox's twins have zeros
constexpr uint32_t kModelGcOnlyStart = 0x6c;
constexpr uint32_t kModelGcOnlyEnd = 0x9c;

constexpr uint32_t kDrawListGeoPrim = 0xffff;   // a draw list entry's low half: a GeoPrim follows

class ObjectSwapper {
public:
    explicit ObjectSwapper(DynamicLoader *loader);
    void Run();

private:
    bool Relocated(const void *p) const {
        return std::binary_search(sites_.begin(), sites_.end(), reinterpret_cast<uintptr_t>(p));
    }
    // A word turned round once, unless the loader put an address there
    void Word(void *p) {
        if (p != NULL && !Relocated(p) && done_.insert(reinterpret_cast<uintptr_t>(p)).second)
            GcSwap32(static_cast<uint8_t *>(p));
    }
    void Half(void *p) {
        if (p != NULL && done_.insert(reinterpret_cast<uintptr_t>(p)).second)
            GcSwap16(static_cast<uint8_t *>(p));
    }
    void Words(void *p, uint32_t bytes) {
        for (uint32_t at = 0; at + 4 <= bytes; at += 4)
            Word(static_cast<uint8_t *>(p) + at);
    }
    uint8_t *SectionAddress(uint32_t offset) const;
    void SwapModel(Model *model);
    void SwapDrawList(uint32_t *list, Model *model);
    void SwapTarList(uint32_t *list);
    void SwapTar(uint8_t *tar);

    DynamicLoader *loader_;
    std::vector<uintptr_t> sites_;
    std::set<uintptr_t> done_;
    std::set<Model *> models_;
    std::set<uint32_t *> lists_;
    int untested_;
};

ObjectSwapper::ObjectSwapper(DynamicLoader *loader) : loader_(loader), untested_(0) {}

// A file offset to memory, as the loader's FileAddress: in the first part, else the second
uint8_t *ObjectSwapper::SectionAddress(uint32_t offset) const {
    if (offset < loader_->elfSize || loader_->second == NULL)
        return loader_->elf + offset;
    return loader_->second + (offset - loader_->elfSize);
}

void ObjectSwapper::SwapTar(uint8_t *tar) {
    // TARSymbolData (eagl/Tar.cpp): words, but the shape's and the clut's four-character names
    for (uint32_t at = 0; at < 0x4c; at += 4)
        if (at != 0x04 && at != 0x2c)
            Word(tar + at);
}

// {name, count, TAR * x count} ... then a null name
void ObjectSwapper::SwapTarList(uint32_t *list) {
    for (int guard = 0; list != NULL && *list != 0 && guard < 4096; guard++) {
        Word(list + 1);
        list += 2 + list[1];
    }
}

// The entries as Model::Optimize and WalkDrawList read them: a block's two words after its entry (its enable table
// entry's halfwords too), or a GeoPrim's pointer after an entry whose low half is 0xffff
void ObjectSwapper::SwapDrawList(uint32_t *list, Model *model) {
    if (list == NULL || !lists_.insert(list).second)
        return;
    Word(list);
    uint32_t limit = *list & 0xffff;
    uint32_t entry = limit;
    uint32_t *p = list + 1;
    uint32_t block = 0;
    for (int guard = 0; guard < 0x100000; guard++) {
        if (entry == kDrawListGeoPrim) {
            p++;
        } else {
            Word(p);
            Word(p + 1);
            p += 2;
            if (model->enableTable != NULL) {
                EAGL::GeometryEnable *enable = &model->enableTable[block];
                Half(&enable->unknown00);
                Half(&enable->enable);
            }
            block++;
        }
        Word(p);
        entry = *p++ & 0xffff;
        if (!(entry > limit))
            break;
    }
}

void ObjectSwapper::SwapModel(Model *model) {
    if (!models_.insert(model).second)
        return;
    uint8_t *bytes = reinterpret_cast<uint8_t *>(model);
    Words(bytes, kModelGcOnlyStart);
    memset(bytes + kModelGcOnlyStart, 0, kModelGcOnlyEnd - kModelGcOnlyStart);
    Words(bytes + kModelGcOnlyEnd, sizeof(Model) - kModelGcOnlyEnd);
    SwapTarList(model->tarList);
    SwapDrawList(model->drawList, model);
    if (model->patch != NULL) {
        uint32_t *patch = model->patch;
        Word(patch);
        for (uint32_t i = 0; i < patch[0] && i < 0x10000; i++)
            Words(patch + 1 + i * 2, 8);
    }
    if ((model->morphData != NULL || model->morphWeights != NULL) && untested_++ == 0)
        printf("[gc] a model with morph data: left as the GameCube has it (untested)\n");
}

void ObjectSwapper::Run() {
    const ElfHeader *header = reinterpret_cast<const ElfHeader *>(loader_->elf);
    const ElfSection *sections = reinterpret_cast<const ElfSection *>(SectionAddress(header->shoff));
    for (int i = 0; i < header->shnum; i++) {
        const ElfSection &s = sections[i];
        if (s.type != kShtRel)
            continue;
        const ElfRel *rel = PtrFrom32<const ElfRel>(s.offset);
        uint8_t *target = PtrFrom32<uint8_t>(s.info);
        for (uint32_t r = 0; r < s.size / sizeof(ElfRel); r++)
            sites_.push_back(reinterpret_cast<uintptr_t>(target + rel[r].offset));
    }
    std::sort(sites_.begin(), sites_.end());

    LoaderSymbol symbol;
    for (int i = 0; loader_->GetSymbol(&symbol, i)->name != NULL; i++) {
        if ((uint8_t)symbol.defined == 0 || symbol.address == NULL)
            continue;
        if (strcmp(symbol.className, "Model") == 0)
            SwapModel(static_cast<Model *>(symbol.address));
        else if (strcmp(symbol.className, "EAGL::TAR") == 0)
            SwapTar(static_cast<uint8_t *>(symbol.address));
    }
}

}  // namespace

void Gc_ConvertObject(DynamicLoader *loader) {
    if (loader == NULL || Gc_IsXboxObject(loader->elf))
        return;
    ObjectSwapper(loader).Run();
}
