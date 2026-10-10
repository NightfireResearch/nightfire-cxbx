#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "InputConfig.h"

#include <string.h>

#include "GameLoop.h"
#include "IOModule.hpp"
#include "InputDevice.hpp"
#include "UFileLoader.h"
#include "UMemory.hpp"
#include "../data/SymbolTable.h"
#include "../platform/RealMemory.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// InputConfigManager, InputToAction, InputTable and the text helpers (0x0004f190, 0x0004ff50..0x00050b80,
// 0x00051e90..0x00052010), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

#define Launch (*(LaunchPage *)0x00243b90)
#define TuningLevel ((const char *)0x00244084)   // the track the tuning databases are read for

#define gInputConfigManager (*(InputConfigManager *)0x001e455c)
#define gInputConfigManagerMade U32_AT(0x001e458c)

// The names the .def files use, built by static initialisers: the actions (over the table at 0x001b6ba0) and the
// update methods (0x001b6fd0); and the front end's slot names, ButtonA .. TITLE.
#define gActionNames (*(StringToNumber *)0x001e23e0)
#define gUpdateMethodNames (*(StringToNumber *)0x001e4590)
#define kFrontEndSlotNames ((StringToNumberEntry *)0x001b6f58)

// The C runtime's, the game's: strtok keeps its place between calls, _stricmp and sscanf follow the game's locale,
// and atol wraps rather than saturates.
#define Crt_strtok ((char *(*)(char *, const char *))0x0013282a)
#define Crt_stricmp ((int (*)(const char *, const char *))0x00134537)
#define Crt_atol ((long (*)(const char *))0x00133d51)
#define Crt_sscanf ((int (*)(const char *, const char *, ...))0x00133234)

static const char kSeparators[] = "\t ";
static const int kActionCount = 0x75;   // one past ACTION_PLUGUNPLUG, the last action

// The line `text` starts with, up to its carriage return, copied into `line` (inlined at each use in the original)
static void CopyLine(char *line, const char *text) {
    size_t length = strchr(text, '\r') - text;
    strncpy(line, text, length);
    line[length] = 0;
}

// ---- the text helpers

// FUNC_AT(0x00050970)
void AdvanceFilePtr(char **text) {
    char *next = strchr(*text, '\n');
    if (next != nullptr)
        next++;
    *text = next;
}

// FUNC_AT(0x0004f190)
int getActionID(char *name) {
    int id = gActionNames.ConvertStringToNumber(name);
    if (id == -1)
        id = kActionCount;
    return id;
}

// FUNC_AT(0x00051e90)
char* __fastcall BuildFileName(char *buffer, int, const char *directory, const char *name, const char *extension) {
    strcpy(buffer, directory);
    strcat(buffer, name);
    if (*extension != 0) {
        strcat(buffer, ".");
        strcat(buffer, extension);
    }
    return buffer;
}

// FUNC_AT(0x00051f30)
char* __fastcall BuildPath(char *buffer, int, const char *directory, const char *subdirectory, const char *name,
                           const char *extension) {
    strcpy(buffer, directory);
    strcat(buffer, subdirectory);
    strcat(buffer, "/");
    strcat(buffer, name);
    if (*extension != 0) {
        strcat(buffer, ".");
        strcat(buffer, extension);
    }
    return buffer;
}

// ---- InputTable

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

// FUNC_AT(0x000507e0)
void InputTable::GrowArrayForOneElement() {
    int oldCapacity = capacity;
    capacity += 4;
    InputTableEntry *grown =
        static_cast<InputTableEntry *>(UMemory::FastAlloc(capacity * sizeof(InputTableEntry), "TableEntries"));
    for (int i = 0; i < count; i++)
        grown[i] = entries[i];
    if (entries != inlineEntries)
        UMemory::FastFree(entries, oldCapacity * sizeof(InputTableEntry));
    entries = grown;
}

// ---- InputToAction

