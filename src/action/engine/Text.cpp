#include "Text.h"

#include "psiFile.h"
#include "viewer.h"
#include "../game.h"
#include "../memory.h"
#include "../game/mp/multiplayer.h"
#include "../gfx/Font.h"
#include "../gfx/Sprite.h"
#include "../util/CirBuf.h"
#include "../util/DList.h"
#include "../util/LList.h"
#include "../util/Sort.h"
#include "../util/bin.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#pragma fp_contract(off)

// XBE_GLOBAL(0x0018ccf8, 0x38) readonly relocate
TextMsgFormat TextMsgFormats[7] = {
    {NULL, 0, 0},
    {FontFormat2Centred, 540, 0},
    {FontFormat2Centred, 540, 0},
    {FontFormat1Centred, 540, 0},
    {FontFormat2Centred, 540, 0},
    {FontFormat2Centred, 575, 0},
    {FontFormat2Centred, 320, 0},
};

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

extern uint32_t SubtitlesEnabled;

// XBE_GLOBAL(0x00215588, 0x4)
static const char** Bank;
// XBE_GLOBAL(0x0021558c, 0x4)
static void* BankData;
// XBE_GLOBAL(0x00215590, 0x4)
static uint32_t NumEntries;
// XBE_GLOBAL(0x00215594, 0x4)
static uint32_t CurrentLanguage;
// XBE_GLOBAL(0x00215580, 0x4)
static uint32_t NumFixups;
// XBE_GLOBAL(0x001fec78, 0x4)
static uint* FixupTable;
// A lock count per heap string (Txt_LockString, Txt_UnlockString); 0 = free.
// XBE_GLOBAL(0x001fec80, 0x100)
static uint8_t StringHeapLock[256];
// XBE_GLOBAL(0x00215584, 0x4)
static uint32_t StringHeapCnt;


// AUTOINJECT
void Txt_SetLanguage(tLANGUAGE languageId) {
    CurrentLanguage = languageId;
    Txt_LoadLanguage();
}

// Order must match the order in the enum
// XBE_GLOBAL(0x0017c1f4, 0x24)
const char* LanguageFileNames[] = {
    "UKTxt.Dat",
    "FRTxt.Dat",
    "GRTxt.Dat",
    "SPTxt.Dat",
    "ITTxt.Dat",
    "DUTxt.Dat",
    "USATxt.Dat",
    "JAPTxt.Dat",
    "SWTxt.Dat"
};

// AUTOINJECT
void Txt_LoadLanguage(void) {

    char filename[1024];
    sprintf(filename, "%s%s", "", LanguageFileNames[CurrentLanguage]);
    printf("Loading language %i from file %s\n", CurrentLanguage, filename);

    int size = 0;
    void* dataFile = psiFileLoad(filename, 0x3604, &size);
    void* fileAt = dataFile;

    if(dataFile == NULL)
        return;

    uint bankSize = BIN_GetDWord((uchar **)&fileAt); // Also increments fileAt

    //printf("Loaded %i bytes of data file, of which %i is bank info\n", size, bankSize);

    // Allocate and copy bank data to RAM - this is the raw ASCII or UTF-16 buffer
    BankData = Mem_Malloc(bankSize, 0x3604, 0);
    memcpy(BankData, fileAt, bankSize);
    
    // Add 4 bytes even if the string ends on a word boundary already. Unclear why.
    if(bankSize % 4 == 0)
        fileAt = (void*)(((int)fileAt + 4));

    // The file is padded to a multiple of 4 bytes
    fileAt = (void*)(((int)fileAt + bankSize + 3) & ~3);

    NumEntries = BIN_GetDWord((uchar **)&fileAt); // 2801 strings
    //printf("Num entries: %i\n", NumEntries);

    Bank = (const char**)Mem_Malloc(NumEntries * 4, 0x3604, 0); // 4 bytes of offset (into the ASCII data array) per string
    
    // Looks like the first entry in the bank is hardcoded to be a blank string
    Bank[0] = "";

    // "relocate", ie map from offset to memory address (ie so that we can access as char*)
    for(uint i = 1; i < NumEntries; i++) {
        uint32_t offset = BIN_GetDWord((uchar **)&fileAt);
        Bank[i] = (const char*)((uint)BankData + offset);
        //printf("String %i: %s\n", i, Bank[i]);
    }
    
    // Fixup table has 7 entries
    NumFixups = BIN_GetDWord((uchar **)&fileAt);
    //printf("Num fixups: %i\n", NumFixups);
    
    FixupTable = (uint*) Mem_Malloc((NumFixups+1) * 4, 0x3604, 0);

    for(uint i = 0; i < NumFixups; i++) {
        uint offset = BIN_GetDWord((uchar **)&fileAt);
        FixupTable[i] = offset;
    }

    Mem_Free(&dataFile);

}

