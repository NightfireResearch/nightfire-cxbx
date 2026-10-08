#ifndef DRIVING_WORLD_SOUNDGROUP_H_
#define DRIVING_WORLD_SOUNDGROUP_H_

// ---------------------------------------------------------------------------------------------------------------
// WSoundGroup: the world's sounds, by id - a std::map from an id to a WSound, the sound a WWorld keeps playing
// while its track is open. WWorld::Open marks every sound unused (Start), adds the track's (Add), then deletes
// those nobody added again (End); Update runs once per audio frame. See SoundGroup.cpp.
//
// The sounds are the audio framework's (audio/Sound.h): WSound is the world's kind of AWorldSound, a sound with a
// voice.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../audio/Sound.h"             // ABaseSound, AWorldSound, AVoice
#include "../data/Tree.h"
#include "../engine/RbTree.h"

struct RPathHandle;

class WSound : public AWorldSound {
public:
    RPathHandle *pathHandle;    // +0x150 the path it follows; NULL from the constructor
    uint8_t unknown154[0xc];

    WSound* Construct(int bank, int index);     // inlined in WSoundGroup::Add
    void Destruct();                                                            // 0x000cc8f0
    WSound* Delete(unsigned flags);                                             // 0x000cc900

    // Sets +0x118 and +0x11c (from a track sound's record): a non-zero sum clears each view's loop flag, a sum
    // below the old one clears +0x130. The name is ours (Ghidra: FUN_000d11a0).
    void SetUnknown118(float first, float second);                              // 0x000d11a0
    // Replaces the extra voice with a new one for the id (bank << 7 | index). The name is ours (FUN_000d1570).
    void SetVoice(int id);                                                      // 0x000d1570
};
static_assert(sizeof(WSound) == 0x160, "WSound is 352 bytes");

// ---- std::map<int, WSound *> (MSVC 7's _Tree; engine/RbTree.h)

struct SoundMapValue {
    int32_t id;                 // the key (compared signed)
    WSound *sound;
};

struct SoundMapNode : RbTreeNode<SoundMapNode, SoundMapValue> {};
static_assert(sizeof(SoundMapNode) == sizeof(TreeNode), "a sound map node is the data layer's map node");

struct SoundMapInsert {
    SoundMapNode *node;
    bool inserted;
};

// The map's own compiled code. Its erase, _Insert, _Erase and erase(first, last) are the data layer's maps'
// instruction for instruction (data/Tree.h); Lrotate is the rotation every map of the game calls, and Find is
// shared with the other maps keyed by int (RTextureContextManager's, AIndex's).
struct WSoundMap : RbTree<SoundMapNode> {
    void Lrotate(SoundMapNode *node);                                           // 0x000cca30
    void EraseSubtree(SoundMapNode *node);                                      // 0x000cca90
    SoundMapNode** Find(SoundMapNode **result, const int32_t *id);              // 0x000ccbb0
    SoundMapNode** EraseAt(SoundMapNode **result, SoundMapNode *where);         // 0x000ccc20
    SoundMapNode** InsertAt(SoundMapNode **result, bool addLeft, SoundMapNode *where,
                            const SoundMapValue *value);                        // 0x000ccef0
    SoundMapNode** EraseRange(SoundMapNode **result, SoundMapNode *first, SoundMapNode *last);   // 0x000cd0d0
    SoundMapInsert* InsertUnique(SoundMapInsert *result, const SoundMapValue *value);           // 0x000cd2b0
    void Destruct();                                                            // 0x000cd560

    Tree *AsTree() { return reinterpret_cast<Tree *>(this); }
};
static_assert(sizeof(WSoundMap) == sizeof(Tree), "a map is 12 bytes");

// ---- the group

class WSoundGroup {
public:
    uint32_t unknown00;         // +0x00 zero
    uint32_t unknown04;
    uint32_t unknown08;
    WSoundMap *sounds;          // +0x0c

    WSoundGroup* Construct();                                                   // 0x000cd5a0
    void Destruct();                                                            // 0x000cd630

    void Start();               // marks every sound unused                    0x000ccad0
    void Update();                                                              // 0x000ccb40
    void End();                 // deletes the sounds still unused             0x000cd190
    void Clear();               // deletes every sound                         0x000cd220

    // The sound with the id, made (or remade, if it plays another index) to play the bank's sound `index`, and
    // marked used.
    WSound* Add(int id, int bank, int index);                                   // 0x000cd370
    // The same from a "bank: sound" name.
    WSound* Add(int id, const char *name);                                      // 0x000cd4c0
    // The same from a bank number and index packed as bank * 128 + index (name ours).
    WSound* AddPacked(int id, int packed);                                      // 0x000cd540
};
static_assert(sizeof(WSoundGroup) == 0x10, "WSoundGroup is 16 bytes");

#endif // DRIVING_WORLD_SOUNDGROUP_H_
