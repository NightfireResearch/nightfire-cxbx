#ifndef DRIVING_ENGINE_UGROUP_H_
#define DRIVING_ENGINE_UGROUP_H_

#include <stddef.h>
#include <stdint.h>

#include "../data/UData.h"   // UData

// ---------------------------------------------------------------------------------------------------------------
// UGroup: a data group, the container format of the game's data files (CARP level data .crp, galleries .gal,
// locale tables .loc). A group is a 16-byte record whose items are an array of 16-byte records: first its
// subgroups (themselves groups), then its data items. A data item has the same shape: a tag, flags, a word, and
// the offset of its data. Offsets are relative in the file (a group's items in 16-byte units from the group, a data
// item's data in bytes from the item) until ResolveOffsets makes them pointers. See UGroup.cpp.
//
// Tags are four characters. An indexed data item's tag keeps its type in the high half and an index in the low
// half; its type is then the high half over two spaces (0x2020, UData::MatchTag).
// ---------------------------------------------------------------------------------------------------------------

// The records are UData (data/UData.h: tag, flags, count, data); a group is a record whose data word leads to its
// items and whose count is its number of data items. This is the one definition of UGroup: data/UData.h is to
// declare it only.
class UGroup : public UData {
public:
    enum GroupFlag : uint32_t {
        kDataSorted = 0x8,      // its data items are sorted by tag
        kGroupsSorted = 0x10,   // its subgroups are sorted by tag
    };
    static const int kGroupCountShift = 5;   // flags above bit 5: the number of subgroups

    // The callbacks of ProcessBreadthFirst: the game's object, its vtable's first three slots.
    struct Processor;
    struct ProcessorVtable {
        bool (__fastcall *startGroup)(Processor *processor, int, UGroup *group);   // false: skip the group
        bool (__fastcall *processData)(Processor *processor, int, UGroup *group, UData *data);   // false: stop
        void (__fastcall *endGroup)(Processor *processor, int, UGroup *group);
    };
    struct Processor {
        const ProcessorVtable *vtable;
        // The base class's StartGroup (0x00117e40): every group.
        bool StartGroup(UGroup *group);
    };

    uint32_t GroupCount() const { return flags >> kGroupCountShift; }

    UGroup* GetArray();                                         // 0x00117940, the items: subgroups, then data
    UData* DataEnd();                                           // 0x0003db60, past the last data item
    UGroup* GroupLocateTag(uint32_t tag);                       // 0x00117950
    int DataCountType(uint32_t type);                           // 0x001179f0
    UData* DataLocateFirst(uint32_t type, int first, uint32_t last);   // 0x00117a60
    UData* DataLocateTag(uint32_t tag);                         // 0x00117b50
    void ProcessBreadthFirst(Processor *processor);             // 0x00117c10
    void ResolveOffsets();                                      // 0x00117cb0
    static UGroup* Deserialize(void *file, bool resolve);       // 0x00117d60

    // A data item by type and index, falling back to index 0 (0x0008d6e0: EAX the tag, ESI the
    // group, the index on the stack). `something` is the register adaptor under Ghidra's name.
    UData* DataLocateIndexed(uint32_t tag, int index);
    static void something();
};
static_assert(sizeof(UGroup) == 0x10, "a data group record is 16 bytes");

#endif // DRIVING_ENGINE_UGROUP_H_
