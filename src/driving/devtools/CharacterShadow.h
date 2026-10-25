#ifndef DRIVING_DEVTOOLS_CHARACTERSHADOW_H_
#define DRIVING_DEVTOOLS_CHARACTERSHADOW_H_

// NIGHTFIRE_CHARACTERSHADOW=1: anim/AnimationDatabase.cpp and anim/Character.cpp against the originals - the
// banks' lookups, ActAnimGroup's channels, ActCharacterInfo's parsing, the draw options, the light block, the
// small character methods on copies of the live characters, the helpers and the event objects. Call once the
// track's actors exist (the first simulation tick); returns at once unless the variable is set.
void CharacterShadow_Run(void);

#endif // DRIVING_DEVTOOLS_CHARACTERSHADOW_H_
