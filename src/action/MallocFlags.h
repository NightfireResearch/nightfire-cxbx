#ifndef MALLOCFLAGS_H_
#define MALLOCFLAGS_H_

#include "../helpers.h"

// Mem_Malloc's tags: the allocation type in the high byte (an index into MemInfo's names, which Mem_Info counts
// bytes by), the alignment in the low byte. The types' names are the game's own: memnames, the table Mem_Info and
// Mem_PrintAllInfo print, the same on the PS2, the Xbox and the GameCube (each platform has types of its own in it).
typedef ushort MallocFlags;

#define MALLOC_TYPE(flags)  (((flags) >> 8) & 0xff)
#define MALLOC_TYPE_COUNT   0x50          // MemInfo's names; the last is malloc_null

// Types (the game's memnames order)
enum MallocType {
    MALLOC_TYPE_ZERO,                 // 0x00 malloc_zero
    MALLOC_TYPE_CELSTRUCTS,           // 0x01
    MALLOC_TYPE_COMPPLANEEQ,
    MALLOC_TYPE_ENTITY_TABLE,
    MALLOC_TYPE_OBJS,
    MALLOC_TYPE_VIEWERSTRUCTS,
    MALLOC_TYPE_WORLDSTRUCTS,
    MALLOC_TYPE_LIGHTSTRUCTS,
    MALLOC_TYPE_PORTALSTRUCTS,
    MALLOC_TYPE_CELGLISTSTRUCTS,
    MALLOC_TYPE_MAP_HEADER,           // 0x0a
    MALLOC_TYPE_TEXTURES,
    MALLOC_TYPE_ANIM_DISTANCE_TABLE,
    MALLOC_TYPE_ANIM_FRAME_TABLE,
    MALLOC_TYPE_ANIM_FRAME_BUFFER,
    MALLOC_TYPE_XYZMISCQZYX,
    MALLOC_TYPE_TEMP_BUFFER,          // 0x10
    MALLOC_TYPE_PS2_DMA_LIST,
    MALLOC_TYPE_XBOX,
    MALLOC_TYPE_SFXDATA,
    MALLOC_TYPE_SCRIPTDATA,
    MALLOC_TYPE_BOT_PATH1,
    MALLOC_TYPE_GAMECUBE,
    MALLOC_TYPE_PARSEFILEBUFFER,
    MALLOC_TYPE_PATHPOINTERS,
    MALLOC_TYPE_RIGIDBODY,
    MALLOC_TYPE_COLLDATA,             // 0x1a
    MALLOC_TYPE_LLIST,
    MALLOC_TYPE_ANIM_SKIN_DATA,
    MALLOC_TYPE_ANIM_SKEL_DATA,
    MALLOC_TYPE_ANIM_SEQ_DATA,
    MALLOC_TYPE_ANIM_SCRIPT_DATA,
    MALLOC_TYPE_ANIM_SKIN_STRUCTS,    // 0x20
    MALLOC_TYPE_ANIM_SKINMAT_STRUCTS,
    MALLOC_TYPE_ANIM_ALTSKIN_STRUCTS,
    MALLOC_TYPE_ANIM_SEQ_STRUCTS,
    MALLOC_TYPE_ANIM_SEQ_DATA_BUFFERS,
    MALLOC_TYPE_ANIM_SCRIPT_STRUCTS,
    MALLOC_TYPE_ANIM_OBJECT_STRUCTS,
    MALLOC_TYPE_PLRHUD,
    MALLOC_TYPE_PS2_OBJECT,
    MALLOC_TYPE_PS2_FILE,
    MALLOC_TYPE_PS2_SPRITE,           // 0x2a
    MALLOC_TYPE_PS2_TEXTURE,
    MALLOC_TYPE_PS2_PALETTE,
    MALLOC_TYPE_PS2_PARTICLE,
    MALLOC_TYPE_PS2_CLONE,
    MALLOC_TYPE_PC_TEXTURE,
    MALLOC_TYPE_PC_MESH,              // 0x30
    MALLOC_TYPE_MENU,
    MALLOC_TYPE_SHARD,
    MALLOC_TYPE_COPTER_PATH,
    MALLOC_TYPE_DRONE_SPAWN_OBJS,
    MALLOC_TYPE_DRONE_SPAWN_VARS,
    MALLOC_TYPE_LANG_DATA,
    MALLOC_TYPE_LIGHT_DATA,
    MALLOC_TYPE_UNPAK_BUFFER,
    MALLOC_TYPE_ARAM_BUFFER,
    MALLOC_TYPE_ANIM_CACHE,           // 0x3a
    MALLOC_TYPE_ANIM_MORPH_DATA_BUFFERS,
    MALLOC_TYPE_ANIM_SET_STRUCTS,
    MALLOC_TYPE_ANIM_UNPAK_OPT,
    MALLOC_TYPE_ANIM_SEQ_HDR,
    MALLOC_TYPE_PHYSICS,
    MALLOC_TYPE_LOAD_DIR,             // 0x40
    MALLOC_TYPE_LOAD_DATA,
    MALLOC_TYPE_GAME_PARTICLE,
    MALLOC_TYPE_GC_ANIM_CACHE,
    MALLOC_TYPE_GC_DL,
    MALLOC_TYPE_GC_SKINS,
    MALLOC_TYPE_GC_PCLIST128,
    MALLOC_TYPE_GC_BIGBUFFER,
    MALLOC_TYPE_GC_ANIMMATRIX,
    MALLOC_TYPE_GC_CARD,
    MALLOC_TYPE_GC_USB2EXI,           // 0x4a
    MALLOC_TYPE_GC_TEXTURE,
    MALLOC_TYPE_GC_SOUND,
    MALLOC_TYPE_BOT_PATH2,
    MALLOC_TYPE_BOT_PATH3,
    MALLOC_TYPE_NULL,                 // 0x4f malloc_null
};

