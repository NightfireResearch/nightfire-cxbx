#include "MenuScript.h"
#include "Process.h"
#include "Window.h"

#include <math.h>
#include <string.h>

// The menus' keyframe scripts on their controls (docs/ui/framework.md) - not the engine's level scripts. A control
// has a list of scripts; a script, keyframes in frame order, each the control's rectangle at its frame and the
// messages it sends as it is passed. Script_RunFrame moves the control between keyframes, by line or by spline.
// PS2: menu_script.cpp.

#pragma fp_contract(off)

// MenuManager_Update's B on a focused control: an event for its script slot 5, never sent as a message
#define SCRIPT_EVENT_BACK 0x5b

// The frames a process lasts when the script has no keyframe after it
#define PROCESS_FRAMES 15

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

static M_SCRIPT* Script_Find(M_CONTROL *control, int id) {
    for (M_SCRIPT *script = (M_SCRIPT *)LinkedList_First(&control->scripts); script != NULL; script = script->next) {
        if (script->id == id)
            return script;
    }
    return NULL;
}

// AUTOINJECT
void Script_PrepareScripting(M_CONTROL *control) {
    if (control == NULL)
        return;
    LinkedList_Init(&control->scripts, 0);
    control->key = NULL;
    control->loopKey = NULL;
    control->loopStep = 0;
    control->step = -1;
    control->frame = -1;
    control->script = NULL;
    Process_Init(control);
}

// The control's script with that id, made if it has none; it becomes the one selected
// AUTOINJECT
M_SCRIPT* Script_AddScript(M_CONTROL *control, int id) {
    if (control == NULL)
        return NULL;
    M_SCRIPT *script = Script_Find(control, id);
    if (script == NULL) {
        script = (M_SCRIPT *)Menu_Malloc(sizeof(M_SCRIPT));
        LinkedList_Add(&control->scripts, &script->prev, script);
        LinkedList_Init(&script->keys, 0);
        script->id = id;
    }
    control->script = script;
    return script;
}

// A copy of the keyframe into the script, in frame order; at a frame the script has, that keyframe is changed instead
// (unless it is fixed: NULL)
// AUTOINJECT
M_KEYFRAME* Script_AddKeyFrame(M_SCRIPT *script, M_KEYFRAME *key) {
    if (script == NULL || key == NULL)
        return NULL;
    for (M_KEYFRAME *k = (M_KEYFRAME *)LinkedList_First(&script->keys); k != NULL; k = k->next) {
        if (key->frame == k->frame) {
            if (k->flags & KEYFRAME_FIXED)
                return NULL;
            k->frame = key->frame;
            k->flags = key->flags;
            k->x = key->x;
            k->y = key->y;
            k->width = key->width;
            k->height = key->height;
            return k;
        }
    }

    M_KEYFRAME *added = (M_KEYFRAME *)Menu_Malloc(sizeof(M_KEYFRAME));
    memcpy(added, key, sizeof(M_KEYFRAME));
    void *after = NULL;
    M_KEYFRAME *k = (M_KEYFRAME *)LinkedList_First(&script->keys);
    if (k != NULL) {
        while (k != NULL && k->frame <= added->frame)
            k = k->next;
        M_KEYFRAME *before = k != NULL ? k->prev : (M_KEYFRAME *)LinkedList_Last(&script->keys);
        if (before != NULL)
            after = LinkedList_Link(&script->keys, before);
    }
    LinkedList_Insert(&script->keys, after, &added->prev, added);
    LinkedList_Init(&added->messages, 0);
    return added;
}

// A copy of the message, to be sent as the keyframe is passed
// AUTOINJECT
M_MESSAGE* Script_AddMessage(M_KEYFRAME *key, M_MESSAGE *msg) {
    if (key == NULL || msg == NULL)
        return NULL;
    M_MESSAGE *added = (M_MESSAGE *)Menu_Malloc(sizeof(M_MESSAGE));
    memcpy(added, msg, sizeof(M_MESSAGE));
    LinkedList_Add(&key->messages, &added->prev, added);
    return added;
}

// AUTOINJECT
M_SCRIPT* Script_Select(M_CONTROL *control, int id) {
    M_SCRIPT *script = Script_Find(control, id);
    if (script != NULL)
        control->script = script;
    return script;
}

// Plays the control's script with that id from its start, or stands it at its last keyframe
// FUNC_AT(0x00096ba0)
bool Script_Play(M_CONTROL *control, int id, char atEnd) {
    M_SCRIPT *script = Script_Find(control, id);
    if (script == NULL)
        return false;
    control->script = script;
    if (atEnd) {
        control->key = (M_KEYFRAME *)LinkedList_Last(&script->keys);
        control->loopKey = (M_KEYFRAME *)LinkedList_Last(&script->keys);
        control->loopStep = script->keys.count;
        control->step = script->keys.count;
        control->frame = control->key->frame;
    } else {
        control->key = (M_KEYFRAME *)LinkedList_First(&script->keys);
        control->loopKey = (M_KEYFRAME *)LinkedList_First(&script->keys);
        control->loopStep = 0;
        control->step = -1;
        control->frame = 0;
    }
    return true;
}

