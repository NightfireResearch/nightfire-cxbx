#ifndef HASHTABLE_H_
#define HASHTABLE_H_

#include "../actionhelpers.h"

celglist_tag * hashtable_hashcode_to_celglist(HASHCODE hashcode);
void hashtable_set_object_to_entity_gfx(obj_tag *obj, HASHCODE hashcode);
void hashtable_additem(HASHCODE hashcode, void* data);

#endif // HASHTABLE_H