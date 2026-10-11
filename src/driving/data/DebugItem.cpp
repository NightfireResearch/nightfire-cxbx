// The debug menu items' small virtual methods. See DebugItem.h.

#include "DebugItem.h"

#include "DebugVarUntested.h"
#include "../engine/UMemory.hpp"
#include "../../common/xbeOverload.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DebugItemVtable ((const void *const *)0x0018bc78)

namespace {

enum : int {
    kSlotIncrease = 1,
    kSlotDecrease = 2,
    kSlotIncrement = 5,
};

constexpr uint32_t kItemHidden = 0x2;           // flags

typedef void (DebugItem::*StepMethod)(float scale);
typedef void (DebugItem::*ActionMethod)();

} // namespace

// FUNC_AT(0x000379e0)
void DebugItem::Destruct() {
    DEBUGVAR_UNTESTED("DebugItem::~DebugItem");
    vtable = DebugItemVtable;
}

// FUNC_AT(0x00037a90)
DebugItem* DebugItem::Delete(unsigned flags) {
    DEBUGVAR_UNTESTED("DebugItem's deleting destructor");
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x000379f0)
void DebugItem::Increment() {
    DEBUGVAR_UNTESTED("DebugItem::Increment");
    (this->*XbeVirtual<StepMethod>(this, kSlotIncrease))(0.0f);
}

// FUNC_AT(0x00037a00)
void DebugItem::Decrement() {
    DEBUGVAR_UNTESTED("DebugItem::Decrement");
    (this->*XbeVirtual<StepMethod>(this, kSlotDecrease))(0.0f);
}

// FUNC_AT(0x00037a20)
void DebugItem::Activate() {
    DEBUGVAR_UNTESTED("DebugItem::Activate");
    (this->*XbeVirtual<ActionMethod>(this, kSlotIncrement))();
}

// FUNC_AT(0x00037a30)
CARP::CountedString* DebugItem::SubMenuNamespace(CARP::CountedString *result) {
    DEBUGVAR_UNTESTED("DebugItem::SubMenuNamespace");
    result->text = "";
    result->length = int(strlen(result->text));
    return result;
}

// The name and its length are a counted string (an LString on the PS2).
//
// FUNC_AT(0x00037a60)
char* DebugItem::Name() {
    DEBUGVAR_UNTESTED("DebugItem::Name");
    return name.CString();
}

// FUNC_AT(0x00037a80)
int DebugItem::IsHidden() {
    DEBUGVAR_UNTESTED("DebugItem::IsHidden");
    return (flags & kItemHidden) != 0;
}

// FUNC_AT(0x00037ac0)
int DebugData::IsIndexed() {
    DEBUGVAR_UNTESTED("DebugData::IsIndexed");
    return indexer != NULL;
}

// FUNC_AT(0x00037ab0)
DebugData* DebugData::GetIndexer() {
    return indexer;
}
