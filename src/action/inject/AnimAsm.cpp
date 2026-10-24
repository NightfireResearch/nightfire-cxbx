// The entries through which the original's code calls the functions in engine/Anim.cpp: each takes the arguments from
// the registers the original takes them in and passes them on. Only the injected build has them.
#include "../engine/Anim.h"

// Not in a header
void loadAnimFileCapturingSize(MallocFlags tag, HASHCODE hashcode);
void AnimProcessScriptData(HASHCODE hashcode);
sAnimSeq_tag* AnimSeqNew(uint size, uint tag);
sAnimRawSeq_tag* AnimSeqGetInfo(sAnimRawSeq_tag *raw, HASHCODE hashcode, sAnimSeq_tag *seq);
sAnimSeq_tag* AnimSeqSet(sAnimScript_tag *script, HASHCODE hashcode, obj_tag *obj, AnimObj *animObj,
                         sAnimRawSeq_tag *raw, int start, int slot);
sAnimScript_tag* AnimScriptNew(obj_tag *obj, sAnimScript_tag *after);
void AnimScriptInit(obj_tag *obj, sAnimScript_tag *script, HASHCODE hashcode);
double AnimScriptEnd(sAnimScript_tag *script, obj_tag *obj, AnimObj *animObj, float frameIn);
double AnimDistanceTableDistanceToFrame2(obj_tag *obj, sAnimScript_tag *script, AnimObj *animObj);
void AnimScriptTick(AnimObj *animObj, obj_tag *obj, sAnimScript_tag *script);
void AnimFacialUpdate(AnimObj *animObj, obj_tag *obj);
void AnimFacialBlend(obj_tag *obj, AnimObj *animObj, sAnimSkin_tag *skin);
void AnimFrameBlend(float *out, float *a, float *b, obj_tag *obj, int numBones, float t, uchar *mask);
void AnimFrameSet(sAnimSeq_tag *seq, sAnimSkin_tag *skin, obj_tag *obj, float fraction);
void AnimSeqTick(sAnimScript_tag *script, sAnimSeq_tag *seq, obj_tag *obj, AnimObj *animObj);
HASHCODE FixupSkinHashcode(HASHCODE skin);
sAnimSet_tag* AnimTagAllocAndGetUnique(sAnimSet_tag *after);
void AnimSetInit(sAnimSet_tag *set, HASHCODE *list, obj_tag *obj, float a, float b);

// The index in EAX
// AUTOLTCG
void __declspec(naked) Asm_AnimSleeveGetEntity(void) {
    _asm {
        push eax
        call AnimSleeveGetEntity
        add esp, 4
        ret
    }
}

// The tag in EAX, the hashcode in ESI
// AUTOLTCG
void __declspec(naked) Asm_loadAnimFileCapturingSize(void) {
    _asm {
        push esi
        movsx eax, ax
        push eax
        call loadAnimFileCapturingSize
        add esp, 8
        ret
    }
}

// The hashcode in EAX
// AUTOLTCG
void __declspec(naked) Asm_AnimProcessScriptData(void) {
    _asm {
        push eax
        call AnimProcessScriptData
        add esp, 4
        ret
    }
}

// The original takes hashcode in EAX (link-time code generation; pops nothing, preserves EBX/ESI/EDI/EBP).
// AUTOLTCG
void __declspec(naked) Asm_AnimProcessSkinData(void) {
    _asm {
        push eax
        call AnimProcessSkinData
        add esp, 4
        ret
    }
}

// The size in ESI, the tag on the stack
// AUTOLTCG
void __declspec(naked) Asm_AnimSeqNew(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call AnimSeqNew
        add esp, 8
        ret
    }
}

// The file in EAX, the hashcode in ECX, the sequence in ESI
// AUTOLTCG
void __declspec(naked) Asm_AnimSeqGetInfo(void) {
    _asm {
        push esi
        push ecx
        push eax
        call AnimSeqGetInfo
        add esp, 12
        ret
    }
}