uint Txt_GetIndex(Action_TranslatedText tt) {
    return (int)FixupTable[(tt >> 24)] + (tt & 0xFFFFFF);
}

// AUTOINJECT
uint GetLanguage(void) {
    return CurrentLanguage;
}

// 256 strings of 0x168 bytes that Txt_BindLabel and friends format into, each with a lock count
// (StringHeapLock) - 0 means free. Handed out round-robin from StringHeapCnt; when none is free, everyone
// shares one overflow buffer.
#define StringHeap (*(char(*)[256][0x168])0x001fed80)
// XBE_GLOBAL(0x00215598, 0x168)
static char StringHeapOverflow[0x168];

// Finds the next free heap string at or after StringHeapCnt, wrapping round, locks it with lockCount and
// returns it emptied. The first scan compares only StringHeapCnt's low 16 bits against 256, the second the whole
// value, as the original does.
// AUTOINJECT
char* Txt_GetStringFromHeap(uchar lockCount) {
    uint start = StringHeapCnt;
    uint found = 0x100;
    for (ushort i = (ushort)start; i < 0x100; i++) {
        if (StringHeapLock[i] == 0) {
            found = i;
            break;
        }
    }
    if (found == 0x100) {
        for (ushort i = 0; i < start; i++) {
            if (StringHeapLock[i] == 0) {
                found = i;
                break;
            }
        }
        if (found == 0x100)
            return StringHeapOverflow;
    }
    StringHeapLock[found] = lockCount;
    StringHeapCnt = found + 1;
    StringHeap[found][0] = '\0';
    return StringHeap[found];
}

// AUTOINJECT
const char* Txt_BindLabel(Action_TranslatedText a, unsigned int b) {

    if(a == 0xFFFFFFFF || FixupTable == NULL)
        return NULL;

    uint index = Txt_GetIndex(a);
    
    if(index >= NumEntries)
        return "Invalid Text Label";

    if(index == 0) {
        return (const char*)Txt_GetStringFromHeap((uchar)b);
    }

    if(Bank != NULL)
        return Bank[index];
    
    return "Not Loaded";
}


// AUTOINJECT
void Txt_LanguageInit(void) {
    memset(&StringHeapLock, 0, sizeof(StringHeapLock));
    StringHeapCnt = 0;
    BankData = NULL;
    Bank = NULL;
    Txt_LoadLanguage();
}


// Which heap string a pointer falls in: the unsigned distance from the start of the heap in whole strings, so any
// pointer into a string - not only its start - names that string, and one below the heap wraps round to a huge
// index. Anything that is not in the heap (the overflow buffer, a text bank string, a literal) comes out 256 or
// more. The original divides by 0x168 with a multiply by 0x6c16c16d; this is the same unsigned division.
static uint Txt_HeapIndex(const char* text) {
    return ((uint32_t)(uintptr_t)text - (uint32_t)(uintptr_t)&StringHeap[0][0]) / sizeof(StringHeap[0]);
}

// Holds a heap string against reuse by Txt_GetStringFromHeap. The count is a byte and is not checked: the 256th
// lock wraps it to 0, freeing the string, as in the original.
// AUTOINJECT
void Txt_LockString(char* text) {
    if (text == NULL)
        return;
    uint i = Txt_HeapIndex(text);
    if (i < 0x100)
        StringHeapLock[i]++;
}

// Drops one lock; never takes a free string below 0.
// AUTOINJECT
void Txt_UnlockString(char* text) {
    if (text == NULL)
        return;
    uint i = Txt_HeapIndex(text);
    if (i < 0x100 && StringHeapLock[i] != 0)
        StringHeapLock[i]--;
}

