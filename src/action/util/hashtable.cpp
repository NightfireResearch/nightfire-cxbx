#include "hashtable.h"

// AUTOGEN
void hashtable_set_object_to_entity_gfx(obj_tag *obj, HASHCODE hashcode);
// AUTOGEN
void hashtable_additem(HASHCODE hashcode, void* data);

#pragma pack(push, 1)
typedef struct{
    ushort lowerLimit;
    ushort upperLimit;
} HashTypeRange;
#pragma pack(pop)

#define HashTypeRanges (*(HashTypeRange(*)[8])0x001fe688)
#define t_hashtable (*(hashtable_entry(*)[1000])0x001f6680)
#define ht_insert_ind U32_AT(0x001fe680)
#define m_nhti U32_AT(0x001fe684)

// Cannot auto-generate because it uses custom calling convention
hashtable_entry* hashtable_getentry(HASHCODE hashcode) {

    uint32_t type = (hashcode >> 18);
    if(type > 7)
        type = 0;
    
    uint lowerLimit = HashTypeRanges[type].lowerLimit;
    uint upperLimit = HashTypeRanges[type].upperLimit;

    do {
        uint pivot = (lowerLimit + upperLimit) / 2;
        if(hashcode < t_hashtable[pivot].key) {
            upperLimit = pivot - 1;
        } else {
            if(hashcode <= t_hashtable[pivot].key)
                return t_hashtable + pivot;
            lowerLimit = pivot + 1;
        }
    } while(lowerLimit <= upperLimit);

    ht_insert_ind = lowerLimit;
    return NULL;
}

// AUTOINJECT
void hashtable_modify(HASHCODE hashcode, void* newData) {
    hashtable_entry *entry = hashtable_getentry(hashcode);
    if(entry != NULL) {
        entry->data = newData;
    }
}

// AUTOINJECT
void* hashtable_getitem(HASHCODE hashcode) {
    hashtable_entry *entry = hashtable_getentry(hashcode);
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