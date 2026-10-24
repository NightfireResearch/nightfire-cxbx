#include "DroneTables.h"
#include "DroneAnim.h"
#include "DroneTables.inc"

// XBE_GLOBAL(0x00177ca0, 0x3e8) readonly relocate
const DroneStateFunc NDrone2_StateFuncs[NUM_DSTATES] = DRONE_STATE_FUNCS_INIT;
// XBE_GLOBAL(0x001776f8, 0x1b0) readonly relocate
const DroneModeSetting DroneModeSettings[DMODE_COUNT] = DRONE_MODE_SETTINGS_INIT;
// XBE_GLOBAL(0x001778a0, 0x3fc) readonly relocate
const DroneTypeSetting DroneTypeSettings[DTYPE_COUNT] = DRONE_TYPE_SETTINGS_INIT;
// XBE_GLOBAL(0x001634e0, 0xb6) readonly relocate
const DroneBehaviourLayout DroneBehaviourLayouts[NUM_DRONE_PROPERTIES] = DRONE_BEHAVIOUR_LAYOUTS_INIT;
// XBE_GLOBAL(0x00163598, 0x8c) readonly relocate
const uint32_t DroneBehaviourMasks[35] = DRONE_BEHAVIOUR_MASKS_INIT;
// XBE_GLOBAL(0x00163628, 0x196) readonly relocate
const BOT_stats_t DefaultBotStats[29] = DEFAULT_BOT_STATS_INIT;
// XBE_GLOBAL(0x001664c0, 0x594)
const DroneAnimState DroneAnimStates[NUM_DRONE_ANIM_STATES] = DRONE_ANIM_STATES_INIT;
// XBE_GLOBAL(0x00169338, 0xb7f0)
const DroneAnimScript Drone_AnimTables[NUM_DRONE_ANIM_ROWS][NUM_DRONE_ANIM_SETS] = DRONE_ANIM_TABLES_INIT;
