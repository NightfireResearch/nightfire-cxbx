#ifndef DRIVING_DEVTOOLS_GEOMSHADOW_H_
#define DRIVING_DEVTOOLS_GEOMSHADOW_H_

// NIGHTFIRE_GEOMSHADOW=1: the collision system's geometry maths (WWorldMath, FasterSegmentIntersect, the physics
// Util_* helpers, OBB, WWorldPos) against the originals, bit for bit. See GeomShadow.cpp.

// The pure functions on random and edge inputs. At injection time, before the game runs.
void GeomShadow_Run(void);

// WWorldPos's face searches and MakeFaceAtPoint over the loaded world. From the first simulation tick, when the
// track's collision data is loaded (devtools/Teleport.cpp's first Teleport_Tick).
void GeomShadow_RunWorld(void);

#endif // DRIVING_DEVTOOLS_GEOMSHADOW_H_