// Plays the script a message has a slot for (M_WIDGET::defaultScripts): a page's entering, leaving and going back
// play its Window's. Whether there was one.
// AUTOINJECT
bool Script_PlayDefault(M_CONTROL *control, uint message) {
    if (control == NULL)
        return false;
    int slot;
    switch (message) {
        case MessageType_ControlCreated:
        case MessageType_PageEnter: slot = 0; break;
        case MessageType_PageLeave:
        case MessageType_GainFocus: slot = 1; break;
        case MessageType_Select:
        case MessageType_BackAtRoot: slot = 2; break;
        case MessageType_Scroll: slot = 3; break;
        case MessageType_LoseFocus: slot = 4; break;
        case SCRIPT_EVENT_BACK: slot = 5; break;
        default: return false;
    }
    if (message == MessageType_PageEnter || message == MessageType_PageLeave || message == MessageType_BackAtRoot) {
        M_CONTROL *window = Window_Get((M_PAGE *)control);
        if (window == NULL)
            return false;
        M_SCRIPT *script = ((M_WIDGET *)window)->defaultScripts[slot];
        if (script == NULL)
            return false;
        Script_Play(window, script->id, false);
        return true;
    }
    M_SCRIPT *script = ((M_WIDGET *)control)->defaultScripts[slot];
    if (script == NULL || control->type == ControlType_Window)
        return false;
    Script_Play(control, script->id, false);
    return true;
}

// Clears bit 8 of the control's flags. Script_RunFrame's loops test the script's own SCRIPT_LOOPING, so a loop
// carries on.
// AUTOINJECT
bool Script_ExitLoop(M_CONTROL *control) {
    if (control == NULL)
        return false;
    control->scriptExecutionFlags &= ~8u;
    return true;
}

// Stops the script a control is playing. The name is shared with the engine's Script_Stop (engine/Script.cpp).
// FUNC_AT(0x00096d70)
void Script_Stop(M_CONTROL *control) {
    if (control != NULL)
        control->key = NULL;
}

// A Catmull-Rom spline through p1 (t 0) and p2 (t 1), unrounded
// AUTOINJECT
double Script_CalculateSpline(int p0, int p1, int p2, int p3, float t) {
    double t2 = (double)t * t;
    double cubic = p1 * 3.0 - p2 * 3.0 + p3 - p0;
    double square = p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3;
    return (cubic * (t2 * t) + square * t2 + (p2 - p0) * (double)t + p1 * 2.0) * 0.5;
}

// From p1 (t 0) to p2 (t 1), unrounded (inline on the Xbox)
static double Script_CalculateLine(int p0, int p1, int p2, int p3, float t) {
    return (p2 - p1) * (double)t + p1;
}

// The keyframes about t (a fraction of the way from keyframe 'key' to the next): the one before, its own, the two
// after, the ends repeated; and how far along it is
static float Script_Span(M_SCRIPT *script, float t, uint *before, uint *key, uint *after, uint *next) {
    uint last = script->keys.count - 1;
    if (t < 0.0f)
        t = 0.0f;
    else if (t > (double)last)
        t = (float)last;
    uint index = t < 0.0f ? 0 : Ftol(t > (double)last ? (double)last : t);
    *after = index + 1 > last ? last : index + 1;
    *before = index - 1 > last ? last : index - 1;
    *next = index + 2 > last ? last : index + 2;
    *key = index;
    return (float)(t - (double)index);
}

#define KEY(n) ((M_KEYFRAME *)LinkedList_GetIndex(&script->keys, n))

// AUTOINJECT
void Script_InterpolateSpline(M_CONTROL *control, M_SCRIPT *script, float t) {
    uint before, key, after, next;
    float along = Script_Span(script, t, &before, &key, &after, &next);
    control->x = Ftol(Script_CalculateSpline(KEY(before)->x, KEY(key)->x, KEY(after)->x, KEY(next)->x, along));
    control->y = Ftol(Script_CalculateSpline(KEY(before)->y, KEY(key)->y, KEY(after)->y, KEY(next)->y, along));
    control->width = Ftol(Script_CalculateSpline(KEY(before)->width, KEY(key)->width, KEY(after)->width,
                                                 KEY(next)->width, along));
    control->height = Ftol(Script_CalculateSpline(KEY(before)->height, KEY(key)->height, KEY(after)->height,
                                                  KEY(next)->height, along));
}

// AUTOINJECT
void Script_InterpolateLine(M_CONTROL *control, M_SCRIPT *script, float t) {
    uint before, key, after, next;
    float along = Script_Span(script, t, &before, &key, &after, &next);
    control->x = Ftol(Script_CalculateLine(KEY(before)->x, KEY(key)->x, KEY(after)->x, KEY(next)->x, along));
    control->y = Ftol(Script_CalculateLine(KEY(before)->y, KEY(key)->y, KEY(after)->y, KEY(next)->y, along));
    control->width = Ftol(Script_CalculateLine(KEY(before)->width, KEY(key)->width, KEY(after)->width,
                                               KEY(next)->width, along));
    control->height = Ftol(Script_CalculateLine(KEY(before)->height, KEY(key)->height, KEY(after)->height,
                                                KEY(next)->height, along));
}

