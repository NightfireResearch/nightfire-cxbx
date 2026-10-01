#include "InputConfigManager.hpp"

#include <string.h>

#include "DefText.hpp"
#include "IOModule.hpp"
#include "UFileLoader.h"
#include "UMemory.hpp"

// The manager and its construction guard (bit 0), Get's function-local static.
#define gInputConfigManager ((InputConfigManager *)0x001e455c)
#define gInputConfigManagerGuard (*(uint32_t *)0x001e458c)

// Fields of the launch page (0x00243b90) the action engine hands over and reads back: the chosen Driving and POV
// configurations and the inversion option. PS2 has them at the same offsets from 0x00100000.
#define pageDrivingConfig (*(int *)0x002445a4)
#define pagePOVConfig (*(int *)0x0024451c)
#define pageInverted (*(int *)0x002445b4)
// The pad port input comes from (Ghidra: glbIFeedback; always 0).
#define glbActivePort (*(int *)0x00244520)
// The mission being played, e.g. "paris_mis01".
#define MissionName ((const char *)0x00244084)

// The names of the pause menu's legend slots (ButtonA..ButtonWhite, DESCRIPTION, TITLE) and their slot numbers.
#define kFrontEndSlotNames ((StringToNumberEntry *)0x001b6f58)
#define kStringToNumberTag ((const char *)0x0018d93c)   // "StringToNumber"

// UFileLoader::FileFree (0x00117110, __cdecl; a jump to MEM_free).
typedef bool (__cdecl *FileFreeFn)(void *block);
#define FileFree ((FileFreeFn)0x00117110)

static const char kControlDir[] = "data/Control/";

// The pause menu's LOCALE ids that GetLocaleID replaces.
enum : int {
    LOCALE_UNUSED_BUTTON = 0xaa1,   // what the files themselves give an unused button
    LOCALE_1709 = 0x6ad,
};

// AUTOINJECT
InputConfigManager* InputConfigManager::Get() {
    InputConfigManager *m = gInputConfigManager;
    if (!(gInputConfigManagerGuard & 1)) {
        gInputConfigManagerGuard |= 1;
        m->initialised = false;
        m->frontEndNameLookup = nullptr;
        m->frontEndDefText = nullptr;
        m->unusedFileData = nullptr;
        m->currentConfigType = CONFIG_CURRENT;
        for (int i = 0; i < 2; i++) {
            m->currentConfig[i] = 0;
            m->numConfigs[i] = 0;
            m->configInfos[i] = nullptr;
        }
        // inverted is left alone, as the original leaves it (zero in bss)
    }
    return m;
}

// The live mapping: FrontEnd.def, then the configuration's own entries (PS2 SetupInputMappings, inlined in both
// callers).
static void SetupInputMappings(InputConfigManager *m, char *text) {
    InputToAction *mapping = IOModule::GetIOModule()->inputActionMappings[glbActivePort];
    mapping->Reset();
    mapping->LoadParseDefFile(m->frontEndDefText);
    if (text != nullptr)
        mapping->LoadParseDefFile(text);
}

// The text of configuration `index` of `type`, or null when there is none. The originals index without checking;
// on the shipped data every index they use is in range.
static char *ConfigText(InputConfigManager *m, int type, int index) {
    if (type < 0 || type > 1 || index < 0 || index >= m->numConfigs[type] || m->configInfos[type] == nullptr)
        return nullptr;
    return m->configInfos[type][index].defText;
}