// ---- Messages: the HUD's message panes and the subtitles

// The queue of message lines, the subtitles' sprites and the line each shows
// XBE_GLOBAL(0x0029abf8, 0x18)
DLISTINFO_tag MsgQueue;
#define SubtitlesInfo (*(SpriteInfo *)0x0018cd30)
// XBE_GLOBAL(0x0029ae20, 0x4)
TXT_MSG *SubTitleMsg;
// XBE_GLOBAL(0x0029ae24, 0x4)
sprite *SubTitleSpr;  // a one-line subtitle
// XBE_GLOBAL(0x0029ae28, 0x4)
sprite *SubTitleSpr1;  // the two lines of the other kind
// XBE_GLOBAL(0x0029ae2c, 0x4)
sprite *SubTitleSpr2;
// XBE_GLOBAL(0x0029ae30, 0x4)
char *SubTitleStr;  // their text, held while shown
// XBE_GLOBAL(0x0029ae34, 0x4)
char *SubTitleStr2;
// The last 128 messages of the kinds kept (Text_AddMsg), held in the string heap
// XBE_GLOBAL(0x0029ae10, 0x10)
CIRBUF_INFO ReceivedMsgs;
#define ReceivedBuf ((int *)0x0029ac10)
// The two-line subtitles' format: font 2, centred (the one TextMsgFormats[5] has)
#define SubtitleFormat FontFormat2Centred

#define SUBTITLE_COLOUR 0x7d6d59ff
#define SUBTITLE_VIEWERS 6  // the highest of the views that show sprites gets them

// The first queued line for player of type
static TXT_MSG* Text_FindMsg(char player, int type) {
    for (TXT_MSG *msg = (TXT_MSG *)MsgQueue.activeList.head; msg != NULL; msg = (TXT_MSG *)msg->node.next) {
        if (msg->playerNum == player && msg->type == type)
            return msg;
    }
    return NULL;
}

// The subtitles go to the highest view that shows sprites
static void Text_LinkSubtitle(sprite *spr) {
    for (short viewer = SUBTITLE_VIEWERS - 1; viewer >= 0; viewer--) {
        if (glb_viewer[viewer] != NULL && glb_viewer[viewer]->spritesShown) {
            spr->linkedViewer = (char)viewer;
            return;
        }
    }
}

// A free line, more of them made when there is none. Only a line never used counts as free: one Text_DelMsg puts
// back keeps its type, so is never taken again (as the original). Our name.
// AUTOINJECT
TXT_MSG* Text_TakeMsg(void) {
    for (;;) {
        for (LLNODE_tag *node = MsgQueue.freeList.head; node != NULL; node = node->next) {
            if (((TXT_MSG *)node)->type == 0)
                return (TXT_MSG *)LList_Remove(&MsgQueue.freeList, node);
        }
        LList_AllocnNodes(&MsgQueue.freeList, 0x20);
    }
}

// The subtitles' sprite gets their format: a dynamic initialiser, run before main. Not named on PS2; the name is ours.
// FUNC_AT(0x000f5520)
void Text_InitSubtitlesFormat(void) {
    SubtitlesInfo.maybeClippedText = TextMsgFormats[TXTMSG_SUBTITLE].font;
}

// The subtitles' sprites and an empty queue; at a cold start an empty history, at a warm one the history's strings
// held again
// FUNC_AT(0x000d1c90)
void Text_Init(uchar warmReset) {
    SubTitleMsg = NULL;
    SubTitleSpr = Sprite_Create2(&SubtitlesInfo);
    SubTitleSpr1 = Sprite_Create2(&SubtitlesInfo);
    SubTitleSpr2 = Sprite_Create2(&SubtitlesInfo);
    DList_Init(&MsgQueue, sizeof(TXT_MSG), 0);
    if (warmReset) {
        for (ushort i = 0; i < ReceivedMsgs.size; i++)
            Txt_LockString((char *)ReceivedMsgs.buffer[i]);
        return;
    }
    CirBuf_Init(&ReceivedMsgs, 0x80, ReceivedBuf);
}