// The tables come from new[] with their count in the word before them, constructed in order and destroyed in
// reverse, as the C runtime's vector iterators (0x0013326e, 0x0013332e) do.
// FUNC_AT(0x00050860)
InputToAction* InputToAction::Construct(InputDevice *device) {
    int scalarCount = device->GetNumDeviceScalar();
    numTables = scalarCount;
    int32_t *block = static_cast<int32_t *>(OperatorNewArray(scalarCount * sizeof(InputTable) + sizeof(int32_t)));
    InputTable *array = nullptr;
    if (block != nullptr) {
        block[0] = scalarCount;
        array = reinterpret_cast<InputTable *>(block + 1);
        for (int i = 0; i < scalarCount; i++)
            array[i].Construct();
    }
    tables = array;

    scalarNames = static_cast<StringToNumberEntry *>(
        UMemory::FastAlloc((numTables + 1) * sizeof(StringToNumberEntry), "StringToNumberEntry"));
    for (int i = 0; i < numTables; i++) {
        scalarNames[i].number = i;
        scalarNames[i].string = device->scalars[i].name;
    }
    scalarNames[numTables].number = 0;
    scalarNames[numTables].string = nullptr;

    StringToNumber *lookup = static_cast<StringToNumber *>(UMemory::FastAlloc(sizeof(StringToNumber), "StringToNumber"));
    scalarLookup = lookup != nullptr ? lookup->Construct(scalarNames) : nullptr;
    return this;
}

// FUNC_AT(0x000509c0)
void InputToAction::Destruct() {
    if (tables != nullptr) {
        int32_t *block = reinterpret_cast<int32_t *>(tables) - 1;
        for (int i = block[0]; i-- > 0;)
            tables[i].Destruct();
        OperatorDelete(block);
    }
    if (StringToNumber *lookup = scalarLookup) {
        lookup->Destruct();
        UMemory::FastFree(lookup, sizeof(StringToNumber));
    }
    UMemory::FastFree(scalarNames, (numTables + 1) * sizeof(StringToNumberEntry));
}

// FUNC_AT(0x00050990)
void InputToAction::Reset() {
    for (int i = 0; i < numTables; i++)
        tables[i].count = 0;
}

