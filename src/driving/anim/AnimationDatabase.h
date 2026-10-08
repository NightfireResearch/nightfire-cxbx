#ifndef DRIVING_ANIM_ANIMATIONDATABASE_H_
#define DRIVING_ANIM_ANIMATIONDATABASE_H_

// ---------------------------------------------------------------------------------------------------------------
// The actors' animations: ActAnimationDatabase loads the animation banks a track's bank list names (each bank an
// EAGL object file with an "AnimationBank" symbol) and answers an animation by bank and index through the lookup
// table data\actors\anims\ALookup.bin; ActAnimGroup holds the pair of FnAnims an actor plays from one entry.
// See AnimationDatabase.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

struct AnimBank;
struct FnAnim;
class DynamicLoader;

// EAGLAnim::FnDefaultAnimBank (8 bytes, vtable 0x00189f44, the PS2 build's name): the interface over a loaded
// AnimationBank. Its eight virtuals are these methods, in this order.
class FnDefaultAnimBank {
public:
    const void *vtable;
    AnimBank *bank;                     // +0x04

    void Construct();                   // inline in the original
    void Init(AnimBank *animBank);                                              // 0x00013c80 slot 0
    int GetNumAnims();                                                          // 0x00013c90 slot 1
    uint8_t* GetAnim(const char *name); // NULL when the bank has no such name    // 0x00013cc0 slot 2
    uint8_t* GetAnim(int index);                                                // 0x00013ca0 slot 3
    const char* GetAnimName(int index);                                         // 0x00013cb0 slot 4
    int GetAnimIndex(const char *name); // -1 when the bank has no such name     // 0x00013cf0 slot 5
    uint32_t GetUnknown08();            // the bank's +0x08 (name ours)          // 0x00013d00 slot 6
    FnDefaultAnimBank* Delete(unsigned flags);                                  // 0x00013d10 slot 7
};
static_assert(sizeof(FnDefaultAnimBank) == 8, "an FnDefaultAnimBank is 8 bytes");

enum FnAnimBankSlot {
    kBankInit = 0, kBankGetNumAnims, kBankGetAnimByName, kBankGetAnim, kBankGetAnimName, kBankGetAnimIndex,
    kBankGetUnknown08, kBankDelete
};

// One loaded bank (ActAnimationDatabase::BankInfo)
struct BankInfo {                       // 0x3c
    AnimBank *bank;                     // +0x00 the object's "AnimationBank"
    FnDefaultAnimBank *anims;           // +0x04
    DynamicLoader *loader;              // +0x08
    uint8_t *data;                      // +0x0c the object file (<name>.dat)
    int32_t animCount;                  // +0x10
    char name[0x28];                    // +0x14

    void Load(const char *bankName);                                            // 0x00013d30
    void Destruct();                    // inline in the original
};
static_assert(offsetof(BankInfo, name) == 0x14, "a bank's name is at +0x14");
static_assert(sizeof(BankInfo) == 0x3c, "a BankInfo is 0x3c bytes");

// ActAnimationDatabase::PrivateData (operator new)
struct PrivateData {                    // 0x448
    enum { kMaxBanks = 18 };

    BankInfo banks[kMaxBanks];          // +0x000 by the bank's place in the bank names
    int32_t bankCount;                  // +0x438 banks loaded
    int32_t animCount;                  // +0x43c their animations
    int32_t *lookup;                    // +0x440 ALookup.bin: per bank, where its entries start; per animation,
                                        //        two anim indices within the bank (-1: none)
    uint32_t lookupSize;                // +0x444

    // Every bank empty, and the lookup table loaded
    PrivateData* Construct();                                                   // 0x00013ba0
    void Destruct();                                                            // 0x00013e90
    // The bank of that name, if it is one of the eighteen known
    void LoadBank(const char *name);                                            // 0x00013f00
    // Animation `index` of `bank`: the anim data its lookup pair names - anim from the pair's second index,
    // second from its first - or NULL for an index of -1
    void GetAnimation(int bank, int index, uint8_t **anim, uint8_t **second);  // 0x00013c00
};
static_assert(offsetof(PrivateData, bankCount) == 0x438, "the bank count is at +0x438");
static_assert(sizeof(PrivateData) == 0x448, "a PrivateData is 0x448 bytes");

class ActAnimationDatabase {            // 4
public:
    PrivateData *data;

    // Loads the banks between the "start" and "end" lines of the track's ACT_ANIM_BANK list (BkDflt.txt when
    // the track names none or it cannot be loaded).
    ActAnimationDatabase* Construct();                                          // 0x00013f90
    void Destruct();                                                            // 0x000141b0
    void GetAnimation(int bank, int index, uint8_t **anim, uint8_t **second);  // 0x00013c70
};
static_assert(sizeof(ActAnimationDatabase) == 4, "an ActAnimationDatabase is 4 bytes");

// The FnAnims of one animation, made in EAGLAnim's pool from the database's anim data.
class ActAnimGroup {                    // 0x20 ("ActAnimGroup")
public:
    float length;                       // +0x00 anim's (0 without one)
    float secondLength;                 // +0x04
    FnAnim *anim;                       // +0x08
    uint8_t *animData;                  // +0x0c
    FnAnim *second;                     // +0x10
    uint8_t *secondData;                // +0x14
    int32_t index;                      // +0x18 within the bank
    int32_t bank;                       // +0x1c

    // The animation, unless index is negative
    ActAnimGroup* Construct(int bank, int index);                               // 0x00014360
    ActAnimGroup* Construct(const ActAnimGroup *other);                         // 0x000143a0
    void Destruct();                                                            // 0x00014310
    void ChangeAnimation(int bank, int index);                                  // 0x00014240
};
static_assert(offsetof(ActAnimGroup, bank) == 0x1c, "an ActAnimGroup's bank is at +0x1c");
static_assert(sizeof(ActAnimGroup) == 0x20, "an ActAnimGroup is 0x20 bytes");

#endif // DRIVING_ANIM_ANIMATIONDATABASE_H_
