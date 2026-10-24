// The entries through which the original's code calls the functions in engine/Script.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/Script.h"

// Not in a header
struct InterpResult;
void Script_InterpLinear(int *keys, float *weights, KEYED_POSROT *base, InterpResult *res, char isCamera);
void Script_InterpSpline(int *keys, float *weights, KEYED_POSROT *base, InterpResult *res, char isCamera);
void Script_ConcatParents(SCRIPTINFO *scriptInfo, SSTREAM *stream, _MATRIX *out);

// The original takes the count in EAX and the keys in EBX, the frame on the stack.
// AUTOLTCG
void __declspec(naked) Asm_Script_FindKey(void) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        push ebx
        call Script_FindKey
        add esp, 12
        ret
    }
}

// The original takes the keys in ECX, the weights in EBX and the base key in EDX; the result and a flag on the stack.
// AUTOLTCG
void __declspec(naked) Asm_Script_InterpLinear(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push edx
        push ebx
        push ecx
        call Script_InterpLinear
        add esp, 20
        ret
    }
}

// The original takes the keys in EAX, the weights in ESI and the base key in ECX; the result and a flag on the stack.
// AUTOLTCG
void __declspec(naked) Asm_Script_InterpSpline(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ecx
        push esi
        push eax
        call Script_InterpSpline
        add esp, 20
        ret
    }
}

// The original takes the mode in EAX, firstKey in DX and lastKey in CX, the rest on the stack.
// AUTOLTCG
void __declspec(naked) Asm_Script_GetInterp(void) {
    _asm {
        movzx ecx, cx
        push ecx
        movzx edx, dx
        push edx
        movzx eax, ax
        push eax
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        call Script_GetInterp
        add esp, 40
        ret
    }
}

// The original takes the stream in EAX and the script in EDI, the matrix on the stack.
// AUTOLTCG
void __declspec(naked) Asm_Script_ConcatParents(void) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        push edi
        call Script_ConcatParents
        add esp, 12
        ret
    }
}

// The original takes the camera block in EAX.
// AUTOLTCG
void __declspec(naked) Asm_Script_UpdateCamera(void) {
    _asm {
        push eax
        call Script_UpdateCamera
        add esp, 4
        ret
    }
}

// The original takes the stream in EAX and the script in ESI (link-time code generation; pops nothing). Only the
// original script interpreter calls it; C callers call Script_CameraStart.
// AUTOLTCG
void __declspec(naked) Asm_Script_CameraStart(void) {
    _asm {
        push eax
        push esi
        call Script_CameraStart
        add esp, 8
        ret
    }
}

// The original takes the stream on the stack, the hashcode in EDI and the placement type in BL.
// AUTOLTCG
void __declspec(naked) Asm_Script_CreateEntity(void) {
    _asm {
        movzx eax, bl
        push eax
        push edi
        push dword ptr [esp + 12]
        call Script_CreateEntity
        add esp, 12
        ret
    }
}

// The original takes the script on the stack and the stream in EDI.
// AUTOLTCG
void __declspec(naked) Asm_Script_LightStart(void) {
    _asm {
        push edi
        push dword ptr [esp + 8]
        call Script_LightStart
        add esp, 8
        ret
    }
}

// The original takes the stream in EDI. Script_Run also pushes the script, which it does not read.
// AUTOLTCG
void __declspec(naked) Asm_Script_SpriteStart(void) {
    _asm {
        push edi
        call Script_SpriteStart
        add esp, 4
        ret
    }
}

// The original takes the script in EDI and the stream in ESI.
// AUTOLTCG
void __declspec(naked) Asm_Script_StreamEnd(void) {
    _asm {
        push esi
        push edi
        call Script_StreamEnd
        add esp, 8
        ret
    }
}

// The original takes the script on the stack and the stream in EDI.
// AUTOLTCG
void __declspec(naked) Asm_Script_SoundStart(void) {
    _asm {
        push edi
        push dword ptr [esp + 8]
        call Script_SoundStart
        add esp, 8
        ret
    }
}

// The original takes the script on the stack and the stream in ESI.
// AUTOLTCG
void __declspec(naked) Asm_Script_SubScriptStart(void) {
    _asm {
        push esi
        push dword ptr [esp + 8]
        call Script_SubScriptStart
        add esp, 8
        ret
    }
}

// The original takes the script in ESI and the stream on the stack.
// AUTOLTCG
void __declspec(naked) Asm_Script_EventHandler(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call Script_EventHandler
        add esp, 8
        ret
    }
}

// The original takes the script on the stack and the stream in EDI.
// AUTOLTCG
void __declspec(naked) Asm_Script_AnimStart(void) {
    _asm {
        push edi
        push dword ptr [esp + 8]
        call Script_AnimStart
        add esp, 8
        ret
    }
}

// The original takes the script in EBX. Script_Update is its only caller.
// AUTOLTCG
void __declspec(naked) Asm_Script_Run(void) {
    _asm {
        push ebx
        call Script_Run
        add esp, 4
        ret
    }
}
