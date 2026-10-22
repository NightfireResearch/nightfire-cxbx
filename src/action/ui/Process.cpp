// A control's processes: messages that run for some frames, a colour fade among them (PS2: menu_manager.cpp)

#include "Process.h"
#include "MenuCommon.h"

#pragma fp_contract(off)

// AUTOINJECT
void Process_Init(M_CONTROL *control) {
    if (control == NULL)
        return;
    LinkedList_Init(&control->processes, 0);
}

// A process of the message's, for frames; a fade starts from the control's colour
// AUTOINJECT
void Process_Create(M_CONTROL *control, M_MESSAGE *msg, uint frames, int unused) {
    if (control == NULL || msg == NULL)
        return;
    M_PROCESS *process = (M_PROCESS *)Menu_MallocXbox(sizeof(M_PROCESS), XBOX_SIZEOF(M_PROCESS));
    process->frame = 0;
    process->frames = frames;
    process->type = msg->type;
    process->colour = msg->arg1;
    process->tint = msg->arg2;
    if (process->type == MESSAGE_PROCESS) {
        process->from = __Menu_SendMessage(control, MessageType_GetColour, 0, 0);
        process->unknown1c = 0;
    }
    LinkedList_Add(&control->processes, &process->prev, process);
}

// The next is read from the process just freed
// AUTOINJECT
void Process_Destroy(M_CONTROL *control) {
    M_PROCESS *process = (M_PROCESS *)LinkedList_First(&control->processes);
    while (process != NULL) {
        LinkedList_Remove(&control->processes, &process->prev, process);
        M_PROCESS *freed = process;
        Menu_Free((void **)&freed, 0xb);
        process = process->next;
    }
}

// One byte of a fade: from towards to by the share of the frames run
static uchar Process_FadeByte(uint from, uint to, double frames, double frame) {
    int change = (int)(to - from);
    return Ftol(change / frames * frame + (int)from);
}

// A frame of the control's processes, and of its label's: a fade sends the colour it has reached; a process that has
// run its frames sends its colour and ends
// AUTOINJECT
void Process_RunFrame(M_CONTROL *control) {
    switch (control->type) {
        case ControlType_Button:
            Process_RunFrame(&((M_BUTTON *)control)->label.control);
            break;
        case ControlType_Radio:
            Process_RunFrame(&((M_RADIO *)control)->label.control);
            break;
        case ControlType_Checkbox:
            Process_RunFrame(&((M_CHECKBOX *)control)->label.control);
            break;
        case ControlType_Combo:
            Process_RunFrame(&((M_COMBO *)control)->label.control);
            break;
        case ControlType_Spin:
            Process_RunFrame(&((M_SPIN *)control)->label.control);
            break;
    }

    M_PROCESS *process = (M_PROCESS *)LinkedList_First(&control->processes);
    while (process != NULL) {
        M_PROCESS *next = process->next;
        if (process->type == MESSAGE_PROCESS) {
            uint from = process->from;
            uint to = process->colour;
            double frames = process->frames;
            double frame = process->frame;
            uint colour = Process_FadeByte(from >> 24 & 0xff, to >> 24, frames, frame);
            colour = colour << 8 | Process_FadeByte(from >> 16 & 0xff, to >> 16 & 0xff, frames, frame);
            colour = colour << 8 | Process_FadeByte(from >> 8 & 0xff, to >> 8 & 0xff, frames, frame);
            colour = colour << 8 | Process_FadeByte(from & 0xff, to & 0xff, frames, frame);
            __Menu_SendMessage(control, MessageType_SetColour, colour, process->tint > 0);
        }
        process->frame++;
        if (process->frame >= process->frames) {
            __Menu_SendMessage(control, MessageType_SetColour, process->colour, process->tint > 0);
            LinkedList_Remove(&control->processes, &process->prev, process);
            Menu_Free((void **)&process, 0xb);
        }
        process = next;
    }
}
