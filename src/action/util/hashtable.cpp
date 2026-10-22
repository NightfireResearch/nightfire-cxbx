#include "hashtable.h"

#include <string.h> // memmove

#include "../game/obj/Effect.h"

#pragma pack(push, 1)
typedef struct{
    ushort lowerLimit;
    ushort upperLimit;
} HashTypeRange;
#pragma pack(pop)

// One search window per hash type (the top byte of a hashcode), set by hashtable_create_sections.
// hashtable_initialise, hashtable_additem and hashtable_deleteitem reset the windows of types 0-6 only, to the
// whole table: type 7's stays as hashtable_create_sections last left it.
#define NUM_HASH_TYPES 8
#define NUM_RESET_TYPES 7

// XBE_GLOBAL(0x001fe688, 0x20)
HashTypeRange HashTypeRanges[NUM_HASH_TYPES];
// The table, sorted by key; nothing checks its count against its 4096 entries
// XBE_GLOBAL(0x001f6680, 0x8000)
hashtable_entry t_hashtable[4096];
// Where the last failed lookup would have put the key. Only hashtable_getentry writes it and only
// hashtable_additem reads it, both ours now.
// XBE_GLOBAL(0x001fe680, 0x4)
static uint32_t ht_insert_ind;
// Number of entries in use, the two end keys (0 and 0xffffffff) included
// XBE_GLOBAL(0x001fe684, 0x4)
uint32_t m_nhti;

// The binary search. On a miss it leaves the insertion point in ht_insert_ind; on a hit it leaves ht_insert_ind
// untouched, which hashtable_additem relies on (see there).
//
// Keys are compared as unsigned (the original's JNC/JBE): HASHCODE is an int-sized enum, so without the casts a
// hashcode with the top bit set (0xFFFFFFFF, the "none" value callers pass) would sort below every key and leave a
// different ht_insert_ind than the original's.
hashtable_entry* _hashtable_getentry(HASHCODE hashcode) {

    uint32_t key = (uint32_t)hashcode;
    uint32_t type = key >> 24;
    if(type > (NUM_HASH_TYPES - 1))
        type = 0;

    uint lowerLimit = HashTypeRanges[type].lowerLimit;
    uint upperLimit = HashTypeRanges[type].upperLimit;

    // A do-while as in the original: the first probe happens even if the window is empty
    do {
        uint pivot = (lowerLimit + upperLimit) / 2;
        uint32_t pivotKey = (uint32_t)t_hashtable[pivot].key;
        if(key < pivotKey) {
            upperLimit = pivot - 1;
        } else {
            if(key == pivotKey)
                return t_hashtable + pivot;
            lowerLimit = pivot + 1;
        }
    } while(lowerLimit <= upperLimit);

    ht_insert_ind = lowerLimit;
    return NULL;
}

// The original (0x0006b370) takes its hashcode in EDI; no original calls it now. It clobbers only EAX, ECX and EDX (it saves ESI
// itself), and neither caller keeps anything in ECX or EDX across the call, so a plain cdecl call is enough: our
// compiled body preserves EBX, ESI, EDI and EBP.
// AUTOLTCG
__declspec(naked) hashtable_entry* hashtable_getentry(HASHCODE hashcode) {
    _asm {
        push edi
        call _hashtable_getentry
        add esp, 4
        ret
    }
}

static void ResetTypeRanges(void) {
    for (int i = 0; i < NUM_RESET_TYPES; i++) {
        HashTypeRanges[i].lowerLimit = 0;
        HashTypeRanges[i].upperLimit = (ushort)(m_nhti - 1);
    }
}

// Inserts a key keeping the table sorted. It searches first only for the side effect: the insertion point the
// miss leaves in ht_insert_ind. Quirks kept from the original:
// - there is no duplicate check and no capacity check. If the key is already present the search leaves
//   ht_insert_ind as it was from the previous miss, and the entry goes in there - a second copy of the key, at a
//   stale position (which may break the sort order).
// - afterwards the search windows of types 0-6 become the whole table, [0, m_nhti - 1], rather than being worked
//   out per type.
// AUTOINJECT
void hashtable_additem(HASHCODE hashcode, void* data) {

    _hashtable_getentry(hashcode);

    uint32_t ind = ht_insert_ind;
    memmove(&t_hashtable[ind + 1], &t_hashtable[ind], (m_nhti - ind) * sizeof(hashtable_entry));

    t_hashtable[ht_insert_ind].key = hashcode;
    t_hashtable[ht_insert_ind].data = data;

    m_nhti++;

    ResetTypeRanges();
}

// A key not in the table is not checked for: the original then copies from address 8
// AUTOINJECT
void hashtable_deleteitem(HASHCODE hashcode) {

    hashtable_entry *entry = _hashtable_getentry(hashcode);

    m_nhti--;
    uint index = entry - t_hashtable;
    memmove(entry, entry + 1, (m_nhti - index) * sizeof(hashtable_entry));

    ResetTypeRanges();
}

// AUTOINJECT
void hashtable_initialise(void) {

    t_hashtable[0].key = HASHCODE_NONE;
    t_hashtable[1].key = (HASHCODE)0xFFFFFFFF;
    m_nhti = 2;

    for (int i = 0; i < NUM_RESET_TYPES; i++) {
        HashTypeRanges[i].lowerLimit = 0;
        HashTypeRanges[i].upperLimit = 1;
    }
}