// Queues a message for player (-1 every player) of type (TXTMSG_*), word-wrapped into lines that share frames
// between them by length, at least a second each but for subtitles. Without text, one line showing sprite. A
// subtitle becomes a two-line one when the main view is off and no movie plays; priority 0xff shows it even with
// subtitles off. In multiplayer a pickup message is an info one, and a new info message divides the time of the
// player's others. A mission message ends the objectives on show. Messages of the kinds other than info, subtitles
// and pickups go in the history too.
// AUTOINJECT
void Text_AddMsg(char player, char priority, int type, char *text, int sprite, short frames) {
    bool forced = false;
    if (type == TXTMSG_SUBTITLE) {
        if ((uchar)priority == 0xff) {
            priority = 0;
            forced = true;
        }
        if (!glb_viewer[4]->enabled && movieFinished())
            type = TXTMSG_SUBTITLE_2LINE;
    }

    if (MPSettings.isMultiplayer) {
        if (type == TXTMSG_PICKUP)
            type = TXTMSG_INFO;
        if (player != -1 && player < 4 && type == TXTMSG_INFO) {
            int count = 0;
            for (TXT_MSG *msg = (TXT_MSG *)MsgQueue.activeList.head; msg != NULL; msg = (TXT_MSG *)msg->node.next) {
                if (msg->playerNum == player && msg->type == TXTMSG_INFO)
                    count++;
            }
            int shares = count + 1;
            if (count != 0) {
                for (TXT_MSG *msg = (TXT_MSG *)MsgQueue.activeList.head; msg != NULL;
                     msg = (TXT_MSG *)msg->node.next) {
                    if (msg->playerNum == player && msg->type == TXTMSG_INFO)
                        msg->timer = msg->timer / shares;
                }
            }
        }
    }
    if (type == TXTMSG_MISSION) {
        for (TXT_MSG *msg = (TXT_MSG *)MsgQueue.activeList.head; msg != NULL; msg = (TXT_MSG *)msg->node.next) {
            if (msg->type == TXTMSG_OBJECTIVE)
                msg->timer = -1.0f;
        }
    }

    if (player > 3)
        return;
    if (!forced && !SubtitlesEnabled && (type == TXTMSG_SUBTITLE || type == TXTMSG_SUBTITLE_2LINE))
        return;

    if (text == NULL) {
        TXT_MSG *msg = Text_TakeMsg();
        msg->spriteHash = (HASHCODE)sprite;
        msg->priority = priority;
        msg->timer = frames;
        msg->text = NULL;
        msg->length = 0;
        msg->type = (uchar)type;
        msg->playerNum = player;
        msg->nextLine = NULL;
        LList_InsertAt(&MsgQueue.activeList, &msg->node, 0, CompMsgTxt);
        return;
    }

    if (type != TXTMSG_INFO && !(type > TXTMSG_MISSION && type <= TXTMSG_PICKUP)) {
        Txt_LockString(text);
        Txt_UnlockString((char *)CirBuf_Add(&ReceivedMsgs, (int)text));
    }

    int length = (int)strlen(text);
    char *rest = text;
    TXT_MSG *previous = NULL;
    do {
        char *line;
        if (type == TXTMSG_SUBTITLE_2LINE) {
            // The first line wrapped to 0.6 of the width, the rest of it to 0.4 and put on its end
            TextMsgFormat *format = &TextMsgFormats[TXTMSG_SUBTITLE_2LINE];
            line = Font_WordWrapString(&rest, (int)strlen(rest), format->font, Ftol(format->wrapWidth * 0.6));
            if (line == NULL)
                return;
            if (rest != NULL)
                strcat(line, Font_WordWrapString(&rest, (int)strlen(rest), format->font,
                                                 Ftol(format->wrapWidth * 0.4)));
        } else {
            line = Font_WordWrapString(&rest, (int)strlen(rest), TextMsgFormats[type].font,
                                       TextMsgFormats[type].wrapWidth);
        }
        if (line == NULL)
            return;

        TXT_MSG *msg = Text_TakeMsg();
        msg->spriteHash = HASHCODE_NONE;
        msg->priority = priority;
        msg->text = line;
        Txt_LockString(line);
        msg->length = (ushort)strlen(msg->text);
        if (frames > 0) {
            // Its share of the frames by length (of the length's low 16 bits)
            msg->timer = (short)Ftol((double)msg->length / (ushort)length * frames + 0.5f);
            if (type != TXTMSG_SUBTITLE && type != TXTMSG_SUBTITLE_2LINE) {
                uint least = Ftol(msg->timer);
                if (least <= 60)
                    least = 60;
                msg->timer = (float)least;
            }
        } else {
            msg->timer = frames;
        }
        msg->type = (uchar)type;
        msg->playerNum = player;
        msg->nextLine = NULL;
        if (previous == NULL) {
            LList_InsertAt(&MsgQueue.activeList, &msg->node, 0, CompMsgTxt);
        } else {
            LList_Insert(&MsgQueue.activeList, &previous->node, &msg->node);
            previous->nextLine = msg;
        }
        previous = msg;
    } while (rest != NULL);
}

