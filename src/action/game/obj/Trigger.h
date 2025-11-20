#ifndef TRIGGER_H_
#define TRIGGER_H_

#include "../../actionhelpers.h"

obj_tag * Trigger_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * MusicTrigger_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * SoundTrigger_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);

obj_tag * Trigger_LoadLevelCreate(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_TouchOnce(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_Touch(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_MultiplexIn(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_MultiplexSIn(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_MultiplexOut(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_MultiplexOrIn(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_MoviePlayer(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);

#endif // TRIGGER_H_