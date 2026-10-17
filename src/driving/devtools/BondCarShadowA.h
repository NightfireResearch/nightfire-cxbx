#ifndef DRIVING_DEVTOOLS_BONDCARSHADOWA_H_
#define DRIVING_DEVTOOLS_BONDCARSHADOWA_H_

// NIGHTFIRE_BONDCARSHADOWA=1: game/BondCarState.cpp, BondCarSnowmobile.cpp and SoftZone.cpp against the originals,
// on copies of the live cars (the player's and the AI's) - set-up, controls, damage, shock, ResetCar, the RPM, the
// weapons, beacon, glares and turn signals, the two-wheel stunt, ImproveLanding, the snowmobile physics - and the
// soft zones on points in and around them. Call once the cars exist (the first simulation tick); returns at once
// unless the variable is set. =2 names each case before it runs.
void BondCarShadowA_Run(void);

// NIGHTFIRE_BONDCARSHADOWA=3 (BondCarShadowA_Run then does nothing): every live ProcessSnowmobilePhysics call, the
// original (with AddSnowmobileForces and ImproveLanding as shipped) on a copy of the car's state and then the port
// for real, compared byte for byte; the first differing calls are printed with their inputs. Call once a
// simulation tick; it stops after NIGHTFIRE_BONDCARSHADOWA_TICKS ticks (7000 if unset).
void BondCarShadowA_Tick(void);

#endif // DRIVING_DEVTOOLS_BONDCARSHADOWA_H_