#undef KEY

// Inline on the Xbox
static void Script_DelMessage(M_KEYFRAME *key, M_MESSAGE *msg) {
    if (key == NULL || msg == NULL)
        return;
    LinkedList_Remove(&key->messages, &msg->prev, msg);
    Menu_Free((void **)&msg, 7);
}

// AUTOINJECT
void Script_DelKeyFrame(M_SCRIPT *script, M_KEYFRAME *key) {
    if (script == NULL || key == NULL)
        return;
    M_MESSAGE *msg = (M_MESSAGE *)LinkedList_First(&key->messages);
    while (msg != NULL) {
        M_MESSAGE *next = msg->next;
        Script_DelMessage(key, msg);
        msg = next;
    }
    LinkedList_Remove(&script->keys, &key->prev, key);
    Menu_Free((void **)&key, 6);
}

// AUTOINJECT
void Script_DelScript(M_CONTROL *control, M_SCRIPT *script) {
    if (control == NULL || script == NULL)
        return;
    M_KEYFRAME *key = (M_KEYFRAME *)LinkedList_First(&script->keys);
    while (key != NULL) {
        M_KEYFRAME *next = key->next;
        Script_DelKeyFrame(script, key);
        key = next;
    }
    LinkedList_Remove(&control->scripts, &script->prev, script);
    Menu_Free((void **)&script, 5);
}

// One frame of the control's script: its rectangle from the keyframes about it; at a keyframe, its loop and its
// messages (from MESSAGE_PROCESS on, a process lasting until the next keyframe)
// AUTOINJECT
void Script_RunFrame(M_MANAGER *mgr, M_CONTROL *control) {
    Process_RunFrame(control);
    M_KEYFRAME *key = control->key;
    if (key == NULL)
        return;
    M_KEYFRAME *prev = key->prev != NULL ? key->prev : key;
    float t;
    if (key->frame == prev->frame)
        t = (float)(control->step + 1);
    else
        t = (float)((uint)(control->frame - prev->frame) / (double)(key->frame - prev->frame) + control->step);
    if (control->script->flags & SCRIPT_SPLINE)
        Script_InterpolateSpline(control, control->script, t);
    else
        Script_InterpolateLine(control, control->script, t);

    if ((int)key->frame > control->frame) {
        control->frame++;
        return;
    }
    if (key->flags & KEYFRAME_LOOP_START) {
        control->loopStep = control->step;
        control->loopKey = control->key;
        control->script->flags |= SCRIPT_LOOPING;
    }
    if ((key->flags & KEYFRAME_LOOP_BACK) && (control->script->flags & SCRIPT_LOOPING)) {
        control->scriptExecutionFlags |= 0x10;
        control->step = control->loopStep;
        control->key = control->loopKey;
        control->frame = control->loopKey->frame + 1;
        return;
    }

    control->key = key->next;
    if (key->next != NULL)
        control->step++;
    else
        control->step = 0;
    for (M_MESSAGE *msg = (M_MESSAGE *)LinkedList_First(&key->messages); msg != NULL; msg = msg->next) {
        if (msg->type >= MESSAGE_PROCESS) {
            short frames = control->key != NULL ? control->key->frame - control->frame - 1 : PROCESS_FRAMES;
            M_CONTROL *target = msg->target == MESSAGE_TO_SELF
                                    ? control
                                    : (M_CONTROL *)__Menu_Send(mgr->index, (HASHCODE)msg->target, MessageType_GetControl, 0, 0);
            Process_Create(target, msg, frames, 0);
        } else {
            intptr_t arg1 = msg->arg1 == MESSAGE_LAST_RESULT ? control->lastScriptResult : msg->arg1;
            if (msg->target == MESSAGE_TO_MANAGER)
                control->lastScriptResult = __Menu_SendMessage(&mgr->control, msg->type, arg1, msg->arg2);
            else if (msg->target == MESSAGE_TO_SELF)
                control->lastScriptResult = __Menu_SendMessage(control, msg->type, arg1, msg->arg2);
            else
                control->lastScriptResult = __Menu_Send(mgr->index, (HASHCODE)msg->target, msg->type, arg1, msg->arg2);
        }
    }
    control->frame++;
}

// AUTOINJECT
void Script_DelAllScripts(M_CONTROL *control) {
    if (control == NULL)
        return;
    control->script = NULL;
    control->key = NULL;
    M_SCRIPT *script = (M_SCRIPT *)LinkedList_First(&control->scripts);
    while (script != NULL) {
        M_SCRIPT *next = script->next;
        Script_DelScript(control, script);
        script = next;
    }
    Process_Destroy(control);
}
