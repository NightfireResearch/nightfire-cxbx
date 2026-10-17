#ifndef DRIVING_DEVTOOLS_ACTORSHADOW_H_
#define DRIVING_DEVTOOLS_ACTORSHADOW_H_

// NIGHTFIRE_ACTORSHADOW=1: anim/Actor.cpp and anim/Controllers.cpp against the originals - VU0_quatstoangvel, the
// controllers' getters and destructors, ActActor's matrices, origin, rotation and weapon code on actors of our own
// and copies of the live ones, the database's list and culling walk, std::string's members and the animation
// database's private data. Call once the track, its collision and its actors exist (the first simulation tick);
// returns at once unless the variable is set.
void ActorShadow_Run(void);

#endif // DRIVING_DEVTOOLS_ACTORSHADOW_H_