// The original takes the axes in ECX and the hashcode in EDX
// AUTOLTCG
void __declspec(naked) Asm_AnimDistanceTableCreate(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 12]
        push edx
        push ecx
        call AnimDistanceTableCreate
        add esp, 12
        pop edx
        pop ecx
        ret
    }
}

// The original takes the hashcode in ESI
// AUTOLTCG
void __declspec(naked) Asm_AnimDistanceTableGet(void) {
    _asm {
        push ecx
        push edx
        push esi
        call AnimDistanceTableGet
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// The script in ECX, the hashcode in EBX
// AUTOLTCG
void __declspec(naked) Asm_AnimSeqSet(void) {
    _asm {
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push ebx
        push ecx
        call AnimSeqSet
        add esp, 28
        ret
    }
}

// The object in EAX, the script to follow on the stack
// AUTOLTCG
void __declspec(naked) Asm_AnimScriptNew(void) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        call AnimScriptNew
        add esp, 8
        ret
    }
}

// The object in EAX, the script in ESI, the hashcode in EDI
// AUTOLTCG
void __declspec(naked) Asm_AnimScriptInit(void) {
    _asm {
        push edi
        push esi
        push eax
        call AnimScriptInit
        add esp, 12
        ret
    }
}

// The script in ESI; the frame comes back in ST0
// AUTOLTCG
void __declspec(naked) Asm_AnimScriptEnd(void) {
    _asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push esi
        call AnimScriptEnd
        add esp, 16
        ret
    }
}

// The object in EAX, the script in EBX, the anim object in EDI; the frames come back in ST0
// AUTOLTCG
void __declspec(naked) Asm_AnimDistanceTableDistanceToFrame2(void) {
    _asm {
        push edi
        push ebx
        push eax
        call AnimDistanceTableDistanceToFrame2
        add esp, 12
        ret
    }
}

// The anim object in ECX
// AUTOLTCG
void __declspec(naked) Asm_AnimScriptTick(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ecx
        call AnimScriptTick
        add esp, 12
        ret
    }
}

// The anim object in ESI
// AUTOLTCG
void __declspec(naked) Asm_AnimFacialUpdate(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call AnimFacialUpdate
        add esp, 8
        ret
    }
}

// The object in EAX, the anim object in ESI
// AUTOLTCG
void __declspec(naked) Asm_AnimFacialBlend(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        push eax
        call AnimFacialBlend
        add esp, 12
        ret
    }
}

// out in ECX, a in EDX, b in EAX
// AUTOLTCG
void __declspec(naked) Asm_AnimFrameBlend(void) {
    _asm {
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push eax
        push edx
        push ecx
        call AnimFrameBlend
        add esp, 28
        ret
    }
}

// The sequence in ESI, the skin in EDI
// AUTOLTCG
void __declspec(naked) Asm_AnimFrameSet(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push edi
        push esi
        call AnimFrameSet
        add esp, 16
        ret
    }
}

// The script in EAX, the sequence in ECX, the object in EDX, the anim object in EBX
// AUTOLTCG
void __declspec(naked) Asm_AnimSeqTick(void) {
    _asm {
        push ebx
        push edx
        push ecx
        push eax
        call AnimSeqTick
        add esp, 16
        ret
    }
}

// The skin in EAX
// FUNC_AT(0x00011350)
void __declspec(naked) Asm_FixupSkinHashcode(void) {
    _asm {
        push eax
        call FixupSkinHashcode
        add esp, 4
        ret
    }
}

// The object in ESI; the height comes back in ST0
// AUTOLTCG
void __declspec(naked) Asm_AnimObjectInitialPose(void) {
    _asm {
        push esi
        call AnimObjectInitialPose
        add esp, 4
        ret
    }
}

// The set to follow in ESI
// AUTOLTCG
void __declspec(naked) Asm_AnimTagAllocAndGetUnique(void) {
    _asm {
        push esi
        call AnimTagAllocAndGetUnique
        add esp, 4
        ret
    }
}

// The set in ECX, the list in EAX
// AUTOLTCG
void __declspec(naked) Asm_AnimSetInit(void) {
    _asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push eax
        push ecx
        call AnimSetInit
        add esp, 20
        ret
    }
}