// The tags the action engine allocates with (type << 8 | alignment)
#define MALLOC_ZERO                     ((MallocFlags)0x0004)   // a free block's flags
#define MALLOC_CELSTRUCTS               ((MallocFlags)0x0104)
#define MALLOC_OBJS                     ((MallocFlags)0x0404)
#define MALLOC_VIEWERSTRUCTS            ((MallocFlags)0x0504)
#define MALLOC_WORLDSTRUCTS             ((MallocFlags)0x0604)
#define MALLOC_PORTALSTRUCTS            ((MallocFlags)0x0804)
#define MALLOC_CELGLISTSTRUCTS          ((MallocFlags)0x0904)
#define MALLOC_MAP_HEADER               ((MallocFlags)0x0a04)
#define MALLOC_TEXTURES_ALIGN8          ((MallocFlags)0x0b08)
#define MALLOC_ANIM_DISTANCE_TABLE      ((MallocFlags)0x0c04)
#define MALLOC_XBOX                     ((MallocFlags)0x1204)
#define MALLOC_SCRIPTDATA               ((MallocFlags)0x1404)
#define MALLOC_BOT_PATH1_ALIGN8         ((MallocFlags)0x1508)
#define MALLOC_PARSEFILEBUFFER          ((MallocFlags)0x1704)
#define MALLOC_PATHPOINTERS             ((MallocFlags)0x1804)
#define MALLOC_COLLDATA                 ((MallocFlags)0x1a04)
#define MALLOC_LLIST                    ((MallocFlags)0x1b04)
#define MALLOC_ANIM_SKIN_DATA           ((MallocFlags)0x1c04)
#define MALLOC_ANIM_SEQ_DATA            ((MallocFlags)0x1e04)
#define MALLOC_ANIM_SCRIPT_DATA         ((MallocFlags)0x1f04)
#define MALLOC_ANIM_SKIN_STRUCTS        ((MallocFlags)0x2004)
#define MALLOC_ANIM_SKINMAT_STRUCTS     ((MallocFlags)0x2104)
#define MALLOC_ANIM_SEQ_STRUCTS         ((MallocFlags)0x2304)
#define MALLOC_ANIM_SEQ_DATA_BUFFERS    ((MallocFlags)0x2404)
#define MALLOC_ANIM_SCRIPT_STRUCTS      ((MallocFlags)0x2504)
#define MALLOC_ANIM_OBJECT_STRUCTS      ((MallocFlags)0x2604)
#define MALLOC_PLRHUD                   ((MallocFlags)0x2704)
#define MALLOC_MENU                     ((MallocFlags)0x3104)
#define MALLOC_SHARD                    ((MallocFlags)0x3204)
#define MALLOC_COPTER_PATH              ((MallocFlags)0x3304)
#define MALLOC_DRONE_SPAWN_OBJS         ((MallocFlags)0x3404)
#define MALLOC_DRONE_SPAWN_VARS         ((MallocFlags)0x3504)
#define MALLOC_LANG_DATA                ((MallocFlags)0x3604)
#define MALLOC_LIGHT_DATA               ((MallocFlags)0x3704)
#define MALLOC_ANIM_MORPH_DATA_BUFFERS  ((MallocFlags)0x3b04)
#define MALLOC_ANIM_SET_STRUCTS         ((MallocFlags)0x3c04)
#define MALLOC_ANIM_UNPAK_OPT           ((MallocFlags)0x3d04)
#define MALLOC_GAME_PARTICLE            ((MallocFlags)0x4204)
#define MALLOC_BOT_PATH2_ALIGN8         ((MallocFlags)0x4d08)
#define MALLOC_BOT_PATH3_ALIGN8         ((MallocFlags)0x4e08)

#endif // MALLOCFLAGS_H_
