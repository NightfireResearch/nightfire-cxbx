#ifndef TEXT_H_
#define TEXT_H_

#include "../actionhelpers.h"

typedef enum {
    Lang_UK = 0,
    Lang_FR = 1,
    Lang_GR = 2,
    Lang_SP = 3,
    Lang_IT = 4,
    Lang_DU = 5,
    Lang_USA = 6,
    Lang_JAP = 7,
    Lang_SW = 8
} tLANGUAGE;

void Text_Update2Line(void);
void Text_Update(void);
const char * Txt_BindLabel(Action_TranslatedText param_1,undefined4 param_2);
void Txt_SetLanguage(tLANGUAGE languageId);
void Txt_LoadLanguage(void);
void Txt_LanguageInit(void);
char* Txt_GetStringFromHeap(uchar param_1);
void Text_AddMsg(char param_1,char param_2,int param_3,char *str,int param_5,short maybeDurationFrames);
void __stdcall Text_FlushAllSubtitles(void);
void Txt_UnlockString(char* text);
void Txt_LockString(char* text);

uint GetLanguage(void);

// The kind of a Text_AddMsg message (its third argument), which decides the pane it shows on: 1 info, 2 objective,
// 3 mission, 4/5 subtitles, 6 pickup. HUD.cpp names the ones it needs as TXTMSG_* defines; Ghidra has only the
// one name below.
typedef enum TXTMSG_TYPE {
    TXTMSG_TYPE_UNKNOWN = 0,
    TXTMSG_TYPE_FORCE_UINT32 = 0x7fffffff
} TXTMSG_TYPE;

#pragma pack(push, 1)
// One line of a queued message (MsgQueue.activeList). Text_AddMsg word-wraps a long message into several of
// these, chained through nextLine.
typedef struct TXT_MSG {
    LLNODE_tag node;            // 0x00
    char *text;                 // 0x08 - the (locked) wrapped line
    struct TXT_MSG *nextLine;   // 0x0c - the message's next line, NULL for the last
    HASHCODE spriteHash;        // 0x10 - a picture to show instead of text (Text_AddMsg's fifth argument), 0 for none
    ushort length;              // 0x14 - strlen(text)
    ushort pad16;
    float timer;                // 0x18 - frames left on screen; Text_UpdateMsg counts it down by FRAME_RATE_MUL.
                                //        Negative: being dropped (Text_AddMsg sets -1 on objectives under a mission banner)
    uchar type;                 // 0x1c - its TXTMSG_TYPE
    char param_2;               // 0x1d - Text_AddMsg's second argument
    char playerNum;             // 0x1e - -1 for every player
} TXT_MSG;
#pragma pack(pop)
static_assert(offsetof(TXT_MSG, text) == 0x08, "TXT_MSG.text is at 0x08");
static_assert(offsetof(TXT_MSG, spriteHash) == 0x10, "TXT_MSG.spriteHash is at 0x10");
static_assert(offsetof(TXT_MSG, timer) == 0x18, "TXT_MSG.timer is at 0x18");
static_assert(offsetof(TXT_MSG, type) == 0x1c, "TXT_MSG.type is at 0x1c");
static_assert(offsetof(TXT_MSG, playerNum) == 0x1e, "TXT_MSG.playerNum is at 0x1e");

#endif // TEXT_H_
