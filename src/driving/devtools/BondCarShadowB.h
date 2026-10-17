#ifndef DRIVING_DEVTOOLS_BONDCARSHADOWB_H_
#define DRIVING_DEVTOOLS_BONDCARSHADOWB_H_

// NIGHTFIRE_BONDCARSHADOWB=1: PBondCar's submarine and spline physics, DebugObject, GetCarColourVariation and the
// accessors (game/BondCarModes.cpp, game/BondCarBasics.cpp) against the originals, on the live cars with their
// state perturbed and put back. See BondCarShadowB.cpp. Run from the first simulation tick, once the cars exist.
void BondCarShadowB_Run(void);

// NIGHTFIRE_BONDCARSHADOWB=3 (instead of the run above): every live call of the submarine and spline physics
// against the original on the state the game hands over, for NIGHTFIRE_BONDCARSHADOWB_TICKS ticks (600). Each tick.
void BondCarShadowB_Tick(void);

#endif // DRIVING_DEVTOOLS_BONDCARSHADOWB_H_