// Each type's search window: from the entry before its first key to the entry after its last (the last entry
// itself at the end of the table). A type with no keys searches the whole table.
// AUTOINJECT
void hashtable_create_sections(void) {

    for (uint type = 0; type < NUM_HASH_TYPES; type++) {
        uint32_t typeBits = type << 24;
        HashTypeRanges[type].upperLimit = (ushort)(m_nhti - 1);
        HashTypeRanges[type].lowerLimit = 0;

        for (uint i = 1; i < m_nhti; i++) {
            if ((t_hashtable[i].key & 0xFF000000) != typeBits)
                continue;

            HashTypeRanges[type].lowerLimit = (ushort)(i > 0 ? i - 1 : i);
            uint upper;
            do {
                upper = i < m_nhti - 1 ? i + 1 : i;
                i++;
            } while ((t_hashtable[i].key & 0xFF000000) == typeBits);
            HashTypeRanges[type].upperLimit = (ushort)upper;
            break;
        }
    }
}

// Every key of the type (the top byte of hashtype), from the end of the table down
// AUTOINJECT
void hashtable_delete_hashtype(uint hashtype) {

    for (uint i = m_nhti - 1; i > 0; i--) {
        if ((t_hashtable[i].key & 0xFF000000) == (hashtype & 0xFF000000))
            hashtable_deleteitem(t_hashtable[i].key);
    }

    hashtable_create_sections();
}

// The keys of the type, in order, into out (hashtable_get_hashtype_count of them)
// AUTOINJECT
void hashtable_get_hashtype_data(uint hashtype, HASHCODE *out) {

    for (uint i = 1; i < m_nhti - 1; i++) {
        if ((t_hashtable[i].key & 0xFF000000) == (hashtype & 0xFF000000))
            *out++ = t_hashtable[i].key;
    }
}

// AUTOINJECT
void hashtable_modify(HASHCODE hashcode, void* newData) {
    hashtable_entry *entry = _hashtable_getentry(hashcode);
    if(entry != NULL) {
        entry->data = newData;
    }
}

// AUTOINJECT
void* hashtable_getitem(HASHCODE hashcode) {
    hashtable_entry *entry = _hashtable_getentry(hashcode);
    return entry ? entry->data : NULL;
}

// AUTOINJECT
celglist_tag * hashtable_hashcode_to_celglist(HASHCODE hashcode) {

    if(hashcode == 0xFFFFFFFF)
        return NULL;

    return (celglist_tag*)(hashtable_getitem(hashcode));
}

// AUTOINJECT
int hashtable_get_hashtype_count(uint hashtype) {

    int count = 0;
    for(uint i = 1; i < (m_nhti-1); i++) {
        if((t_hashtable[i].key & 0xFF000000) == (hashtype & 0xFF000000))
            count++;
    }
    return count;
}

// AUTOINJECT
HASHCODE hashtable_celglist_to_hashcode(celglist_tag *celgl) {

    if(celgl == NULL)
        return HASHCODE_NONE;

    // Unclear why, but the original code starts at 1 not 0
    for(uint i = 1; i < (m_nhti - 1); i++) {
        if (t_hashtable[i].data == (void*)celgl) {
            return t_hashtable[i].key;
        }
    }

    return HASHCODE_NONE;
}

typedef struct {
    uint unknownDataMaybeTexPtr;
    short defaultWidth;
    short defaultHeight;
} SpriteInfoFromHashmap;

// AUTOINJECT
bool hashtable_set_sprite(sprite *sprOut, HASHCODE hc) {

    hashtable_entry* entry = _hashtable_getentry(hc);

    if(entry == NULL) {
        NF_WARN("FYI : SPRITE HASHCODE %08x NOT FOUND\n", hc); // GC check (0x80051da4)
        return false;
    }

    SpriteInfoFromHashmap sprInfo = *(SpriteInfoFromHashmap*)(entry->data);

    if(&sprInfo == NULL)
        return false;

    sprOut->unknownDataMaybeTexPtr = sprInfo.unknownDataMaybeTexPtr;
    sprOut->backupOnscreenWidth = sprInfo.defaultWidth;
    sprOut->backupOnscreenHeight = sprInfo.defaultHeight;
    sprOut->onscreenWidth = sprInfo.defaultWidth;
    sprOut->onscreenHeight = sprInfo.defaultHeight;
    sprOut->spritesheetWidth = sprInfo.defaultWidth;
    sprOut->spritesheetHeight = sprInfo.defaultHeight;

    return true;

}

// AUTOINJECT
void hashtable_set_sub_object_to_entity_gfx(ATTACH_tag *mark, HASHCODE hc) {

    if(hc == 0xFFFFFFFF)
        return;

    hashtable_entry *entry = _hashtable_getentry(hc);

    if((entry == NULL) || (entry->data == NULL))
        return;

    mark->glist = (celglist_tag*) entry->data;
}

// AUTOINJECT
void hashtable_set_object_to_entity_gfx(obj_tag *obj, HASHCODE hc) {

    if(hc == 0xFFFFFFFF)
        return;

    hashtable_entry *entry = _hashtable_getentry(hc);

    if((entry == NULL) || (entry->data == NULL))
        return;

    Control_SetGList(obj, (celglist_tag*) entry->data);

}