// The first line is skipped. Nothing is checked: an unknown control indexes table -1, a line without a carriage
// return runs off the text, as in the original.
// FUNC_AT(0x00050a20)
void InputToAction::LoadParseDefFile(char *text) {
    char line[256];
    for (;;) {
        AdvanceFilePtr(&text);
        CopyLine(line, text);
        char *actionName = Crt_strtok(line, kSeparators);
        if (Crt_stricmp(actionName, "FRONTEND") == 0 || Crt_stricmp(actionName, "END") == 0)
            return;
        char *scalarName = Crt_strtok(nullptr, kSeparators);
        int action = getActionID(actionName);
        int scalar = scalarLookup->ConvertStringToNumber(scalarName);

        int method = UPDATE_ON_CHANGE;
        if (char *methodName = Crt_strtok(nullptr, kSeparators)) {
            int named = gUpdateMethodNames.ConvertStringToNumber(methodName);
            if (named != -1)
                method = named;
        }
        float threshold = 0.0f;
        if (method >= UPDATE_ABOVE && method <= UPDATE_BELOW)
            threshold = 0.5f;
        if (char *thresholdText = Crt_strtok(nullptr, kSeparators))
            Crt_sscanf(thresholdText, "%f", &threshold);

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

// ---- InputConfigManager

// FUNC_AT(0x00050270)
InputConfigManager* InputConfigManager::Get() {
    if (!(gInputConfigManagerMade & 1)) {
        gInputConfigManagerMade |= 1;
        InputConfigManager &m = gInputConfigManager;
        m.initialised = false;
        m.frontEndSlots = nullptr;
        m.frontEndDefinition = nullptr;
        m.unknown2C = nullptr;
        m.currentType = kInputNoType;
        for (int type = 0; type < 2; type++) {
            m.currentConfig[type] = 0;
            m.numConfigs[type] = 0;
            m.configs[type] = nullptr;
        }
    }
    return &gInputConfigManager;
}

// A section's array: names empty, no text, no labels (the constructor, inlined)
static InputConfig *NewConfigs(int count) {
    InputConfig *configs = static_cast<InputConfig *>(OperatorNewArray(count * sizeof(InputConfig)));
    if (configs != nullptr) {
        for (int i = 0; i < count; i++) {
            memset(configs[i].localeIDs, 0, sizeof(configs[i].localeIDs));
            configs[i].name[0] = 0;
            configs[i].definition = nullptr;
        }
    }
    return configs;
}

// Each section's first line gives its count; every line after it, up to the next section or END, a .def file. A
// name before any section would go to configs[2], as in the original; Master.def starts with a section.
// FUNC_AT(0x000502d0)
void InputConfigManager::ParseMasterConfigFile(char *text) {
    char line[256];
    int type = kInputNoType;
    int named[2] = {0, 0};
    CopyLine(line, text);
    char *token = Crt_strtok(line, kSeparators);
    while (Crt_stricmp(token, "END") != 0) {
        if (Crt_stricmp(token, "Driving") == 0) {
            type = kInputDriving;
            numConfigs[kInputDriving] = Crt_atol(Crt_strtok(nullptr, kSeparators));
            configs[kInputDriving] = NewConfigs(numConfigs[kInputDriving]);
        } else if (Crt_stricmp(token, "POV") == 0) {
            type = kInputPOV;
            numConfigs[kInputPOV] = Crt_atol(Crt_strtok(nullptr, kSeparators));
            configs[kInputPOV] = NewConfigs(numConfigs[kInputPOV]);
        } else {
            strcpy(configs[type][named[type]].name, token);
            named[type]++;
        }
        AdvanceFilePtr(&text);
        CopyLine(line, text);
        token = Crt_strtok(line, kSeparators);
    }
}

// The labels after the FRONTEND line, by slot name, up to END. The first line is skipped.
// FUNC_AT(0x0004ff50)
void InputConfigManager::ParseDefFileForFrontEnd(InputConfig *config, char *text) {
    char line[256];
    bool seekingFrontEnd = true;
    AdvanceFilePtr(&text);
    CopyLine(line, text);
    char *token = Crt_strtok(line, kSeparators);
    while (Crt_stricmp(token, "END") != 0) {
        if (seekingFrontEnd) {
            if (Crt_stricmp(token, "FRONTEND") == 0)
                seekingFrontEnd = false;
        } else {
            int slot = frontEndSlots->ConvertStringToNumber(token);
            config->localeIDs[slot] = Crt_atol(Crt_strtok(nullptr, kSeparators));
        }
        AdvanceFilePtr(&text);
        CopyLine(line, text);
        token = Crt_strtok(line, kSeparators);
    }
}

// The choice of a type's configuration, as the launch page keeps it
static void StoreChoice(int type, int config) {
    switch (type) {
    case kInputDriving:
        Launch.drivingConfig = config;
        break;
    case kInputPOV:
        Launch.povConfig = config;
        break;
    }
}

// The active pad's mappings: the front end's, then the current configuration's (inlined in both callers)
static void LoadMappings(InputConfigManager *manager) {
    char *definition = manager->configs[manager->currentType][manager->currentConfig[manager->currentType]].definition;
    InputToAction *mapping = IOModule::GetIOModule()->inputActionMappings[Launch.controllerPort];
    mapping->Reset();
    mapping->LoadParseDefFile(manager->frontEndDefinition);
    if (definition != nullptr)
        mapping->LoadParseDefFile(definition);
}

// FUNC_AT(0x000504d0)
void InputConfigManager::InitAndPreload() {
    char *master = static_cast<char *>(UFileLoader::FileLoad("data/Control/Master.def", 0));
    ParseMasterConfigFile(master);
    MEM_free(master);
    frontEndDefinition = static_cast<char *>(UFileLoader::FileLoad("data/Control/FrontEnd.def", 0));

    for (int type = 0; type < 2; type++) {
        for (int i = 0; i < numConfigs[type]; i++) {
            char path[64];
            strcpy(path, "data/Control/");
            strcat(path, configs[type][i].name);
            configs[type][i].definition = static_cast<char *>(UFileLoader::FileLoad(path, 0));
        }
    }

    StringToNumber *slots = static_cast<StringToNumber *>(UMemory::FastAlloc(sizeof(StringToNumber), "StringToNumber"));
    frontEndSlots = slots != nullptr ? slots->Construct(kFrontEndSlotNames) : nullptr;
    for (int type = 0; type < 2; type++)
        for (int i = 0; i < numConfigs[type]; i++)
            ParseDefFileForFrontEnd(&configs[type][i], configs[type][i].definition);
    if (StringToNumber *lookup = frontEndSlots) {
        lookup->Destruct();
        UMemory::FastFree(lookup, sizeof(StringToNumber));
    }
    frontEndSlots = nullptr;

    initialised = true;
    currentType = kInputDriving;
    currentConfig[kInputDriving] = Launch.drivingConfig;
    currentConfig[kInputPOV] = Launch.povConfig;
    inverted = Launch.invertedControls != 0;
    for (int type = 0; type < 2; type++) {
        if (currentConfig[type] >= numConfigs[type]) {
            currentConfig[type] = 0;
            StoreChoice(type, 0);
        }
    }
    LoadMappings(this);
}

// FUNC_AT(0x00050080)
void InputConfigManager::Shutdown() {
    for (int type = 0; type < 2; type++) {
        for (int i = 0; i < numConfigs[type]; i++) {
            if (configs[type][i].definition != nullptr) {
                MEM_free(configs[type][i].definition);
                configs[type][i].definition = nullptr;
            }
        }
    }
    if (frontEndDefinition != nullptr) {
        MEM_free(frontEndDefinition);
        frontEndDefinition = nullptr;
    }
    if (unknown2C != nullptr) {
        MEM_free(unknown2C);
        unknown2C = nullptr;
    }
    for (int type = 0; type < 2; type++) {
        OperatorDelete(configs[type]);
        configs[type] = nullptr;
    }
    initialised = false;
}

// FUNC_AT(0x00050140)
void InputConfigManager::SetInverted(bool on) {
    inverted = on;
    Launch.invertedControls = on;
}

// FUNC_AT(0x00050160)
bool InputConfigManager::IsInverted() {
    return inverted;
}

// FUNC_AT(0x00050170)
int InputConfigManager::GetNumConfigs(int type) {
    return numConfigs[type];
}

// FUNC_AT(0x00050180)
int InputConfigManager::GetCurrentConfig(int type) {
    if (type == kInputNoType)
        type = currentType;
    return currentConfig[type];
}

// The ids are locale string ids; 0xaa1 is put in the place of the others shown.
// FUNC_AT(0x000501a0)
int InputConfigManager::GetLocaleID(int type, int config, int slot) {
    int id = configs[type][config].localeIDs[slot];
    switch (id) {
    case 0x590:
        if (Crt_stricmp(TuningLevel, "snow1a_mis3") != 0)
            id = 0xaa1;
        break;
    case 0xa94:
        if (strstr(TuningLevel, "uw_") != nullptr)
            id = 0xaa1;
        break;
    case 0xa98:
    case 0xa99:
        if (Crt_stricmp(TuningLevel, "paris_mis01") == 0 && type != kInputDriving)
            id = 0xaa1;
        break;
    case 0xa9f:
        id = 0xaa1;
        break;
    case 0xaa3:
        if (Crt_stricmp(TuningLevel, "paris_mis01") == 0 && type != kInputDriving)
            id = config == 6 || config == 7 ? 0x6ad : 0xaa1;
        break;
    }
    return id;
}

// FUNC_AT(0x00050720)
void InputConfigManager::SetConfig(int type, int config) {
    currentType = type;
    if (config != -1)
        currentConfig[type] = config;
    StoreChoice(currentType, currentConfig[type]);
    LoadMappings(this);
}
