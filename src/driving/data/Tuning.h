#ifndef DRIVING_DATA_TUNING_H_
#define DRIVING_DATA_TUNING_H_

// The tuning databases: text files of "name value" lines on the disc, under data\tuning\, one folder per database
// and one file per level (data\tuning\Render\Fog\<level>.tun), with default.tun in the folder for the levels
// without one. DTuningDBMgr, a singleton made at start-up, holds the one database open at a time: a system opens
// its database (LoadDatabase), names its variables with dbattrib_ (DebugVariables.h), and closes it
// (CloseCurrent). A database name's ':' become folder separators ("Render:Fog" is data\tuning\Render\Fog).
//
// The file is loaded whole and searched for each name with OptionParser (the key at the start of a line). The
// mode for reading it as a stream (a TextFile) is never chosen on the Xbox.

#include <stddef.h>
#include <stdint.h>

// The file reader the Xbox never makes (mode 1): a file system slot, closed with the object if it was opened.
struct TextFile {
    int slot;              // +0x00
    uint32_t unknown04;
    uint8_t open;          // +0x08
    uint8_t pad09[3];

    void Destruct();       // 0x0003d9a0
};

// A tuning file (0x110 bytes): its path, and the file loaded whole.
struct DTuningFile {
    enum Mode : int {
        kLoaded = 0,       // read whole into reader
        kTextFile = 1,     // read through textFile (not on the Xbox)
        kNotOpen = 2,
    };

    // The loaded text, searched by name.
    struct Reader {
        const char *text;

        // The value of `name` (its spaces removed), copied into a static buffer, or null (0x0003d7f0).
        char* FindItem(const char *name);
        // The same for "name{index}" (0x0003d870).
        char* FindIndexedItem(const char *name, const char *index);
    };

    char unknown000;       // +0x000 0
    char path[0x103];      // +0x001 the file chosen, or "" without a level
    TextFile *textFile;    // +0x104
    Reader *reader;        // +0x108
    int mode;              // +0x10c

    // Picks the file for database `name` and `level` (0x0003d6e0): <level>.tun if it exists (or without the check
    // when skipCheck), else default.tun. With retry set, the same name is tried a second time first.
    DTuningFile* Construct(const char *name, const char *level, int retry, bool skipCheck);
    void Destruct();                    // 0x0003da50
    // Loads the file (0x0003d9c0).
    Reader* OpenForRead();

    // A value as four floats, "%f,%f,%f,%f" (0x0003d910), and as a colour, "a,r,g,b" to 0xAARRGGBB (0x0003d940).
    static void ParseData(const char *text, float *values);
    static void ParseData_Colour(const char *text, uint32_t *colour);
};
static_assert(sizeof(DTuningFile) == 0x110, "DTuningFile");

// The tuning database manager (Ghidra: DTuningDBManager / DTuningDBMgr, 16 bytes), a USingleton.
struct DTuningDBMgr {
    const void *const *vtable;      // +0x00
    const char *name;               // +0x04 the open database's
    DTuningFile *file;              // +0x08
    DTuningFile::Reader *reader;    // +0x0c

    DTuningDBMgr* Construct();                                                      // 0x0003d610
    static void InitSingleton();                                                    // 0x00059630
    // Vtable slot 2, the singleton's kill: deletes the instance (0x0003d620).
    void DestroySingleton();
    void LoadDatabase(const char *databaseName, const char *level, int retry, bool skipCheck);   // 0x0003d640
    // Deletes the file object; the loaded text stays (0x0003d6c0).
    void CloseCurrent();
};
static_assert(sizeof(DTuningDBMgr) == 0x10, "DTuningDBMgr");

#define TuningDBMgr (*(DTuningDBMgr **)0x001e22e8)   // fgThis: the instance

#endif // DRIVING_DATA_TUNING_H_