// Takes a line off the queue, releasing its text where it holds it (and a subtitle's lines on show)
// AUTOINJECT
void Text_DelMsg(TXT_MSG *msg) {
    if (msg == NULL)
        return;
    switch (msg->type) {
        case TXTMSG_SUBTITLE:
        case TXTMSG_SUBTITLE_2LINE:
            if (msg == SubTitleMsg) {
                if (SubTitleStr != NULL) {
                    Txt_UnlockString(SubTitleStr);
                    SubTitleStr = NULL;
                }
                if (SubTitleStr2 != NULL) {
                    Txt_UnlockString(SubTitleStr2);
                    SubTitleStr2 = NULL;
                }
            }
            // fall through
        case TXTMSG_INFO:
        case TXTMSG_PICKUP:
            if (msg->text != NULL)
                Txt_UnlockString(msg->text);
            break;
        default:
            break;
    }
    DList_RemoveFromInUse2Free(&MsgQueue, &msg->node);
}

// Drops every subtitle, the one on show with them
// AUTOINJECT
void Text_FlushAllSubtitles(void) {
    SubTitleMsg = NULL;
    SubTitleSpr->maybeEnabled = 0xff;
    SubTitleSpr1->maybeEnabled = 0xff;
    SubTitleSpr2->maybeEnabled = 0xff;
    TXT_MSG *msg = (TXT_MSG *)MsgQueue.activeList.head;
    while (msg != NULL) {
        TXT_MSG *next = (TXT_MSG *)msg->node.next;
        if (msg->type == TXTMSG_SUBTITLE || msg->type == TXTMSG_SUBTITLE_2LINE)
            Text_DelMsg(msg);
        msg = next;
    }
}

// Drops a line and gives the one after: the message's next line, or the first queued for player of type. Our name.
TXT_MSG* _Text_NextMsg(TXT_MSG *msg, char player, int type) {
    TXT_MSG *next = msg != NULL ? msg->nextLine : NULL;
    Text_DelMsg(msg);
    if (next != NULL)
        return next;
    return Text_FindMsg(player, type);
}

// EAX the line, BL the player, EDI the type
// AUTOLTCG
void __declspec(naked) Text_NextMsg(void) {
    _asm {
        push ecx
        push edx
        push edi
        push ebx
        push eax
        call _Text_NextMsg
        add esp, 12
        pop edx
        pop ecx
        ret
    }
}

// Counts down the line *msg shows for player (-1 any) of type; when its time is up, or there is none, the next.
// True when *msg changed (or was looked for), false while it shows on, or when the player's view shows no sprites.
// AUTOINJECT
bool Text_UpdateMsg(char player, int type, TXT_MSG **msg) {
    if (player != -1) {
        viewer_tag *viewer = glb_viewer[player];
        if (viewer != NULL && !viewer->spritesShown)
            return false;
    }
    TXT_MSG *line = *msg;
    if (line == NULL) {
        Text_DelMsg(NULL);
        *msg = Text_FindMsg(player, type);
        return true;
    }
    if (line->timer > 0.0f)
        line->timer = line->timer - FRAME_RATE_MUL;
    if (!(line->timer <= 0.0f))
        return false;
    *msg = _Text_NextMsg(line, player, type);
    return true;
}

