#include "InputMapping.hpp"

#include <stdio.h>
#include <string.h>

#include "DefText.hpp"
#include "InputDevice.hpp"
#include "UMemory.hpp"

#define kTableEntriesTag ((const char *)0x0018d9d8)        // "TableEntries"
#define kStringToNumberEntryTag ((const char *)0x0018d9e8)  // "StringToNumberEntry"
#define kStringToNumberTag ((const char *)0x0018d93c)       // "StringToNumber"

static const int kUnknownAction = 0x75;

// FUNC_AT(0x000507a0)
InputTable* InputTable::Construct() {
    count = 0;
    capacity = 4;
    entries = inlineEntries;
    return this;
}

// FUNC_AT(0x000507c0)
void InputTable::Destruct() {
    if (entries != inlineEntries)
        UMemory::FastFree(entries, capacity * sizeof(InputTableEntry));
}

// AUTOINJECT
void InputTable::GrowArrayForOneElement() {
    int old = capacity;
    capacity += 4;
    InputTableEntry *grown = (InputTableEntry *)UMemory::FastAlloc(capacity * sizeof(InputTableEntry), kTableEntriesTag);
    for (int i = 0; i < count; i++)
        grown[i] = entries[i];
    if (entries != inlineEntries)
        UMemory::FastFree(entries, old * sizeof(InputTableEntry));
    entries = grown;
}

// FUNC_AT(0x00050860)
InputToAction* InputToAction::Construct(InputDevice *device) {
    int n = device->GetNumDeviceScalar();
    numTables = n;
    int *raw = (int *)GameArrayNew(n * sizeof(InputTable) + 4);   // with the count cookie, as new InputTable[n]
    InputTable *array = nullptr;
    if (raw != nullptr) {
        raw[0] = n;
        array = (InputTable *)(raw + 1);
        for (int i = 0; i < n; i++)
            array[i].Construct();
    }
    tables = array;
    scalarNames = (StringToNumberEntry *)UMemory::FastAlloc(numTables * 8 + 8, kStringToNumberEntryTag);
    for (int i = 0; i < numTables; i++) {
        scalarNames[i].number = i;
        scalarNames[i].string = device->scalars[i].name;
    }
    scalarNames[numTables].number = 0;
    scalarNames[numTables].string = nullptr;
    void *memory = UMemory::FastAlloc(sizeof(StringToNumber), kStringToNumberTag);
    scalarNameLookup = memory ? ((StringToNumber *)memory)->Construct(scalarNames) : nullptr;
    return this;
}

// FUNC_AT(0x000509c0)
void InputToAction::Destruct() {
    if (tables != nullptr) {
        int *raw = (int *)tables - 1;
        for (int i = raw[0] - 1; i >= 0; i--)   // the eh vector destructor iterator's order
            tables[i].Destruct();
        GameArrayDelete(raw);
    }
    if (scalarNameLookup != nullptr) {
        scalarNameLookup->Destruct();
        UMemory::FastFree(scalarNameLookup, sizeof(StringToNumber));
    }
    UMemory::FastFree(scalarNames, numTables * 8 + 8);
}

// AUTOINJECT
void InputToAction::Reset() {
    for (int i = 0; i < numTables; i++)
        tables[i].count = 0;
}

// Lines are ACTION SCALAR [METHOD [THRESHOLD]]. The first line (the column headings) is skipped; FRONTEND or END
// (any case) ends the mappings. An unknown action becomes 0x75, a missing or unknown method 0 (U), and the
// threshold defaults to 0.5 for > and < and to 0 otherwise.
//
// Where the original misbehaves on a malformed file this is safe instead: a blank line, or one with no CR before
// the end of the text, ends the file (the original crashes), and a line whose control name is missing or unknown
// is skipped (the original writes before the first table). No shipped file has any of these.
// AUTOINJECT
void InputToAction::LoadParseDefFile(char *text) {
    DefLine line;
    const char *p = text;
    for (;;) {
        p = DefNextLine(p);
        if (p == nullptr)
            return;
        const char *actionName = line.First(p);
        if (actionName == nullptr || DefStricmp(actionName, "FRONTEND") == 0 || DefStricmp(actionName, "END") == 0)
            return;
        const char *scalarName = line.Next();
        int action = getActionID((char *)actionName);
        int scalar = scalarName ? scalarNameLookup->ConvertStringToNumber((char *)scalarName) : -1;
        int method = 0;
        if (const char *m = line.Next()) {
            int v = UpdateMethodNames->ConvertStringToNumber((char *)m);
            if (v != -1)
                method = v;
        }
        float threshold = (method >= 3 && method <= 4) ? 0.5f : 0.0f;
        if (const char *t = line.Next())
            sscanf(t, "%f", &threshold);
        if (scalar < 0)
            continue;
        InputTable *table = &tables[scalar];
        if (table->count >= table->capacity)
            table->GrowArrayForOneElement();
        InputTableEntry *entry = &table->entries[table->count];
        entry->updateMethod = method;
        entry->threshold = threshold;
        entry->action = action;
        table->count++;
    }
}

// AUTOINJECT
void AdvanceFilePtr(char **text) {
    *text = (char *)DefNextLine(*text);
}

// AUTOINJECT
int getActionID(char *name) {
    int id = ActionNames->ConvertStringToNumber(name);
    return id == -1 ? kUnknownAction : id;
}
