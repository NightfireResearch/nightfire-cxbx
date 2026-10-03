#include "UGroup.h"

// ---------------------------------------------------------------------------------------------------------------
// UGroup, the data group (0x00117940-0x00117e50, with 0x0003db60 and 0x0008d6e0), ported from the listing. See
// UGroup.h for the format.
//
// The sorted lookups go through the game's binary search over 16-byte records (0x00117840, not ours yet): it takes
// the number of records in EBX, a pointer to the first record (moved to the record found, or to where the tag would
// be) and the tag, and answers the tag of the record it stopped at (0 if none). LocateSorted calls it.
// ---------------------------------------------------------------------------------------------------------------

#define UGroup_SearchSorted ((uint32_t (*)(UData **cursor, uint32_t tag))0x00117840)   // + the count in EBX: called by LocateSorted

static_assert(sizeof(void *) == sizeof(uint32_t), "a group's offsets become pointers in place");

// The binary search at 0x00117840, with its count in EBX (which it keeps)
__declspec(naked) static uint32_t LocateSorted(uint32_t count, UData **cursor, uint32_t tag) {
    __asm {
        push ebx
        mov ebx, dword ptr [esp + 8]
        push dword ptr [esp + 16]    // tag
        push dword ptr [esp + 16]    // cursor
        mov eax, 0x00117840
        call eax
        add esp, 8
        pop ebx
        ret
    }
}

// The items as an address: the code tests the address of the first data item for zero (a group with no items),
// arithmetic a pointer would not be allowed to do.
static uintptr_t ItemsAddress(UGroup *group) {
    return (uintptr_t)group->GetArray();
}

// FUNC_AT(0x00117940)
UGroup* UGroup::GetArray() {
    if ((flags & kDataRelative) != 0)
        return this + data;
    return (UGroup *)(uintptr_t)data;
}

// FUNC_AT(0x0003db60)
UData* UGroup::DataEnd() {
    return GetArray() + (GroupCount() + count);
}

// FUNC_AT(0x00117950)
UGroup* UGroup::GroupLocateTag(uint32_t tag) {
    UGroup *groups = GetArray();
    if (groups == NULL)
        return NULL;
    if ((flags & kGroupsSorted) != 0) {
        UData *found = groups;
        if (LocateSorted(GroupCount(), &found, tag) == tag)
            return static_cast<UGroup *>(found);
        return GetArray() + GroupCount();
    }
    UGroup *end = GetArray() + GroupCount();
    UGroup *group = groups;
    while (group < end && group->tag != tag)
        group++;
    return group;
}

// FUNC_AT(0x001179f0)
int UGroup::DataCountType(uint32_t type) {
    int found = 0;
    for (UData *item = GetArray() + GroupCount(); item != DataEnd(); item++) {
        if (item->MatchTag() == type)
            found++;
    }
    return found;
}

// The first data item of `type` (an indexed type: its index from `first` to `last`; `first` -1: the unindexed tag
// `type` itself), by the binary search whether or not the items are sorted; DataEnd() if there is none.
// FUNC_AT(0x00117a60)
UData* UGroup::DataLocateFirst(uint32_t type, int first, uint32_t last) {
    uintptr_t items = ItemsAddress(this) + GroupCount() * sizeof(UGroup);
    if (items == 0)
        return NULL;
    UData *found = (UData *)items;
    if (first == -1) {
        if (LocateSorted(count, &found, type) == type)
            return found;
        return DataEnd();
    }
    uint32_t foundTag = LocateSorted(count, &found, (type & 0xffff0000) | first);
    if (((foundTag & 0xffff2020) | 0x2020) == type && last >= (foundTag & 0xffff))   // the found tag's type
        return found;
    return DataEnd();
}

// FUNC_AT(0x00117b50)
UData* UGroup::DataLocateTag(uint32_t tag) {
    uintptr_t items = ItemsAddress(this) + GroupCount() * sizeof(UGroup);
    if (items == 0)
        return NULL;
    UData *item = (UData *)items;
    if ((flags & kDataSorted) != 0) {
        if (LocateSorted(count, &item, tag) == tag)
            return item;
        return DataEnd();
    }
    UData *end = DataEnd();
    while (item < end && item->tag != tag)
        item++;
    return item;
}

// The processor's virtual methods, through its vtable
static bool StartGroup(UGroup::Processor *processor, UGroup *group) {
    return processor->vtable->startGroup(processor, 0, group);
}

static bool ProcessData(UGroup::Processor *processor, UGroup *group, UData *data) {
    return processor->vtable->processData(processor, 0, group, data);
}

static void EndGroup(UGroup::Processor *processor, UGroup *group) {
    processor->vtable->endGroup(processor, 0, group);
}

// The group's data items, then each subgroup the same way; EndGroup for every group StartGroup was asked about.
// A data item the processor refuses ends the group there, subgroups unvisited.
// FUNC_AT(0x00117c10)
void UGroup::ProcessBreadthFirst(Processor *processor) {
    if (StartGroup(processor, this)) {
        for (uint32_t i = 0; i < count; i++) {
            if (!ProcessData(processor, this, GetArray() + (GroupCount() + i))) {
                EndGroup(processor, this);
                return;
            }
        }
        for (uint32_t i = 0; i < GroupCount(); i++)
            (GetArray() + i)->ProcessBreadthFirst(processor);
    }
    EndGroup(processor, this);
}

// Every relative offset made a pointer: the group's items, its subgroups' (recursively), its data items' data.
// FUNC_AT(0x00117cb0)
void UGroup::ResolveOffsets() {
    data = (uint32_t)(uintptr_t)GetArray();
    flags &= ~kDataRelative;
    for (UGroup *group = GetArray(); group != GetArray() + GroupCount(); group++)
        group->ResolveOffsets();
    for (UData *item = GetArray() + GroupCount(); item != DataEnd(); item++) {
        if ((item->flags & kDataRelative) != 0)
            item->data += (uint32_t)(uintptr_t)item;
        item->flags &= ~kDataRelative;
    }
}

// FUNC_AT(0x00117d60)
UGroup* UGroup::Deserialize(void *file, bool resolve) {
    UGroup *group = (UGroup *)file;
    if (resolve)
        group->ResolveOffsets();
    return group;
}

// FUNC_AT(0x00117e40)
bool UGroup::Processor::StartGroup(UGroup *group) {
    (void)group;
    return true;
}

UData* UGroup::DataLocateIndexed(uint32_t tag, int index) {
    uint32_t wanted = tag;
    if (index != -1)
        wanted = (tag & 0xffff0000) | index;
    UData *found = DataLocateTag(wanted);
    if (found == DataEnd() && index != 0)
        found = DataLocateTag(tag & 0xffff0000);
    return found == DataEnd() ? NULL : found;
}

static UData *DataLocateIndexedCall(UGroup *group, uint32_t tag, int index) {
    return group->DataLocateIndexed(tag, index);
}

// 0x0008d6e0: EAX the tag, ESI the group, the index on the stack (the caller pops it); EAX the answer. ECX and EDX
// are not kept, as the original does not keep them.
// AUTOLTCG
__declspec(naked) void UGroup::something() {
    __asm {
        push dword ptr [esp + 4]
        push eax
        push esi
        call DataLocateIndexedCall
        add esp, 12
        ret
    }
}