// The one-line subtitle
// AUTOINJECT
void Text_Update(void) {
    if (Text_UpdateMsg(-1, TXTMSG_SUBTITLE, &SubTitleMsg) && SubTitleMsg != NULL) {
        Sprite_SetText(SubTitleSpr, SubTitleMsg->text);
        SubTitleSpr->maybeEnabled = 0;
        Text_LinkSubtitle(SubTitleSpr);
    }
    if (SubTitleMsg == NULL)
        SubTitleSpr->maybeEnabled = 0xff;
}

// The two-line subtitle: the line wrapped again, its first 0.6 of the width on one sprite and the rest on the other
// AUTOINJECT
void Text_Update2Line(void) {
    if (Text_UpdateMsg(-1, TXTMSG_SUBTITLE_2LINE, &SubTitleMsg) && SubTitleMsg != NULL) {
        ushort width = TextMsgFormats[TXTMSG_SUBTITLE_2LINE].wrapWidth;
        char *rest = SubTitleMsg->text;
        SubTitleStr = Font_WordWrapString(&rest, (int)strlen(rest), SubtitleFormat, Ftol((double)width * 0.6f));
        Txt_LockString(SubTitleStr);
        Sprite_SetParams(SubTitleSpr1, 0, 320, 410, HASHCODE_NONE, SUBTITLE_COLOUR);
        Sprite_SetTextNFmt(SubTitleSpr1, SubTitleStr, SubtitleFormat);
        Text_LinkSubtitle(SubTitleSpr1);
        if (rest == NULL) {
            SubTitleSpr2->maybeEnabled = 0xff;
        } else {
            SubTitleStr2 = Font_WordWrapString(&rest, (int)strlen(rest), SubtitleFormat, width);
            if (SubTitleStr2 != NULL) {
                Txt_LockString(SubTitleStr2);
                Sprite_SetParams(SubTitleSpr2, 0, 320, 430, HASHCODE_NONE, SUBTITLE_COLOUR);
                Sprite_SetTextNFmt(SubTitleSpr2, SubTitleStr2, SubtitleFormat);
                Text_LinkSubtitle(SubTitleSpr2);
            }
        }
    }
    if (SubTitleMsg == NULL) {
        SubTitleSpr1->maybeEnabled = 0xff;
        SubTitleSpr2->maybeEnabled = 0xff;
    }
}

// ---- times as text

// Four buffers in turn, so a caller can hold a few results at once
// XBE_GLOBAL(0x0029ae38, 0x400)
char timer_text[4][0x100];
// XBE_GLOBAL(0x0029b238, 0x1)
uint8_t timer_text_next;

// Hundredths of a second as text: format 1 hh:mm:ss, 2 mm:ss, 3 mm:ss:00, 4 ss:00, and otherwise hh:mm when there
// are hours, else mm:ss. A negative time is 0; the hours wrap at 60. The sum is made on the x87 from a float 0.01,
// which double reproduces for any time below 2^29 hundredths.
// AUTOINJECT
char* Timer_Seconds2String(int hundredths, int format) {
    char *text = timer_text[timer_text_next];
    timer_text_next = (timer_text_next + 1) & 3;
    uint total = Ftol((double)hundredths * 0.01f + 0.5f);
    uint seconds = total % 60;
    uint minutes = total / 60 % 60;
    uint hours = total / 3600 % 60;
    if (hundredths < 0)
        hours = minutes = seconds = 0;
    switch (format) {
        case 1:
            sprintf(text, "%.2d:%.2d:%.2d", hours, minutes, seconds);
            break;
        case 2:
            sprintf(text, "%.2d:%.2d", minutes, seconds);
            break;
        case 3:
            sprintf(text, "%.2d:%.2d:%.2d", minutes, seconds, 0);
            break;
        case 4:
            sprintf(text, "%.2d:%.2d", seconds, 0);
            break;
        default:
            if (hours != 0)
                sprintf(text, "%.2d:%.2d", hours, minutes);
            else
                sprintf(text, "%.2d:%.2d", minutes, seconds);
            break;
    }
    return text;
}