// Master.def, then FrontEnd.def and every configuration it lists (kept until Shutdown), the legend ids from each
// configuration's FRONTEND section, the current choices from the launch page, and the Driving mapping made live.
// The original also leaves out-of-range page choices alone when they are negative, and crashes on a missing
// file; here a negative choice resets to 0 like one too large, and a missing file gives no configurations.
// AUTOINJECT
void InputConfigManager::InitAndPreload() {
    char *master = (char *)UFileLoader::FileLoad((char *)"data/Control/Master.def", 0);
    if (master != nullptr) {
        ParseMasterConfigFile(master);
        FileFree(master);
    }
    frontEndDefText = (char *)UFileLoader::FileLoad((char *)"data/Control/FrontEnd.def", 0);

    for (int type = 0; type < 2; type++)
        for (int i = 0; i < numConfigs[type]; i++) {
            ConfigInfo *info = &configInfos[type][i];
            char path[100];
            strcpy(path, kControlDir);
            strcat(path, info->fileName);
            info->defText = (char *)UFileLoader::FileLoad(path, 0);
        }

    void *memory = UMemory::FastAlloc(sizeof(StringToNumber), kStringToNumberTag);
    frontEndNameLookup = memory ? ((StringToNumber *)memory)->Construct(kFrontEndSlotNames) : nullptr;
    for (int type = 0; type < 2; type++)
        for (int i = 0; i < numConfigs[type]; i++)
            ParseDefFileForFrontEnd(&configInfos[type][i], configInfos[type][i].defText);
    if (StringToNumber *lookup = frontEndNameLookup) {
        lookup->Destruct();
        UMemory::FastFree(lookup, sizeof(StringToNumber));
    }
    frontEndNameLookup = nullptr;

    initialised = true;
    currentConfigType = CONFIG_DRIVING;
    currentConfig[0] = pageDrivingConfig;
    currentConfig[1] = pagePOVConfig;
    inverted = pageInverted != 0;
    for (int type = 0; type < 2; type++)
        if (currentConfig[type] >= numConfigs[type] || currentConfig[type] < 0) {
            currentConfig[type] = 0;
            if (type == 0)
                pageDrivingConfig = 0;
            else
                pagePOVConfig = 0;
        }

    if (frontEndDefText != nullptr)
        SetupInputMappings(this, ConfigText(this, currentConfigType, currentConfig[currentConfigType]));
}

// Lines of Master.def, from the first: "Driving N" or "POV N" (the keyword in any case) starts that type's list
// of N configurations; any other line's first token is the next configuration's file name; END (any case) ends
// the file. The original writes past the arrays for a name before any header or more names than N, and crashes
// on a blank line or a missing N; here those names are dropped, a blank line ends the file and N defaults to 0.
// AUTOINJECT
void InputConfigManager::ParseMasterConfigFile(char *text) {
    int count[2] = {0, 0};
    int section = 2;   // none yet
    DefLine line;
    for (const char *p = text; p != nullptr; p = DefNextLine(p)) {
        const char *token = line.First(p);
        if (token == nullptr || DefStricmp(token, "END") == 0)
            return;
        int type = DefStricmp(token, "Driving") == 0 ? CONFIG_DRIVING : DefStricmp(token, "POV") == 0 ? CONFIG_POV : -1;
        if (type >= 0) {
            const char *countToken = line.Next();
            int n = countToken ? (int)DefAtol(countToken) : 0;
            numConfigs[type] = n;
            ConfigInfo *infos = (ConfigInfo *)GameArrayNew(n * sizeof(ConfigInfo));   // no constructor, no cookie
            if (infos != nullptr)
                for (int i = 0; i < n; i++) {
                    memset(infos[i].frontEndLocaleIDs, 0, sizeof(infos[i].frontEndLocaleIDs));
                    infos[i].fileName[0] = '\0';
                    infos[i].defText = nullptr;
                }
            configInfos[type] = infos;
            section = type;   // the count of names so far is kept, as in the original
        } else if (section < 2 && count[section] < numConfigs[section] && configInfos[section] != nullptr) {
            ConfigInfo *info = &configInfos[section][count[section]];
            strncpy(info->fileName, token, sizeof(info->fileName) - 1);
            info->fileName[sizeof(info->fileName) - 1] = '\0';
            count[section]++;
        }
    }
}

