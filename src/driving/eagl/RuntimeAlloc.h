#ifndef DRIVING_EAGL_RUNTIMEALLOC_H_
#define DRIVING_EAGL_RUNTIMEALLOC_H_

// EAGL's property-built objects: the RUNTIME_ALLOC:: constructors the loader calls for a symbol named
// "RUNTIME_ALLOC::<properties>" of class EAGL::TAR or EAGL::GeoPrimState, and the parsers behind them. No object on
// the disc has such a symbol, so the shipped game never runs any of this: a provisional port, untested. See
// RuntimeAlloc.cpp and docs/driving/eagl.md 3.1.

#include <stdint.h>

class DynamicLoader;

struct Property {           // "name=value,value,...", 0xc (EAGLInternal::Property)
    const char *name;
    int count;
    const char **values;

    bool Is(const char *wanted, int valueCount);                 // 0x000ed330
};

struct Properties {         // a property string split up in a copy of itself, 0x10
    int count;
    Property *list;         // new[]: the element count sits in the dword before it
    int length;
    char *buffer;

    Properties* Construct(const char *text);                     // 0x000edb30
    void Destruct();        // inline in the original (0x000edde0 is only an unwind funclet's)
};

void* RuntimeAllocTARConstructor(const char *properties, DynamicLoader *loader, void **context, char *destroy);
void RuntimeAllocTARDestructor(void *tars, int count);
void* RuntimeAllocGeoPrimStateConstructor(const char *properties, DynamicLoader *loader, void **context,
                                          char *destroy);
void RuntimeAllocGeoPrimStateDestructor(void *state, int unused);

void EAGL_SetTarApi(uint8_t *tar, Property *property);           // 0x000ed640 (invented)
uint32_t EAGL_ParseTarApi(const char *text);                     // 0x000ed390 (invented)
uint32_t EAGL_ParseTarValue(const char *text);                   // 0x000ed700 (invented)
void EAGL_SetGeoPrimState(uint8_t *state, Property *property);   // 0x000f0c50 (invented)
uint32_t EAGL_ParseGeoPrimState(const char *text);               // 0x000ef7a0 (invented)
uint32_t EAGL_ParseXboxGeoPrimState(const char *text);           // 0x000f04d0 (invented)
bool EAGL_ParseTrue(const char *text);                           // 0x000f0450 (invented)
uint32_t EAGL_ParseHex(const char *text);                        // 0x000f04b0 (invented)

#endif // DRIVING_EAGL_RUNTIMEALLOC_H_
