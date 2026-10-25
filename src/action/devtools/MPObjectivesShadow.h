#pragma once

// MP_ObjectUpdate and MP_DemolitionProtectionUpdate (game/mp/multiplayer_objects.cpp), with the uplink and hill
// updates it calls, ours against the original on every call (MPObjectivesShadow=on in settings.ini).
void MPObjectivesShadow_Install(void);
