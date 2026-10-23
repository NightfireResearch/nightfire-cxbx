#ifndef TEXT_H_
#define TEXT_H_

#include "../actionhelpers.h"
#include "../../common/xbeClass.h"

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

const char * Txt_BindLabel(Action_TranslatedText param_1,undefined4 param_2);
void Txt_SetLanguage(tLANGUAGE languageId);
void Txt_LoadLanguage(void);
void Txt_LanguageInit(void);
char* Txt_GetStringFromHeap(uchar param_1);
void Txt_UnlockString(char* text);
void Txt_LockString(char* text);

uint GetLanguage(void);
char* Timer_Seconds2String(int hundredths, int format);

// The kind of a Text_AddMsg message (its third argument), which decides the pane it shows on
#define TXTMSG_INFO             1   // e.g. a hostage's line (NDrone2_Hostage)
#define TXTMSG_OBJECTIVE        2   // objective updates (Mission.cpp)
#define TXTMSG_MISSION          3
#define TXTMSG_SUBTITLE         4
#define TXTMSG_SUBTITLE_2LINE   5   // a subtitle without the main view, in two lines (Text_Update2Line)
#define TXTMSG_PICKUP           6

#pragma pack(push, 1)
// The layout of each kind of message: the format it is word-wrapped in and the width it is wrapped to. PS2 name:
// TextMsgFormats. Entry 0 is empty; 5 (the two-line subtitles) wraps to 0.6 and 0.4 of its width in two lines.
typedef struct TextMsgFormat {
    char *font;         // a format, handed to Font_WordWrapString
    ushort wrapWidth;
    ushort pad;
} TextMsgFormat;
#pragma pack(pop)
LAYOUT_32(sizeof(TextMsgFormat) == 8, "TextMsgFormat is 8 bytes");
extern TextMsgFormat TextMsgFormats[7];

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
    uchar type;                 // 0x1c - TXTMSG_*
    char priority;              // 0x1d - Text_AddMsg's second argument: the queue's order (CompMsgTxt)
    char playerNum;             // 0x1e - -1 for every player
    char pad1f;
} TXT_MSG;
#pragma pack(pop)
LAYOUT_32(offsetof(TXT_MSG, text) == 0x08, "TXT_MSG.text is at 0x08");
LAYOUT_32(offsetof(TXT_MSG, spriteHash) == 0x10, "TXT_MSG.spriteHash is at 0x10");
LAYOUT_32(offsetof(TXT_MSG, timer) == 0x18, "TXT_MSG.timer is at 0x18");
LAYOUT_32(offsetof(TXT_MSG, type) == 0x1c, "TXT_MSG.type is at 0x1c");
LAYOUT_32(offsetof(TXT_MSG, playerNum) == 0x1e, "TXT_MSG.playerNum is at 0x1e");
LAYOUT_32(sizeof(TXT_MSG) == 0x20, "TXT_MSG is 0x20 bytes");

TXT_MSG* Text_TakeMsg(void);
void Text_Init(uchar warmReset);
void Text_InitSubtitlesFormat(void);
void Text_AddMsg(char player, char priority, int type, char *text, int sprite, short frames);
void Text_DelMsg(TXT_MSG *msg);
void Text_FlushAllSubtitles(void);
TXT_MSG* Text_NextMsg(TXT_MSG *msg, char player, int type);
bool Text_UpdateMsg(char player, int type, TXT_MSG **msg);
void Text_Update(void);
void Text_Update2Line(void);

#endif // TEXT_H_
