#include "PhysicsNamespace.h"

#include <stdio.h>
#include <string.h>

#include "../../helpers.h"
#include "../data/AttributeParsers.h"
#include "../data/AttributeSystem.h"
#include "../engine/UMemory.hpp"

// ---------------------------------------------------------------------------------------------------------------
// PhysicsNamespace (0x0006ed40-0x0006f070), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
// The sound fields' parser: a sound's index in a bank, the bank given by the field's count (or its next word)
#define SoundAttribParserFunc ((AttributeParseResult *(*)(AttributeParseResult *, const char *, const char *, uint32_t, uint32_t, uint32_t, uint32_t, char *))0x0006ecd0)

// ---- globals
#define PhysicsNamespaceVtable ((void **)0x0018f8fc)
#define PhysicsDataType I32_AT(0x001c3dac)          // the extension type's id; -1 until the first constructor

namespace {

// The warning beside a provisional port (code no shipped data reaches), once, the first time it runs
void PhysicsUntested(const char *what) {
    printf("[physics] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

} // namespace

#define PHYSICS_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            PhysicsUntested(what); \
        } \
    } while (0)

// FUN_0006ee10 (the name is ours)
// FUNC_AT(0x0006ee10)
void InitPhysicsData(const char *className, const char *collectionName, const char *attributeName, uint32_t type,
                     void *data) {
    PhysicsData *physics = static_cast<PhysicsData *>(data);
    if (physics == NULL)
        return;
    physics->attributes.Construct("smackable", collectionName);
    physics->mass = 100.0f;
    physics->hitPoints = 500.0f;
    physics->forceDetach = 0;
    physics->defaultSound = -1;
    physics->warningSound = -1;
    physics->impactSoundLo = -1;
    physics->impactSoundMed = -1;
    physics->impactSoundHi = -1;
    physics->scrapeSound = -1;
    physics->description = 0.0f;
}

// FUNC_AT(0x0006ed40)
void* PhysicsNamespace::NameLookup(const char *name, uint32_t *size) {
    *size = strlen(name) + 1;
    AttributeSet attributes;
    attributes.Construct("smackable", name);
    void *data = attributes.LookupStruct("PhysicsData", PhysicsDataType, NULL);
    if (data == NULL) {
        // LookupStruct makes the structure when the collection has none, so this needs a failed allocation
        PHYSICS_UNTESTED("PhysicsNamespace::NameLookup's \"Bench\" fallback");
        AttributeSet bench;
        bench.Construct("smackable", "Bench");
        data = bench.LookupStruct("PhysicsData", PhysicsDataType, NULL);
        bench.Destruct();
    }
    attributes.Destruct();
    return data;
}

// FUNC_AT(0x0006ee90)
PhysicsNamespace* PhysicsNamespace::Construct() {
    vtable = PhysicsNamespaceVtable;
    if (PhysicsDataType == -1) {
        PhysicsDataType = AttributeSystemInstance->RegisterExtensionType("smackable", "PhysicsData",
                                                                         sizeof(PhysicsData), InitPhysicsData);
        AttributeSystemInstance->RegisterExtensionField(PhysicsDataType, "MASS", Float_AttribByteOffsetParserFunc,
                                                        offsetof(PhysicsData, mass), 1, 0, 0);
        AttributeSystemInstance->RegisterExtensionField(PhysicsDataType, "HITPOINTS",
                                                        Float_AttribByteOffsetParserFunc,
                                                        offsetof(PhysicsData, hitPoints), 1, 0, 0);
        AttributeSystemInstance->RegisterExtensionField(PhysicsDataType, "DESCRIPTION",
                                                        Float_AttribByteOffsetParserFunc,
                                                        offsetof(PhysicsData, description), 1, 0, 0);
        AttributeSystemInstance->RegisterExtensionField(PhysicsDataType, "FORCE_DETACH",
                                                        Bool_AttribByteOffsetParserFunc,
                                                        offsetof(PhysicsData, forceDetach), 1, 0, 0);
        AttributeSystemInstance->RegisterExtensionField(PhysicsDataType, "MOMENT", Vector_AttribByteOffsetParserFunc,
                                                        offsetof(PhysicsData, forceDetach), 1, 0, 0);
        AttributeSystemInstance->RegisterExtensionField(PhysicsDataType, "DEFAULTSOUND", SoundAttribParserFunc,
                                                        offsetof(PhysicsData, defaultSound), 5, 0, 0);
        AttributeSystemInstance->RegisterExtensionField(PhysicsDataType, "WARNINGSOUND", SoundAttribParserFunc,
                                                        offsetof(PhysicsData, warningSound), 5, 0, 0);
        AttributeSystemInstance->RegisterExtensionField(PhysicsDataType, "IMPACTSOUNDLO", SoundAttribParserFunc,
                                                        offsetof(PhysicsData, impactSoundLo), 2, 1, 0);
        AttributeSystemInstance->RegisterExtensionField(PhysicsDataType, "IMPACTSOUNDMED", SoundAttribParserFunc,
                                                        offsetof(PhysicsData, impactSoundMed), 2, 1, 0);
        AttributeSystemInstance->RegisterExtensionField(PhysicsDataType, "IMPACTSOUNDHI", SoundAttribParserFunc,
                                                        offsetof(PhysicsData, impactSoundHi), 2, 1, 0);
        AttributeSystemInstance->RegisterExtensionField(PhysicsDataType, "SCRAPESOUND", SoundAttribParserFunc,
                                                        offsetof(PhysicsData, scrapeSound), 2, 1, 0);
    }
    return this;
}

// FUNC_AT(0x0006f050)
PhysicsNamespace* PhysicsNamespace::Delete(unsigned flags) {
    vtable = PhysicsNamespaceVtable;
    if (flags & 1)
        OperatorDelete(this);
    return this;
}
