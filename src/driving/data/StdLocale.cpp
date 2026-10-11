// The C++ library's locale helpers. See StdLocale.h.

#include "StdLocale.h"

#include "DebugVarUntested.h"
#include "../engine/UMemory.hpp"
#include "../../common/xbeOverload.h"

#include <stddef.h>
#include <stdint.h>

using namespace GameStd;

// ---- the C runtime's, called at their addresses

#define Lockit_Construct ((Lockit *(__fastcall *)(Lockit *, int, int))0x00130cef)
#define Lockit_Destruct ((void (__fastcall *)(Lockit *, int))0x00130d10)
#define IosBase_Destruct ((void (__fastcall *)(IosBase *, int))0x00131191)

#define FacetVtable ((const void *const *)0x0018bc6c)

namespace {

constexpr int kLockLocale = 0;                  // _LOCK_LOCALE
constexpr uint32_t kRefsFull = 0xffffffff;      // (size_t)(-1): a count that no longer moves

typedef Facet* (Facet::*FacetDeleteMethod)(unsigned flags);

} // namespace

// FUNC_AT(0x000378c0)
void GameStd::Facet::Incref() {
    Lockit lock;
    Lockit_Construct(&lock, 0, kLockLocale);
    if (refs < kRefsFull)
        refs++;
    Lockit_Destruct(&lock, 0);
}

// FUNC_AT(0x000378f0)
Facet* GameStd::Facet::Decref() {
    Lockit lock;
    Lockit_Construct(&lock, 0, kLockLocale);
    if (refs > 0 && refs < kRefsFull)
        refs--;
    Facet *dead = refs == 0 ? this : NULL;
    Lockit_Destruct(&lock, 0);
    return dead;
}

// FUNC_AT(0x00037930)
void GameStd::Facet::Destruct() {
    vtable = FacetVtable;
}

// Only a facet of the base class itself is deleted through this vtable, and none is made.
//
// FUNC_AT(0x00037960)
Facet* GameStd::Facet::Delete(unsigned flags) {
    DEBUGVAR_UNTESTED("locale::facet's deleting destructor");
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x00037940)
void GameStd::Locale::Destruct() {
    if (impl == NULL)
        return;
    Facet *dead = impl->Decref();
    if (dead != NULL)
        (dead->*XbeVirtual<FacetDeleteMethod>(dead, 0))(1);
}

// FUNC_AT(0x00037980)
Locale* GameStd::IosBase::GetLoc(Locale *result) {
    Facet *impl = locale->impl;
    result->impl = impl;
    impl->Incref();
    return result;
}

// Only an ios_base of the base class itself is deleted through this vtable, and none is made.
//
// FUNC_AT(0x000379c0)
IosBase* GameStd::IosBase::Delete(unsigned flags) {
    DEBUGVAR_UNTESTED("ios_base's deleting destructor");
    IosBase_Destruct(this, 0);
    if (flags & 1)
        OperatorDelete(this);
    return this;
}
