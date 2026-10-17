#ifndef DRIVING_DEVTOOLS_ATTRIBSHADOW_H_
#define DRIVING_DEVTOOLS_ATTRIBSHADOW_H_

// NIGHTFIRE_ATTRIBSHADOW=1: the attribute system (src/driving/data/Attribute*.cpp) against its originals - the
// parsers on synthetic text, the trees, the store-block sort and vector on random data, and the disc's attribute
// database loaded side by side into two systems. See AttribShadow.cpp. Call it once the game's own attribute
// database exists and the mission's archive is open (after Bond_StartUpSystem and the track's load).
void AttribShadow_Run(void);

#endif // DRIVING_DEVTOOLS_ATTRIBSHADOW_H_
