// The entries through which the original's code calls the functions in game/obj/Sensor.cpp: each takes the arguments
// from the registers the original takes them in and passes them on. Only the injected build has them.
#include "../game/obj/Sensor.h"

// AUTOLTCG
void __declspec(naked) Asm_Sensor_SetBeam(void) {
    // Assembly wrapper for custom calling convention
    // Original game code calls with: targetObj on stack, sensorObj in EDI, sensor in ESI
    _asm {
        push esi                    // Push sensor from ESI
        push edi                    // Push sensorObj from EDI
        push dword ptr [esp + 12]   // Push targetObj from stack
        call Sensor_SetBeam
        add esp, 12                 // Clean up stack (3 params)
        ret
    }
}
