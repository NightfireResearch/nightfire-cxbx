#ifndef DRIVING_ENGINE_USINGLETON_H_
#define DRIVING_ENGINE_USINGLETON_H_

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// USingleton: the base of the renderer's and the game's managers (the fog, the light, state, texture context and
// decal managers, the tuning databases, the attribute system, ...), and USingletonManager, which keeps a list of
// them so they can all be reset when a track restarts and killed when the game ends. The manager is a function-
// local static (0x001e47c0), made on first use by SingletonManager. See USingleton.cpp.
// ---------------------------------------------------------------------------------------------------------------

class USingleton;

// The first three slots of a singleton's vtable
struct USingletonVtable {
    void *slot0;                                            // the deleting destructor
    void (__fastcall *reset)(USingleton *singleton, int);   // +0x04
    void (__fastcall *kill)(USingleton *singleton, int);    // +0x08
};

// The base class: only its vtable pointer (each manager's own fields follow it).
class USingleton {
public:
    const USingletonVtable *vtable;

    void Destruct();   // 0x0007d780: back to the base class's vtable (0x0018beb0)
    // The base class's scalar deleting destructor (0x0003d5f0, slot 0 of its vtable; the compiler placed it among
    // the tuning manager's functions). Bit 0 of `flags` frees the object too.
    USingleton* Delete(unsigned flags);
};

// std::vector<USingleton *> (0x10 bytes) as the game compiled it: the allocator's word, then begin, end and the end
// of the storage. Its growth is the game's own copy of push_back and _Insert_n.
struct SingletonVector {
    uint32_t allocator;
    USingleton **first;
    USingleton **last;
    USingleton **end;

    // push_back (0x0011bae0, Ghidra: FUN_0011bae0).
    void PushBack(USingleton *const *value);
    // _Insert_n (0x0011b820, Ghidra: FUN_0011b820): `count` copies of *value before `where`, growing by half.
    void InsertN(USingleton **where, uint32_t count, USingleton *const *value);
    // _Xlen (0x00059ca0): throws std::length_error("vector<T> too long").
    static void Xlen();
};
static_assert(sizeof(SingletonVector) == 0x10, "a vector is 16 bytes");

class USingletonManager {
public:
    SingletonVector singletons;            // +0x00

    void ResetAll();                       // 0x0011b7a0
    void KillAll();                        // 0x0011b7d0, and frees the list
    void Register(USingleton *singleton);  // 0x0011bb50
    // The static's destructor, registered with atexit (0x00059c30): kills them all, frees the vector.
    void Destruct();
};
static_assert(sizeof(USingletonManager) == 0x10, "the singleton manager is a vector");

// The singleton manager, made on first use (0x00059d20).
USingletonManager* SingletonManager();

#endif // DRIVING_ENGINE_USINGLETON_H_
