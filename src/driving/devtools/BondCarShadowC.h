#ifndef DRIVING_DEVTOOLS_BONDCARSHADOWC_H_
#define DRIVING_DEVTOOLS_BONDCARSHADOWC_H_

// NIGHTFIRE_BONDCARSHADOWC=1: PBondCar's driving (game/BondCarPhysics.cpp) against the originals, on the live cars
// with their state perturbed and put back. See BondCarShadowC.cpp. Run from the first simulation tick, once the cars
// exist.
void BondCarShadowC_Run(void);

#endif // DRIVING_DEVTOOLS_BONDCARSHADOWC_H_
