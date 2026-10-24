#ifndef DRIVING_GAME_BONDCARBASICS_H_
#define DRIVING_GAME_BONDCARBASICS_H_

#include <stdint.h>

#include "BondCar.h"

// ---------------------------------------------------------------------------------------------------------------
// PBondCar's small methods (0x00065ba0-0x00066060): DebugObject, the destructor and the accessors - most of them
// virtual, declared in BondCar.h - and the colour helper the constructor and ChangeCarType share. See
// BondCarBasics.cpp.
// ---------------------------------------------------------------------------------------------------------------

// The colour variation `colour` picks among the car's RENDER_NUMCOLORS (at least one)
uint32_t GetCarColourVariation(const char *carType, uint32_t colour);                          // 0x00065bf0

#endif // DRIVING_GAME_BONDCARBASICS_H_
