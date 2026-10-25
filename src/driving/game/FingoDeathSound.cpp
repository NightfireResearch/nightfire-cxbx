#pragma fp_contract(off)

#include "FingoDeathSound.h"
#include "../Vtables.h"   // our copies of the game's vtables

#include "../audio/Bank.h"
#include "../audio/Mix.h"
#include "../engine/UMemory.hpp"

// ---------------------------------------------------------------------------------------------------------------
// AFingoDeath: see FingoDeathSound.h.
// ---------------------------------------------------------------------------------------------------------------

// AOneShotSound's constructor without a velocity: a patch of a bank at a place, looped or not

namespace {

void **const kAFingoDeathVtable = Vtable_AFingoDeath;

const int kWeaponBankSlot = 4;  // (name ours)

// AVoice's destructor as the compiler inlines it: its views', last first.
void DeleteVoice(AVoice *voice) {
    for (int i = 3; i-- > 0;)
        voice->views[i].Destruct();
    UMemory::FastFree(voice, sizeof(AVoice));
}

}  // namespace

// FUNC_AT(0x00129a30)
AFingoDeath* AFingoDeath::Construct() {
    AVehicle::Construct("Player Car", kVehicleSqueals);
    vtable = kAFingoDeathVtable;
    reloadVoice = NULL;
    rechargePercent = 1.0f;
    recharging = false;
    ActivateGun("POV Weapons");
    GameStd::String patch;
    patch.ConstructText("SFX_fingergunfire");
    gun->SetFirePatch(patch);
    gun->shells = true;
    gunLoops = 0;
    ActivateTurret();
    return this;
}

// FUNC_AT(0x001297a0)
void AFingoDeath::Destruct() {
    vtable = kAFingoDeathVtable;
    if (reloadVoice != NULL)
        DeleteVoice(reloadVoice);
    AVehicle::Destruct();
}

// FUNC_AT(0x00129a00)
AFingoDeath* AFingoDeath::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(AFingoDeath));
    return this;
}

// FUNC_AT(0x00129820)
void AFingoDeath::SetRechargePercent(float percent) {
    rechargePercent = percent;
}

// FUNC_AT(0x00129830)
void AFingoDeath::Play(ASoundPlayParams *params) {
    AVehicle::Play(params);
    if (rechargePercent < 1.0f) {
        recharging = true;
        if (reloadVoice == NULL) {
            void *memory = UMemory::FastAlloc(sizeof(AVoice), "AVoice");
            if (memory != NULL) {
                ABank *bank = fgBanks[kWeaponBankSlot];
                int handle = bank->handle;
                int loop = bank->index.Lookup("SFX_fingerReloadLP");
                reloadVoice = static_cast<AVoice *>(memory)->Construct(AMix::Get("Weapons Reload"), handle, loop);
            } else {
                reloadVoice = NULL;
            }
        }
        double level = AMix::Get("Weapons Reload")->GetVolume();
        reloadVoice->Play(params->listener->view, float(level * params->volume),
                          float((rechargePercent + double(0.7f)) * params->pitch * pitch), params->azimuth, 0.0f,
                          0.0f);
    } else if (recharging) {
        recharging = false;
        if (reloadVoice != NULL) {
            DeleteVoice(reloadVoice);
            reloadVoice = NULL;
        }
        void *memory = ABaseSound::OperatorNew(sizeof(AOneShotSound), "AOneShotSound");
        if (memory != NULL) {
            ABank *bank = fgBanks[kWeaponBankSlot];
            static_cast<AOneShotSound *>(memory)->ConstructAt(bank->handle, bank->index.Lookup("SFX_fingerReloadST"), &position,
                                                              1.0f, "Weapons Reload", false);
        }
    }
}
