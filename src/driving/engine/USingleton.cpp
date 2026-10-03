#include "USingleton.h"

#include "UMemory.hpp"

// ---------------------------------------------------------------------------------------------------------------
// USingleton and USingletonManager, ported from the listings (0x0007d780, 0x0011b7a0-0x0011b820, 0x0011bb50).
// ---------------------------------------------------------------------------------------------------------------

// The list's push_back (a compiled copy of std::vector<USingleton *>::push_back, not ours yet)
#define SingletonVector_PushBack ((void (__fastcall *)(GameVector<USingleton *> *, int, USingleton *const *value))0x0011bae0)

static const USingletonVtable *const kSingletonVtable = (const USingletonVtable *)0x0018beb0;

// FUNC_AT(0x0007d780)
void USingleton::Destruct() {
    vtable = kSingletonVtable;
}

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
    SingletonVector_PushBack(&singletons, 0, &singleton);
}
