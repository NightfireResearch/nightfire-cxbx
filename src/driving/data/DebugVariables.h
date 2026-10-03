#ifndef DRIVING_DATA_DEBUGVARIABLES_H_
#define DRIVING_DATA_DEBUGVARIABLES_H_

// The debug variables: how the renderer and the physics get their tuning values.
//
// A system's constructor (the fog, the lights, the glare, the water, the bond car's handling, the rigid bodies ...)
// opens its tuning database - DTuningDBMgr::LoadDatabase("Render:Fog", level), which reads
// data\tuning\Render\Fog\<level>.tun, or default.tun beside it (Tuning.h) - and then names each of its variables:
// dbattrib_float("fog START", &fogStart, min, max, stride, step, names). On the PS2 each call also put the variable
// in the debug menu, as a DebugVariable<T> with its range and step, for editing live; on the Xbox the menu is gone
// and a dbattrib_ call only looks the name up in the open file ("name value" lines, found with OptionParser) and
// parses the value into the variable - through the C++ library's istrstream (StdStreams.h), so its parse is the
// library's.
//
// An array of settings is named through an index variable: dbindex("Light", &current, 0, 3, names) makes a
// DebugVariable<unsigned int> - the only DebugVariable the Xbox build still constructs - and until dbendindex every
// dbattrib_ call reads one value per index, "name{index}" (the index printed as its name from `names`, or as a
// number through ostrstream), into data + index * stride. The index variables are never freed.
//
// The rest of DebugVariable<unsigned int>'s vtable (Increase, Decrease, SetToMin ...) and DebugData's are the menu's:
// ported, but nothing on the Xbox calls them.

#include <stddef.h>
#include <stdint.h>

// A debug-menu item with a value (Ghidra: DebugData): the variable's name, and where its value is - indexed by
// another variable when `indexer` is set.
struct DebugData {
    const void *const *vtable;   // +0x00
    const char *name;            // +0x04
    uint32_t nameLength;         // +0x08
    uint32_t unknown0c;          // +0x0c 0
    DebugData *indexer;          // +0x10 the index variable this one is an array over, or null
    uint32_t stride;             // +0x14 bytes from one index's value to the next
    void *data;                  // +0x18

    // The value's address for the indexer's current index: base + stride * index, recursively (0x00038160).
    void* GetDeindexedPtr(void *base);
    // The same for the variable's own data (0x00037b00, the inlined form of GetDeindexedPtr(data)).
    uint32_t* GetValuePtr();
    // DebugData's own AsString (0x000380e0) and Axis2AsString (0x00038130), vtable slots 9 and 10: empty.
    char* AsString(char *out);
    char* Axis2AsString();
};
static_assert(sizeof(DebugData) == 0x1c, "DebugData");

// DebugVariable<unsigned int> (0x2c bytes): an index variable, with its range, step and value names.
struct DebugUIntVariable : DebugData {
    uint32_t maximum;              // +0x1c
    uint32_t minimum;              // +0x20
    float step;                    // +0x24 the fraction of the range one press moves by; -1 from dbindex
    const char *const *names;      // +0x28 a name per value, or null for the number

    int Debounce();                                // 0x00037b50 slot 11: step <= 0
    void Increase(float scale);                    // 0x00037b70 slot 1
    void Decrease(float scale);                    // 0x00037cb0 slot 2
    void SetToMin();                               // 0x00038490 slot 7
    void SetToMax();                               // 0x000384c0 slot 8
    char* AsString(char *out);                     // 0x000396d0 slot 9
    void SetFromString(const char *text);          // 0x0003a3f0 slot 20
};
static_assert(sizeof(DebugUIntVariable) == 0x2c, "DebugUIntVariable");

// Starts an index: the dbattrib_ calls up to dbendindex read one value per index (0x00038190).
void dbindex(const char *name, uint32_t *data, uint32_t minimum, uint32_t maximum, const char *const *names);
// Ends it (0x00037ad0).
void dbendindex();

// The tuning reads, one instantiation per type. The minimum, maximum, step and names were the menu's; the Xbox
// ignores them. (Ghidra's names: dbattrib_f reads a char, dbattrib_s8 an int, dbattrib_u8 an unsigned int.)
void dbattrib_f(const char *name, char *data, int minimum, int maximum, uint32_t stride, float step,
                const char *const *names);                                                        // 0x000395d0
void dbattrib_s8(const char *name, int *data, int minimum, int maximum, uint32_t stride, float step,
                 const char *const *names);                                                       // 0x00039ff0
void dbattrib_u8(const char *name, unsigned *data, unsigned minimum, unsigned maximum, uint32_t stride, float step,
                 const char *const *names);                                                       // 0x0003a0f0
void dbattrib_float(const char *name, float *data, float minimum, float maximum, uint32_t stride, float step,
                    const char *const *names);                                                    // 0x0003a1f0
void dbattrib_bool(const char *name, bool *data, int minimum, int maximum, uint32_t stride, float step,
                   const char *const *names);                                                     // 0x0003a2f0
// Four floats, "r,g,b,a" (0x0003d470), and a colour, "a,r,g,b" packed as 0xAARRGGBB (0x0003d530). The third
// argument (a flag byte's address) is not used.
void dbattrib_floatrgb(const char *name, float *data, uint8_t *unused, uint32_t stride);
void dbattrib_argb(const char *name, uint32_t *data, uint8_t *unused, uint32_t stride);

// The value parsers dbattrib_ uses, the game's template over the type: istrstream(text) >> *value, when there is a
// text.
void StringToChar(const char *text, char *value);       // 0x00038f10
void StringToInt(const char *text, int *value);         // 0x00039db0
void StringToUInt(const char *text, unsigned *value);   // 0x00039e40
void StringToFloat(const char *text, float *value);     // 0x00039ed0
void StringToBool(const char *text, bool *value);       // 0x00039f60

#endif // DRIVING_DATA_DEBUGVARIABLES_H_
