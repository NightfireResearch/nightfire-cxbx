#ifndef DRIVING_DATA_UDATA_H_
#define DRIVING_DATA_UDATA_H_

// The data-group format CARP files are made of (EA's UGroup/UData), as far as the data layer reads it.
//
// A deserialised file is a tree of 16-byte records. Each record has a four-character tag, flags, a count and
// a data word. A group's data word leads to its array of records, its child groups first and its data records
// after them (UGroup::GetArray, ported with the rest of UGroup in another package). A data record's data word is
// its payload: an offset from the record itself while kDataRelative is set, a plain pointer once something has
// adopted new data for it (UData::AdoptData). Tags compare as big-endian character codes ('Base' is 0x42617365);
// an indexed tag (kDataIndexedTag) keeps a 16-bit number in its low half and compares with that half read as
// two spaces - "sn" plus a texture-file number, "as" plus an index.
//
// UGroup (engine/UGroup.h) derives from UData.

#include <stdint.h>

enum UDataFlag : uint32_t {
    kDataIndexedTag = 0x01,   // the low half of the tag is a number; compare it as "  "
    kDataRelative = 0x02,     // the data word is an offset from the record
    kDataOwned = 0x04,        // the data word points at a block AdoptData frees when it adopts another
};

// A data record (a UGroup's data entry). flags >> 8 is the payload's size in bytes, count its element count.
class UData {
public:
    uint32_t tag;
    uint32_t flags;
    uint32_t count;
    uint32_t data;

    // The tag as lookups compare it: an indexed tag's number replaced by two spaces.
    uint32_t MatchTag() const {
        return (flags & kDataIndexedTag) ? (tag & 0xffff2020) | 0x2020 : tag;
    }
    // An indexed tag's number, -1 for a plain tag.
    int TagIndex() const { return (flags & kDataIndexedTag) ? int(tag & 0xffff) : -1; }
    uint8_t *Data() {
        return (flags & kDataRelative) ? reinterpret_cast<uint8_t *>(this) + data
                                       : reinterpret_cast<uint8_t *>(uintptr_t(data));
    }
    uint32_t Size() const { return flags >> 8; }

    // Points the record at new data (0x001178f0): frees the block it owned first, clears kDataRelative, and
    // sets kDataOwned from `own` and the size from `size`. CARP::SymbolicResolver uses it to swap a reference's
    // name for what the symbol table found.
    void AdoptData(void *newData, uint32_t size, uint32_t newCount, bool own);
};

// A group (engine/UGroup.h, UGroup's own port): the same header, its child groups' count in flags >> 5.
class UGroup;

static_assert(sizeof(UData) == 16, "UData is a 16-byte record");

// A tag written as text in a symbol name (0x001177b0): "{abcd}" packs the four characters, "{abcd0012}" puts
// the hexadecimal number after them in the low half. Anything else is 0.
uint32_t UDataGroupDecodeTag(const char *text);

// UGroup's binary search over a sorted run of records (0x00117840): EBX = count, (records **, tag). Moves
// *records to the first record with the tag and answers the tag, or to where it would go and answers 0. The
// adaptor is under Ghidra's name; UDataFindTag is the C++ under it.
void FUN_00117840();
uint32_t UDataFindTag(uint32_t count, UData **records, uint32_t tag);

#endif // DRIVING_DATA_UDATA_H_
