"""Writes driving-symbol-matching/results/structs/action-ui.json for ghidra/NightfireStructs.py: the menu system's
enums (MessageType and CONTROL_TYPE, from docs/ui/messages.json), its structures as src/action/ui/ui.h and Menu.h
have them (not M_SCROLL, whose other fields Ghidra has and ui.h does not), and the prototypes the menu pass
found wrong in Ghidra.

  python tools/ui/make_ghidra_json.py
"""
import json
import os

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
messages = json.load(open(os.path.join(REPO, 'docs', 'ui', 'messages.json')))


def f(offset, type_, name, comment=None):
    d = {'offset': offset, 'type': type_, 'name': name}
    if comment:
        d['comment'] = comment
    return d


HANDLER = [{'name': 'managerNum', 'type': 'uchar'}, {'name': 'control', 'type': 'M_CONTROL *'},
           {'name': 'hashcode', 'type': 'uint'}, {'name': 'message', 'type': 'uint'},
           {'name': 'arg1', 'type': 'int'}, {'name': 'arg2', 'type': 'int'}]

doc = {
    'program': 'default.xbe',
    'comment': 'Menu system (docs/ui): message and control type enums, control/page/manager layouts, prototype fixes',
    'enums': [
        {'name': 'MessageType', 'size': 4, 'comment': 'Menu messages (docs/ui/messages.md); names invented',
         'values': [{'name': v['name'], 'value': v['value'], 'comment': v['comment'][:250]} for v in messages['MessageType']]},
        {'name': 'CONTROL_TYPE', 'size': 1, 'comment': 'M_CONTROL.type (docs/ui/messages.md; ControlType in ui.h)',
         'values': [{'name': v['name'], 'value': v['value'], 'comment': v['comment']} for v in messages['ControlType']]},
    ],
    'types': [
        {'name': 'M_CONTROL', 'size': 0x7c, 'comment': 'The part every control, page and the manager share',
         'fields': [f(0x0, 'M_CONTROL *', 'next'), f(0x4, 'M_CONTROL *', 'prev'),
                    f(0xc, 'M_CONTROL *', 'parent', 'the page for a control, the manager for a page'),
                    f(0x10, 'HASHCODE', 'extraControlHash', 'page only'), f(0x18, 'HASHCODE', 'hashcode'),
                    f(0x1c, 'uint', 'drawState', '0x10 normal, 0x20 focused, 0x40 pressed, 0x80 under the cursor'),
                    f(0x20, 'int', 'id', 'SetId/GetId; __Menu_SendEx picks controls by it'),
                    f(0x24, 'uint', 'framesFocused'), f(0x28, 'LINKEDLIST', 'scripts'),
                    f(0x38, 'void *', 'selectedScript'), f(0x3c, 'void *', 'scriptPlaying'),
                    f(0x50, 'int', 'lastScriptResult'), f(0x58, 'uint', 'scriptExecutionFlags'),
                    f(0x5c, 'LINKEDLIST', 'processes'), f(0x70, 'short', 'x'), f(0x72, 'short', 'y'),
                    f(0x74, 'short', 'width'), f(0x76, 'short', 'height'), f(0x78, 'ushort', 'depth'),
                    f(0x7a, 'byte', 'state', 'SetState: bit 0 hidden, bit 1 not selectable'),
                    f(0x7b, 'CONTROL_TYPE', 'type')]},
        {'name': 'M_WIDGET', 'size': 0x98, 'comment': 'An ordinary control: M_CONTROL, then its default scripts',
         'fields': [f(0x0, 'M_CONTROL', 'control'), f(0x7c, 'void *[7]', 'defaultScripts')]},
        {'name': 'M_PAGE', 'size': 0xd0, 'comment': 'A menu page',
         'fields': [f(0x0, 'M_CONTROL', 'control'), f(0x94, 'LINKEDLIST', 'controls'), f(0xa4, 'void *[2]', 'pageLink'),
                    f(0xac, 'M_CONTROL *', 'focused'), f(0xb0, 'M_CONTROL *[4]', 'playerFocus'),
                    f(0xc0, 'int', 'navigationMode'), f(0xc4, 'HASHCODE', 'backgroundMovie'),
                    f(0xc8, 'int', 'framesShown'), f(0xcc, 'int', 'inputDelay')]},
        {'name': 'M_MANAGER', 'size': 0x1d8, 'comment': 'A menu instance: manager[managerNum] at 0x25f1d0',
         'fields': [f(0x0, 'M_CONTROL', 'control'), f(0x94, 'sprite *', 'cursorSprite'), f(0x98, 'LINKEDLIST', 'pages'),
                    f(0xa8, 'STACKINFO', 'stack'), f(0xb0, 'int[64]', 'stackMem'),
                    f(0x1b0, 'HASHCODE', 'currentMovie'), f(0x1b4, 'uint', 'status'), f(0x1b8, 'HASHCODE', 'startPage'),
                    f(0x1bc, 'M_PAGE *', 'currentPage'), f(0x1c0, 'M_PAGE *', 'underPage'), f(0x1c4, 'M_PAGE *', 'sidePage'),
                    f(0x1c8, 'GameActions_tag', 'closeAction'), f(0x1cc, 'short', 'whichPlayer'),
                    f(0x1ce, 'byte', 'startPlayer'), f(0x1cf, 'byte', 'field_0x1cf'), f(0x1d0, 'byte', 'index'),
                    f(0x1d1, 'bool', 'exists'), f(0x1d2, 'bool', 'takingInput'), f(0x1d3, 'bool', 'overlayMode'),
                    f(0x1d4, 'bool', 'inputLocked'), f(0x1d5, 'bool', 'didPauseAudio')]},
        {'name': 'M_MESSAGE', 'size': 0x18, 'comment': 'A process or delayed message',
         'fields': [f(0xc, 'int', 'type'), f(0x10, 'int', 'arg1'), f(0x14, 'int', 'arg2')]},
        {'name': 'MENU_LS', 'size': 0x48, 'comment': 'The codename load/save in progress (ls, 0x17d540)',
         'fields': [f(0x0, 'uint', 'busy'), f(0x4, 'uint', 'numCodenames'), f(0x8, 'HASHCODE', 'returnPage'),
                    f(0xc, 'undefined4', 'field3_0xc'), f(0x10, 'uint', 'slot', '999 = a new codename'),
                    f(0x14, 'undefined4', 'field5_0x14'), f(0x18, 'char[32]', 'codename'),
                    f(0x39, 'byte', 'operation', '0 load, 1 save'), f(0x3a, 'byte', 'field9_0x3a'),
                    f(0x3b, 'byte', 'field10_0x3b'), f(0x3d, 'byte', 'field12_0x3d'), f(0x40, 'byte', 'doMiniMission'),
                    f(0x41, 'byte', 'relatedToMainMenuSound'), f(0x42, 'byte', 'field17_0x42')]},
        {'name': 'MPJoinSlot', 'size': 0x10, 'comment': 'A controller\'s place on the join page (4 at 0x245240)',
         'fields': [f(0x0, 'bool', 'joined'), f(0x1, 'bool', 'ready'), f(0x4, 'uint', 'team', '0 Phoenix, 1 MI6'),
                    f(0x8, 'uint', 'skin'), f(0xc, 'uint', 'field_0xc')]},
    ],
    'prototypes': [
        {'address': '0x757d0', 'calling_convention': '__cdecl', 'return': 'void',
         'params': [{'name': 'managerNum', 'type': 'uint'}, {'name': 'param_2', 'type': 'ushort'}, {'name': 'param_3', 'type': 'byte'}]},
        {'address': '0x8ca10', 'calling_convention': '__cdecl', 'return': 'bool', 'params': HANDLER},   # P_CNNAME_Handler
        {'address': '0x8ded0', 'calling_convention': '__cdecl', 'return': 'bool', 'params': HANDLER},   # P_CREDITS_Handler
        {'address': '0x76470', 'calling_convention': '__cdecl', 'return': 'CreditsEntry *',
         'params': [{'name': 'numLines_out', 'type': 'uint *'}]},
        {'address': '0x7cfb0', 'calling_convention': '__cdecl', 'return': 'ulonglong',
         'params': [{'name': 'bonus', 'type': 'ulonglong'}, {'name': 'objId', 'type': 'uint'}, {'name': 'count', 'type': 'byte'}]},
        {'address': '0x7d110', 'return': 'ulonglong', 'return_only': True},   # the caller tests EDX:EAX
        {'address': '0xc6360', 'return': 'int', 'return_only': True},         # SFXMusicGetVolume
    ],
}

out = os.path.join(REPO, 'driving-symbol-matching', 'results', 'structs', 'action-ui.json')
json.dump(doc, open(out, 'w'), indent=1)
print('wrote', out, '-', len(doc['enums']), 'enums,', len(doc['types']), 'types,', len(doc['prototypes']), 'prototypes')
