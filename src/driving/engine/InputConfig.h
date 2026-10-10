#ifndef DRIVING_ENGINE_INPUTCONFIG_H_
#define DRIVING_ENGINE_INPUTCONFIG_H_

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// The control configurations: which action each control of a device sends, and when.
//
// data/Control/Master.def names the configurations of two types, "Driving" and "POV", each a .def file in the same
// directory. A .def file is a title line, then one mapping per line - action name, control name, update method
// letter (IOModule.hpp's UPDATE_*; U when left out) and threshold (0.5 for > and <, else 0, when left out) - up
// to a FRONTEND line; after it the front end's labels, one locale string id per slot (ButtonA .. TITLE), up to END.
//
// InputConfigManager::InitAndPreload, at startup, reads every configuration and their labels, then loads the
// active pad's InputToAction from FrontEnd.def (the menus' mappings) and the current configuration's file;
// SetConfig loads another. IOModule::SendGameMessages walks the InputToAction for each control's changes.
// See docs/driving/input.md.
// ---------------------------------------------------------------------------------------------------------------

class InputDevice;
struct StringToNumber;
struct StringToNumberEntry;

// InputTableEntry::updateMethod - how a mapping entry decides to send its action (the table at 0x001b6fd0, the
// letters the .def files use).
enum : int {
    UPDATE_ON_CHANGE    = 0,   // U: whenever the control's value changes
    UPDATE_DOWN         = 1,   // +: on the down transition
    UPDATE_UP           = 2,   // -: on the up transition
    UPDATE_ABOVE        = 3,   // >: on crossing the entry's threshold upwards
    UPDATE_BELOW        = 4,   // <: on crossing it downwards
    UPDATE_CENTRED      = 5,   // =: on coming to rest within 0.2 of the middle
    UPDATE_IF_INVERTED  = 6,   // I: with the controls inverted
    UPDATE_NOT_INVERTED = 7,   // N: with them not inverted
    UPDATE_REPEAT_DOWN  = 8,   // R: on the down transition, then auto-repeating while down
    UPDATE_REPEAT_UP    = 9,   // M: the same on the "up" side (for an axis, below -0.5)
};

// One mapping (12 bytes)
struct InputTableEntry {
    int32_t updateMethod;   // +0x00 UPDATE_*
    float threshold;        // +0x04
    int32_t action;         // +0x08 the action id (ActionQueue.hpp)
};
static_assert(sizeof(InputTableEntry) == 12, "a mapping entry is 12 bytes");

// A control's mappings (Ghidra: InputTable, 0x3c bytes): four in place, then a pool block grown four at a time
struct InputTable {
    int32_t count;                      // +0x00
    int32_t capacity;                   // +0x04
    InputTableEntry *entries;           // +0x08 inlineEntries, or a block from the pools
    InputTableEntry inlineEntries[4];   // +0x0c

    // The constructor (0x000507a0)
    InputTable* Construct();
    // The destructor (0x000507c0): frees a grown block
    void Destruct();
    // Room for four more (0x000507e0)
    void GrowArrayForOneElement();
};
static_assert(sizeof(InputTable) == 0x3c, "an input table is 0x3c bytes");

// A device's mappings (Ghidra: InputToAction, 0x10 bytes): one InputTable per control, and a lookup of the
// controls by name
struct InputToAction {
    int32_t numTables;                  // +0x00 the device's controls
    InputTable *tables;                 // +0x04 from new[]
    StringToNumber *scalarLookup;       // +0x08 over scalarNames
    StringToNumberEntry *scalarNames;   // +0x0c each control's index and name, then a null entry

    // The constructor (0x00050860)
    InputToAction* Construct(InputDevice *device);
    // The destructor (0x000509c0)
    void Destruct();
    // Every table emptied (0x00050990)
    void Reset();
    // Adds the mappings of a .def file's text, up to its FRONTEND or END line (0x00050a20)
    void LoadParseDefFile(char *text);
};
static_assert(sizeof(InputToAction) == 0x10, "InputToAction is 16 bytes");

// The two types of configuration, as InputConfigManager's arrays index them
enum : int32_t {
    kInputDriving = 0,   // Master.def's "Driving" section
    kInputPOV     = 1,   // its "POV" section
    kInputNoType  = 2,   // before either; GetCurrentConfig takes it for the current type
};

// One configuration (0x5c bytes)
struct InputConfig {
    char name[0x20];        // +0x00 its .def file, in data/Control/
    char *definition;       // +0x20 the file's text
    int32_t localeIDs[14];  // +0x24 the front end's label for each slot
};
static_assert(sizeof(InputConfig) == 0x5c, "a configuration is 0x5c bytes");

// The configurations (Ghidra: InputConfigManager, 0x30 bytes; a function-local static at 0x001e455c)
class InputConfigManager {
public:
    bool initialised;               // +0x00
    int32_t currentType;            // +0x04 kInputDriving or kInputPOV
    int32_t currentConfig[2];       // +0x08 per type
    int32_t numConfigs[2];          // +0x10
    InputConfig *configs[2];        // +0x18 from new[]
    bool inverted;                  // +0x20
    StringToNumber *frontEndSlots;  // +0x24 the slot names, while InitAndPreload reads the labels
    char *frontEndDefinition;       // +0x28 FrontEnd.def's text
    char *unknown2C;                // +0x2c freed by Shutdown; nothing here sets it

    // The one manager, emptied the first time (0x00050270).
    static InputConfigManager *Get();

    // Reads Master.def, every configuration and FrontEnd.def; takes the choices from the launch page and loads the
    // active pad's mappings (0x000504d0).
    void InitAndPreload();
    void ParseMasterConfigFile(char *text);
    void ParseDefFileForFrontEnd(InputConfig *config, char *text);
    // Frees the files and the configurations (0x00050080).
    void Shutdown();

    // Also kept in the launch page (0x00050140).
    void SetInverted(bool inverted);
    bool IsInverted();
    int GetNumConfigs(int type);
    int GetCurrentConfig(int type);
    // A configuration's label for a slot, with some swapped for others on some tracks (0x000501a0).
    int GetLocaleID(int type, int config, int slot);
    // Makes `type` current, with configuration `config` (-1 keeps its current one), and reloads the active pad's
    // mappings (0x00050720).
    void SetConfig(int type, int config);
};
static_assert(sizeof(InputConfigManager) == 0x30, "the configuration manager is 0x30 bytes");
static_assert(offsetof(InputConfigManager, configs) == 0x18 && offsetof(InputConfigManager, inverted) == 0x20 &&
                  offsetof(InputConfigManager, unknown2C) == 0x2c,
              "configuration manager layout");

// ---- the text helpers

// Moves *text past the next line feed, or to null at the last line (0x00050970).
void AdvanceFilePtr(char **text);
// An action's id by name (ActionQueue.hpp's ids, the table at 0x001b6ba0); 0x75, one past the last, for an
// unknown name (0x0004f190).
int getActionID(char *name);

// directory + name, then "." + extension unless the extension is empty, into `buffer`, which is returned. The
// buffer comes in ECX and the callee pops the rest: __fastcall, with EDX unused (Ghidra: strcat, 0x00051e90).
char* __fastcall BuildFileName(char *buffer, int unused, const char *directory, const char *name,
                               const char *extension);
// directory + subdirectory + "/" + name, then "." + extension unless it is empty (0x00051f30; the name is ours).
char* __fastcall BuildPath(char *buffer, int unused, const char *directory, const char *subdirectory,
                           const char *name, const char *extension);

#endif // DRIVING_ENGINE_INPUTCONFIG_H_
