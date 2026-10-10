#ifndef DRIVING_DATA_ATTRIBUTESYSTEM_H_
#define DRIVING_DATA_ATTRIBUTESYSTEM_H_

// The attribute system: named, typed values the game's classes look up by name - vehicle handling (every
// PVehicle's "pvehicle" collection, read into a CarPhysics structure), breakable props ("smackable": mass, hit
// points, impact sounds), the world's per-mission settings (traffic, animation bank), sentry guns.
//
// The data is text on the disc, inside each mission's archive: data\sim\attrib\attrib.dir lists every collection
// as "class\name" lines, and each collection is data\sim\attrib\<class>/<name>.atr, an INI file (read with DAFI)
// whose sections are named after collections - "[vanquish]" in vanquish.atr - each line a "KEY[.type]=value".
// PrepareDatabase registers the listed collections at start-up; a collection's file is read the first time an
// AttributeSet uses it (LoadCollection): the section named by the current track (SetCollectionSection), then the
// collection's own section, then "[default]". A name a class does not list gets an empty collection.
//
// A key either has a built-in type (AttributeValue.h: the ".b/.i/.u/.f/.v/.m/.s/.r" suffix, a string without
// one) or is a field of the class's extension structure: RegisterExtensionType declares a structure for a class
// (PVehicle::InitializeGlobals: "CarPhysics", 0x100 bytes, for "pvehicle"; PhysicsNamespace: "PhysicsData",
// 0x2c bytes, for "smackable"), RegisterExtensionField its fields (key, parser, offset, count). Such a key is
// parsed straight into the collection's structure, which LookupStruct hands out whole. Lookups that miss a
// collection go on to its parent, the class's "default" collection - so default.atr holds a class's defaults.
//
// Every string the system keeps (names, keys, string values) is stored once, in 4 KB blocks (MakeString), and
// compared by content. The system is a singleton made by Init (Bond_StartUpSystem) at 0x001e464c.

#include "AttributeContainers.h"

#include <stdint.h>

class USymbolTable;
struct DAFI;

class AttributeSystem {
public:
    void **vtable;                         // +0x00 kAttributeSystemVtable
    ExtensionTypeMap *extensionTypes;      // +0x04 "AttributeExtensionTypeMap"
    uint32_t nextExtensionType;            // +0x08 the id the next RegisterExtensionType gives (from 1)
    ExtensionClassMap *extensionFields;    // +0x0c "AttributeExtensionClassMap": class -> its fields
    EditConfigMap *editConfig;             // +0x10 "AttributeEditConfigMap" (never filled)
    CollectionMap *collections;            // +0x14 "AttributeCollectionMap"
    StringSet *strings;                    // +0x18 "AttributeStringSet"
    StoreBlockList *storeBlocks;           // +0x1c "AttributeStoreBlockList"
    char collectionSection[64];            // +0x20 the section read before a collection's own (the track's)
    uint8_t loadingEnabled;                // +0x60 collections' files may be read (PrepareDatabase has run)
    uint8_t loadingDatabase;               // +0x61 set until PrepareDatabase has run
    uint8_t unknown62[2];
    USymbolTable *symbolTable;             // +0x64 what symbol keys (".r") are looked up in

    AttributeSystem *Construct();                                                               // 0x00059090
    void Destruct();                                                                            // 0x000592d0
    AttributeSystem *Delete(unsigned flags);   // the scalar deleting destructor, vtable slot 0  // 0x00059530
    static void Init();                                                                         // 0x00059550
    static void Kill();                        // vtable slot 2                                 // 0x000592b0

    void SetCollectionSection(const char *section);                                             // 0x000525d0
    void PrepareDatabase();                                                                     // 0x00058e30
    // The collection of `name` in `className`, made (and given the class's "default" as parent) if new.
    AttributeCollection *GetCollection(const char *className, const char *name);                // 0x00058cc0
    void LoadCollection(AttributeCollection *collection);                                       // 0x00057ee0
    // Every key of the selected section of `file` into `collection`.
    void ProcessDafiSection(DAFI **file, AttributeCollection *collection);                      // 0x00057a80
    // The stored copy of `text` (NULL for NULL).
    const char *MakeString(const char *text);                                                   // 0x00055da0
    // How many collections `className` has, and the one after `after` (the first for NULL; NULL past the last).
    int CountClassNames(const char *className);                                                 // 0x00053990
    const char *GetClassNextName(const char *className, const char *after);                     // 0x00053a40

    // A structure type for `className`'s collections, kept under `attributeName`; answers its id.
    uint32_t RegisterExtensionType(const char *className, const char *attributeName, uint32_t size,
                                   AttributeExtensionInit init);                                // 0x00055a90
    // A key of the type's class that parses `count` elements into the structure at `offset`.
    void RegisterExtensionField(uint32_t type, const char *name, AttributeParserFunc parser, uint32_t offset,
                                uint32_t count, uint32_t unused1, uint32_t unused2);            // 0x00057240
    const char *GetExtensionTypeClass(uint32_t type);                                           // 0x00055b30
    // The type's initialiser on a new structure for the collection named `collectionName`.
    void InitializeExtensionType(int type, const char *collectionName, void *data);             // 0x00055b80
    // The collection's structure of `type`: the existing one, or a new one its initialiser fills in.
    AttributeValue *CreateExtensionAttribute(uint32_t type, AttributeCollection *collection);   // 0x000570c0
    // An editor's range for a field: empty in the retail game. Its callers pass the bounds as raw words (floats or
    // ints as the field's type), then -1 for an extension structure's field or 1 for a plain key, then 0.
    void ConfigEditParameters(const char *className, const char *name, uint32_t low, float high, float scope,
                              uint32_t unknown6);                                               // 0x000525f0

    // extensionTypes[type] (std::map::operator[], inlined wherever it is used): an unknown type gets an empty
    // entry.
    AttributeExtension &ExtensionType(uint32_t type);
};
static_assert(sizeof(AttributeSystem) == 0x68, "AttributeSystem is 0x68 bytes");

#define AttributeSystemInstance (*(AttributeSystem **)0x001e464c)   // AttributeSystem::fgThis

// What ProcessDafiSection does with a parsed key of a plain type (SetAttribute<T> in the PS2 build): the value
// goes into the map under `name` - held in the value if it is one element and `copy` is set, in a block of its
// own if `copy` is set, else pointing at the parsed data.
// 0x00057520, 0x00057650, 0x00057780, 0x000578b0
void SetBoolAttribute(AttributeMap *attributes, const char *name, bool copy, const AttributeParseResult *parsed);
void SetIntAttribute(AttributeMap *attributes, const char *name, bool copy, const AttributeParseResult *parsed);
void SetUIntAttribute(AttributeMap *attributes, const char *name, bool copy, const AttributeParseResult *parsed);
void SetFloatAttribute(AttributeMap *attributes, const char *name, bool copy, const AttributeParseResult *parsed);

#endif // DRIVING_DATA_ATTRIBUTESYSTEM_H_
