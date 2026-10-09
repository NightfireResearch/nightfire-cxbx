#ifndef DRIVING_DATA_RCARPFILE_H_
#define DRIVING_DATA_RCARPFILE_H_

// Loading a CARP file and resolving it (see Carp.h for the format and what resolving does).
//
// RCARPFileLoader is the "CARP" namespace (a UCarpNamespace registered in the global symbol table); the world,
// scene objects and weapons each make one, load their file through it (LoadCARPFile), and the world resolves
// its file afterwards (ResolveCARPFile). RCARPFile::Resolve, with the file's directory:
//   1. adds the file's groups to the "CARP" namespace;
//   2. opens each texture file its 'sn' records name ("TEXn" namespaces, n the record's index) and registers
//      their shapes with EAGL;
//   3. builds the EAGL loader for the file's model object ('dFLE', with 'rFLE' as its second part) and adds it
//      as the "EAGL" namespace;
//   4. resolves the file's references (CARP::ResolveSymbolicReferences) and then the loader's;
//   5. shrinks the file's block by the sections no longer needed after resolving ('Sect'), and takes the
//      temporary namespaces out again.
//
// Names the symbol table cannot answer go to the registered callbacks (RegisterCallback): up to 16 functions
// asked in turn, among them RegisterSymbols, which maps the "GAME::" and "EAGL::" names the models use to the
// renderer's live data - reflection maps, lighting properties, switch states, UV animation, the fog.

#include "SymbolTable.h"
#include "UData.h"

#include <stdint.h>

class DynamicLoader;
class RTextureContext;

constexpr int kCarpTextureFiles = 10;   // TEX0..TEX9

struct RCARPFile {
    const char *name;                      // the caller's label, "Default" without one
    void *data;                            // the file as loaded
    UGroup *root;                          // ... deserialised
    DynamicLoader *loader;                 // the model object, while the file is loaded
    RTextureContext *textures[kCarpTextureFiles];

    // Loads directory + file (0x0007bf00), resolving it at once if `resolve`; false if the file is missing.
    bool Load(const char *directory, const char *file, UCarpNamespace *carp, bool resolve);
    // Resolves the file against the global symbol table (0x0007b8e0); `directory` is where its texture files are.
    void Resolve(const char *directory, UCarpNamespace *carp);
    // The destructor (0x0007bff0): the texture contexts killed, the loader destroyed, the data freed.
    void Destruct();
    // The root group (FUN_0001aa80, folded with other classes' getters)
    UGroup* GetRoot();

    // Loads the shared EAGL material objects (eaglrm.o, bondrm.o) at start-up and leaves them loaded (0x0007bc90,
    // and the linker's thunk to it at 0x0008baa0).
    static void LoadEAGLMaterials();
    static void LoadEAGLMaterialsThunk();
    // The registered callback for "GAME::" and "EAGL::" names (0x0007b020).
    static void *RegisterSymbols(const char *name, bool *found);
};
static_assert(sizeof(RCARPFile) == 0x38, "RCARPFile is 0x38 bytes");

class RCARPFileLoader : public UCarpNamespace {
public:
    // The constructor (0x0007c070): the namespace, made sure of the global table, registered as "CARP".
    RCARPFileLoader *Construct();
    // The destructor (0x0007aef0).
    void Destruct();
    // A new RCARPFile for directory + file (0x0007c0d0), loaded and, if `resolve`, resolved; null if missing.
    RCARPFile* LoadCARPFile(const char *directory, const char *file, const char *label, bool resolve);
    // Resolves a file loaded without (0x0007bd40).
    void ResolveCARPFile(const char *directory, RCARPFile *file);
};
static_assert(sizeof(RCARPFileLoader) == 8, "RCARPFileLoader is a UCarpNamespace");

// The one symbol table (at 0x001ebcdc), made with "CHAR" and "DATA" in it.
class GlobalSymbolTable {
public:
    static void Init();   // 0x0007bd60
    static void Kill();   // 0x0007af50
};

// What the symbol table cannot answer, asked of up to 16 functions (the table at 0x001ebce8).
typedef void *(*SymbolCallback)(const char *name, bool *found);

class RegisterCallback {
public:
    static void Add(SymbolCallback callback);                 // 0x0007af90: into the first free slot, once
    static void Remove(SymbolCallback callback);              // 0x0007afc0
    static void *Resolve(const char *name, bool *found);      // 0x0007afe0: the first callback to answer
    static void AddMajorCallbacks();                          // 0x0007be40: the game's three
};

// The resolver EAGL's loaders are given (0x0007b8a0): the symbol table, then the callbacks.
void *ResolveEAGLReferences(const char *name, bool *found);

#endif // DRIVING_DATA_RCARPFILE_H_