// A configuration's lines after the first: the mappings, which are skipped, up to FRONTEND (any case); then
// "SLOT <id>" lines, each setting the LOCALE id of that legend slot, up to END (any case). END ends the file even
// before FRONTEND. The original writes over the record's defText for an unknown slot name and crashes on a
// blank line or a missing id; here those lines end the file or are skipped.
// AUTOINJECT
void InputConfigManager::ParseDefFileForFrontEnd(ConfigInfo *info, char *text) {
    bool beforeFrontEnd = true;
    DefLine line;
    if (text == nullptr)
        return;
    for (const char *p = DefNextLine(text); p != nullptr; p = DefNextLine(p)) {
        const char *token = line.First(p);
        if (token == nullptr || DefStricmp(token, "END") == 0)
            return;
        if (beforeFrontEnd) {
            if (DefStricmp(token, "FRONTEND") == 0)
                beforeFrontEnd = false;
            continue;
        }
        int slot = frontEndNameLookup->ConvertStringToNumber((char *)token);
        const char *value = line.Next();
        if (slot >= 0 && slot < 14 && value != nullptr)
            info->frontEndLocaleIDs[slot] = (int)DefAtol(value);
    }
}

// Always re-parses, even when nothing changed, and records the choice in the launch page for Driving and POV.
// The caller releases the buttons first (SMissionManager::SetAutoDrive) where that matters.
// AUTOINJECT
void InputConfigManager::SetConfig(int type, int index) {
    currentConfigType = type;
    if (index != -1)
        currentConfig[type] = index;
    if (currentConfigType == CONFIG_DRIVING)
        pageDrivingConfig = currentConfig[type];
    else if (currentConfigType == CONFIG_POV)
        pagePOVConfig = currentConfig[type];
    SetupInputMappings(this, ConfigText(this, currentConfigType, currentConfig[type]));
}

// numConfigs and currentConfig are kept, as in the original.
// AUTOINJECT
void InputConfigManager::Shutdown() {
    for (int type = 0; type < 2; type++)
        for (int i = 0; i < numConfigs[type] && configInfos[type] != nullptr; i++)
            if (configInfos[type][i].defText != nullptr) {
                FileFree(configInfos[type][i].defText);
                configInfos[type][i].defText = nullptr;
            }
    if (frontEndDefText != nullptr) {
        FileFree(frontEndDefText);
        frontEndDefText = nullptr;
    }
    if (unusedFileData != nullptr) {
        FileFree(unusedFileData);
        unusedFileData = nullptr;
    }
    for (int type = 0; type < 2; type++) {
        GameArrayDelete(configInfos[type]);
        configInfos[type] = nullptr;
    }
    initialised = false;
}

// AUTOINJECT
void InputConfigManager::SetInverted(bool on) {
    inverted = on;
    pageInverted = on ? 1 : 0;
}

// AUTOINJECT
bool InputConfigManager::IsInverted() {
    return inverted;
}

// AUTOINJECT
int InputConfigManager::GetNumConfigs(int type) {
    return numConfigs[type];
}

// AUTOINJECT
int InputConfigManager::GetCurrentConfig(int type) {
    if (type == CONFIG_CURRENT)
        type = currentConfigType;
    return currentConfig[type];
}

// AUTOINJECT
int InputConfigManager::GetLocaleID(int type, int configIndex, int slot) {
    int id = configInfos[type][configIndex].frontEndLocaleIDs[slot];
    switch (id) {
    case 0x590:   // only snow1a_mis3 shows it
        return DefStricmp(MissionName, "snow1a_mis3") != 0 ? LOCALE_UNUSED_BUTTON : id;
    case 0xa94:   // hidden underwater
        return strstr(MissionName, "uw_") != nullptr ? LOCALE_UNUSED_BUTTON : id;
    case 0xa98:
    case 0xa99:   // hidden in Paris's POV section
        if (DefStricmp(MissionName, "paris_mis01") != 0 || type == CONFIG_DRIVING)
            return id;
        return LOCALE_UNUSED_BUTTON;
    case 0xa9f:
        return LOCALE_UNUSED_BUTTON;
    case 0xaa3:   // in Paris's POV section, 1709 for povcfg6 and 7 and hidden for the rest
        if (DefStricmp(MissionName, "paris_mis01") != 0 || type == CONFIG_DRIVING)
            return id;
        return (configIndex == 6 || configIndex == 7) ? LOCALE_1709 : LOCALE_UNUSED_BUTTON;
    default:
        return id;
    }
}
