#ifndef DRIVING_PLATFORM_CRUNTIMESTD_H_
#define DRIVING_PLATFORM_CRUNTIMESTD_H_

#include <stddef.h>
#include <stdint.h>

#include "../data/StdStreams.h"

// ---------------------------------------------------------------------------------------------------------------
// The C++ half of the game's runtime: the non-template parts of the Dinkumware library (MSVC 7) the string
// streams use, and the compiler's helpers for throwing and for arrays of objects. The streams' templates
// (src/driving/data/StdStreams.cpp) reach them: the library's locks and mutexes, the global locale and the facet
// list, _Locinfo, ctype's tables, ios_base, strstreambuf's own members, istrstream's and ostrstream's
// destructors, _Stod, std::exception and bad_cast. Members of the classes in StdStreams.h are defined in
// CRuntimeStd.cpp as well; the classes only the runtime uses are here.
//
// Nothing in the game catches an exception: the original's only handlers are the library's catch (...) blocks,
// which clean up and rethrow, so a throw ends the program.
// ---------------------------------------------------------------------------------------------------------------

namespace GameStd {

// The Xbox kernel's critical section (std::_Mutex's, declared in StdStreams.h).
struct CriticalSection {
    uint8_t bytes[0x1c];
};

// std::_Init_locks: the static object whose first construction sets up the library's four locks.
struct InitLocks {
    uint8_t unused;

    InitLocks* Construct();                                                // 0x00130c99
    void Destruct();                                                       // 0x00130cc7
};

// std::_Init_atexit: the static object whose destruction runs what _Atexit collected.
struct InitAtExit {
    uint8_t unused;

    void Destruct();                                                       // 0x00131cd0
};

// std::locale::_Locimp: a locale's facets by id, itself a facet.
struct Locimp : Facet {
    Facet **facets;        // +0x08 _Facetvec
    uint32_t facetCount;   // +0x0c
    int categories;        // +0x10 _Catmask
    bool transparent;      // +0x14 _Xparent: ask the global locale for a facet it lacks
    uint8_t pad15[3];
    String name;           // +0x18

    Locimp* Construct(bool isTransparent);
};
static_assert(sizeof(Locimp) == 0x34, "Locimp");

// std::_Fac_node: an entry of the list of facets released at exit.
struct FacetNode {
    FacetNode *next;
    Facet *facet;

    void Destruct();
};

struct BadCast : Exception {
    BadCast* Construct(const char *message);                               // 0x001326ae
};

// std::runtime_error, and ios_base::failure over it (another vtable).
struct RuntimeError : Exception {
    String message;        // +0x0c

    RuntimeError* Construct(const String *text);
};
static_assert(sizeof(RuntimeError) == 0x28, "RuntimeError");

// ios_base::_Stdstr and _Stdopens as the game lays them out: the eight standard streams and how often each was
// opened, each with the slot after it that a ninth standard stream reaches (AddStandard does not stop at eight).
struct StandardStreamTable {
    IosBase *streams[9];   // +0x00
    uint32_t unknown24;
    int8_t opens[9];       // +0x28
    uint8_t pad31[3];
};
static_assert(sizeof(StandardStreamTable) == 0x34, "StandardStreamTable");

typedef void (*AtExitFn)(void);

// The library's state (CRuntimeStd.cpp), for the shadow test.
extern volatile long InitLocksCount;
extern FacetNode *FacetList;
extern Locimp *GlobalLocale;
extern Locimp *ClassicImpl;
extern Locale ClassicLocale;
extern bool GlobalTidyRegistered;
extern StandardStreamTable StandardStreams;
extern uint32_t AtExitCount;
extern AtExitFn AtExitFunctions[10];

// The C locale's ctype table (a copy from the C runtime's heap) and code page.
CtypeVec Getctype();                                                       // 0x00130c3c _Getctype
CvtVec Getcvt();                                                           // 0x00131c31 _Getcvt
int Tolower(int c, const CtypeVec *ctype);                                 // 0x00130c00 _Tolower
int Toupper(int c, const CtypeVec *ctype);                                 // 0x0013143d _Toupper

bool UncaughtException();                                                  // 0x00131c2c uncaught_exception

// strtod scaled by a power of ten (the digits num_get dropped). The value is left unrounded, as on the x87.
double Stod(const char *text, char **end, long powerOfTen);                // 0x00131c45 _Stod
double Stof(const char *text, char **end, long powerOfTen);                // 0x00131c79 _Stof

} // namespace GameStd

namespace Crt {

struct ThrowInfo;

// A method of the array's class (this in ECX, nothing else).
typedef void (__thiscall *ElementFn)(void *element);

// throw: the C++ exception (0xe06d7363) raised with the object and its type's throw information; null for both
// rethrows.
void __stdcall CxxThrowException(void *object, const ThrowInfo *info);     // 0x001325ad _CxxThrowException

// `eh vector constructor iterator' / `eh vector destructor iterator': each element of an array, the destructor in
// reverse.
void __stdcall EhVectorConstructor(void *array, size_t size, int count, ElementFn constructor,
                                   ElementFn destructor);                  // 0x0013326e
void __stdcall EhVectorDestructor(void *array, size_t size, int count, ElementFn destructor);   // 0x0013332e

} // namespace Crt

#endif // DRIVING_PLATFORM_CRUNTIMESTD_H_
