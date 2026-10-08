#ifndef DRIVING_AUDIO_INDEX_H_
#define DRIVING_AUDIO_INDEX_H_

// ---------------------------------------------------------------------------------------------------------------
// AIndex: a sound bank's names. ABank's constructor makes one from the bank's header file (the bank's name with
// ".h" for its extension, in the bank's directory): every "#define <name> <number>" line becomes a pair in two
// std::maps, number -> name and name -> number. A name with an underscore after its fourth character is stored as
// "SFX_" and what follows the first such underscore; every '.' after the fourth character ends the name. The
// names live in one block the index owns. See Index.cpp.
//
// The maps are MSVC 7's _Tree (engine/RbTree.h, 0x18-byte nodes): their erase, _Insert and erase(first, last)
// are the data layer's maps' instruction for instruction (data/Tree.h), and the number map's find is the one
// every map keyed by int shares (WSoundMap::Find).
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../data/Tree.h"
#include "../engine/RbTree.h"

// ---- std::map<int, const char *>

struct AIndexIdPair {
    int32_t id;                 // the key (compared signed)
    const char *name;
};

struct AIndexIdNode : RbTreeNode<AIndexIdNode, AIndexIdPair> {};
static_assert(sizeof(AIndexIdNode) == sizeof(TreeNode), "an index node is the data layer's map node");

struct AIndexIdInsert {
    AIndexIdNode *node;
    bool inserted;
};

struct AIndexIdMap : RbTree<AIndexIdNode> {
    // _Insert and erase(iterator): one compiled copy each, which the name map calls too.
    AIndexIdNode** InsertAt(AIndexIdNode **result, bool addLeft, AIndexIdNode *where,
                            const AIndexIdPair *value);                                 // 0x00126d80
    AIndexIdNode** EraseAt(AIndexIdNode **result, AIndexIdNode *where);                 // 0x00126f60
    AIndexIdInsert* InsertUnique(AIndexIdInsert *result, const AIndexIdPair *value);    // 0x00127230
    AIndexIdNode** EraseRange(AIndexIdNode **result, AIndexIdNode *first, AIndexIdNode *last);   // 0x001273c0
    // The map's destructor; only the exception unwinds of AIndexMap's constructor call it.
    void Destruct();                                                                    // 0x00127540

    Tree *AsTree() { return reinterpret_cast<Tree *>(this); }
};
static_assert(sizeof(AIndexIdMap) == sizeof(Tree), "a map is 12 bytes");

// ---- std::map<const char *, int>, names compared with _stricmp

struct AIndexNamePair {
    const char *name;           // the key
    int32_t id;
};

struct AIndexNameNode : RbTreeNode<AIndexNameNode, AIndexNamePair> {};
static_assert(sizeof(AIndexNameNode) == sizeof(TreeNode), "an index node is the data layer's map node");

struct AIndexNameInsert {
    AIndexNameNode *node;
    bool inserted;
};

struct AIndexNameMap : RbTree<AIndexNameNode> {
    AIndexNameInsert* InsertUnique(AIndexNameInsert *result, const AIndexNamePair *value);        // 0x001272f0
    AIndexNameNode** EraseRange(AIndexNameNode **result, AIndexNameNode *first, AIndexNameNode *last); // 0x00127480
    // _Insert: the game has one compiled copy for both maps (the number map's, 0x00126d80).
    AIndexNameNode* InsertAt(bool addLeft, AIndexNameNode *where, const AIndexNamePair *value);

    Tree *AsTree() { return reinterpret_cast<Tree *>(this); }
};
static_assert(sizeof(AIndexNameMap) == sizeof(Tree), "a map is 12 bytes");

// ---- the two maps together ("AIndexMap", the label of its allocation)

struct AIndexMap {
    AIndexIdMap byId;           // +0x00
    AIndexNameMap byName;       // +0x0c

    AIndexMap* Construct();                                                             // 0x00127620
    void Destruct();                                                                    // 0x00127580
};
static_assert(sizeof(AIndexMap) == 0x18, "AIndexMap is 24 bytes");

// ---- the index

class AIndex {
public:
    AIndexMap *maps;            // +0x00
    char *names;                // +0x04 every name, each after the last; NULL until a header file is read

    // The index of `file`'s header file in `directory`; empty if either is NULL or the file has no definitions.
    AIndex* Construct(const char *directory, const char *file);                         // 0x001276c0
    // An empty index.
    AIndex* Construct();                                                                // 0x001279e0
    void Destruct();                                                                    // 0x00127a40

    // The name of number `id`, or "".
    const char* Lookup(int id);                                                         // 0x00126d10
    // The number of `name`, or -1 (also for NULL).
    int Lookup(const char *name);                                                       // 0x00126d40
};
static_assert(sizeof(AIndex) == 8, "AIndex is 8 bytes");

// ---- the warning beside a provisional port: code no shipped data reaches, said once, the first time it runs

inline void AudioIndexUntested(const char *what) {
    printf("[audio] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define AUDIO_INDEX_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            AudioIndexUntested(what); \
        } \
    } while (0)

#endif // DRIVING_AUDIO_INDEX_H_
