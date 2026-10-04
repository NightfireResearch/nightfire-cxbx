#include "USingleton.h"

#include "CoreFoundation.h"
#include "UMemory.hpp"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// USingleton and USingletonManager, ported from the listings (0x0003d5f0, 0x0007d780, 0x0011b7a0-0x0011b820,
// 0x0011bb50, and the static instance's functions at 0x00059c30-0x00059d20), with the manager's vector.
//
// The vector's growth calls the STL's own helpers - the uninitialised copies and fills, copy_backward, the
// deallocation - at their addresses, in the original's order with its arguments. The original's catch block (free
// the new storage, rethrow) has nothing to catch here: copying pointers cannot throw.
// ---------------------------------------------------------------------------------------------------------------

#define Vector_UninitializedCopy ((USingleton **(*)(USingleton **, USingleton **, USingleton **, SingletonVector *, uint32_t))0x000bfeb0)
#define Vector_UninitializedFillN ((void (*)(USingleton **, uint32_t, USingleton *const *, SingletonVector *, uint32_t))0x000b2ec0)
#define Vector_Deallocate ((void (__fastcall *)(SingletonVector *, int, USingleton **, uint32_t))0x000ad630)
#define Vector_Ucopy ((USingleton **(__fastcall *)(SingletonVector *, int, USingleton **, USingleton **, USingleton **))0x000c1230)
#define Vector_Ufill ((void (__fastcall *)(SingletonVector *, int, USingleton **, uint32_t, USingleton *const *))0x000c1290)
#define Vector_CopyBackward ((void (*)(USingleton ***, USingleton **, USingleton **, USingleton **))0x000b2e40)
#define Vector_Fill ((void (*)(USingleton **, USingleton **, USingleton *const *))0x0004f3e0)

// The C runtime's atexit, the game's (its table holds the destructor's address).
#define CRT_atexit ((int (*)(void (*)(void)))0x00132a7b)
// The manager's destructor as the static initialisers register it: a thunk to USingletonManager::Destruct
// (StaticInit.cpp's DestroyStatic_001e47c0), passed at its original address as the original passes it.
#define SingletonManager_AtExit ((void (*)(void))0x0015ce10)

#define TheSingletonManager (*(USingletonManager *)0x001e47c0)
#define SingletonManagerMade U32_AT(0x001e47d0)

static const USingletonVtable *const kSingletonVtable = (const USingletonVtable *)0x0018beb0;

constexpr uint32_t kMaxSize = 0x3fffffff;

// ---- USingleton

// FUNC_AT(0x0007d780)
void USingleton::Destruct() {
    vtable = kSingletonVtable;
}

// FUNC_AT(0x0003d5f0)
USingleton* USingleton::Delete(unsigned flags) {
    CORE_UNTESTED("USingleton scalar deleting destructor");
    vtable = kSingletonVtable;
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// ---- the vector

// FUNC_AT(0x0011b820)
void SingletonVector::InsertN(USingleton **where, uint32_t count, USingleton *const *value) {
    USingleton *copy = *value;   // the value is copied first: it may live in the vector
    uint32_t capacity = first != NULL ? uint32_t(end - first) : 0;
    if (count == 0)
        return;
    uint32_t size = first != NULL ? uint32_t(last - first) : 0;
    if (kMaxSize - size < count) {
        CORE_UNTESTED("SingletonVector::InsertN's length error");
        Xlen();
        return;
    }
    if (capacity < size + count) {
        uint32_t grown = kMaxSize - capacity / 2 < capacity ? 0 : capacity + capacity / 2;
        if (grown < size + count)
            grown = size + count;
        uint32_t bytes = grown * sizeof(USingleton *);
        USingleton **storage = static_cast<USingleton **>(UMemory::FastAlloc(bytes, "STL"));
        USingleton **at = Vector_UninitializedCopy(first, where, storage, this, count);
        Vector_UninitializedFillN(at, count, &copy, this, count);
        Vector_UninitializedCopy(where, last, at + count, this, count);
        uint32_t newSize = count + (first != NULL ? uint32_t(last - first) : 0);
        if (first != NULL)
            Vector_Deallocate(this, 0, first, uint32_t(end - first));
        end = reinterpret_cast<USingleton **>(reinterpret_cast<uint8_t *>(storage) + bytes);
        last = storage + newSize;
        first = storage;
    } else if (uint32_t(last - where) < count) {
        // Room enough: push_back never comes here (it only inserts when the vector is full).
        CORE_UNTESTED("SingletonVector::InsertN in the middle");
        Vector_Ucopy(this, 0, where, last, where + count);
        Vector_Ufill(this, 0, last, count - uint32_t(last - where), &copy);
        last += count;
        Vector_Fill(where, last - count, &copy);
    } else {
        CORE_UNTESTED("SingletonVector::InsertN in the middle");
        USingleton **oldLast = last;
        last = Vector_Ucopy(this, 0, oldLast - count, oldLast, oldLast);
        USingleton **ignored;
        Vector_CopyBackward(&ignored, where, oldLast - count, oldLast);
        Vector_Fill(where, where + count, &copy);
    }
}

// FUNC_AT(0x0011bae0)
void SingletonVector::PushBack(USingleton *const *value) {
    uint32_t size = first != NULL ? uint32_t(last - first) : 0;
    if (first != NULL && size < uint32_t(end - first)) {
        Vector_UninitializedFillN(last, 1, value, this, uint32_t(uintptr_t(value)));
        last++;
        return;
    }
    InsertN(last, 1, value);
}

// FUNC_AT(0x00059ca0)
void SingletonVector::Xlen() {
    CORE_UNTESTED("USingletonManager's vector _Xlen");
    ThrowLengthError("vector<T> too long");
}

// ---- USingletonManager

// FUNC_AT(0x0011b7a0)
void USingletonManager::ResetAll() {
    for (USingleton **s = singletons.first; s != singletons.last; s++)
        (*s)->vtable->reset(*s, 0);
}

// FUNC_AT(0x0011b7d0)
void USingletonManager::KillAll() {
    for (USingleton **s = singletons.first; s != singletons.last; s++)
        (*s)->vtable->kill(*s, 0);
    if (singletons.first != NULL)
        UMemory::FastFree(singletons.first, (singletons.end - singletons.first) * sizeof(USingleton *));
    singletons.first = NULL;
    singletons.last = NULL;
    singletons.end = NULL;
}

// FUNC_AT(0x0011bb50)
void USingletonManager::Register(USingleton *singleton) {
    singletons.PushBack(&singleton);
}

// FUNC_AT(0x00059c30)
void USingletonManager::Destruct() {
    KillAll();
    if (singletons.first != NULL)
        UMemory::FastFree(singletons.first, unsigned(singletons.end - singletons.first) * sizeof(USingleton *));
    singletons.first = NULL;
    singletons.last = NULL;
    singletons.end = NULL;
}

// FUNC_AT(0x00059d20)
USingletonManager* SingletonManager() {
    if ((SingletonManagerMade & 1) == 0) {
        SingletonManagerMade |= 1;
        TheSingletonManager.singletons.first = NULL;
        TheSingletonManager.singletons.last = NULL;
        TheSingletonManager.singletons.end = NULL;
        CRT_atexit(SingletonManager_AtExit);
    }
    return &TheSingletonManager;
}
