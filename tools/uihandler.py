# From Handler_HandleMessage
mapping = {
    0x40000009: "P_START_Handler",
    # TODO: Non-contiguous block of crap
    0x4000000c: "P_NIS_Handler",
    0x40000012: "P_MPOPTIONS_Handler",
    0x40000013: "P_MPMAP_Handler",
    0x40000014: "P_MPRULES_Handler",
    0x40000017: "P_MPPLAYERMODS_Handler",
    0x40000019: "P_MPJOIN_Handler",
    0x4000001a: "P_MPSCENARIO_Handler",
    0x4000001b: "P_CNSELECT_Handler",
    0x4000001c: "P_NFMAP_Handler",
    0x4000001d: "P_CNMENU_Handler",
    0x40000020: "P_CNNAME_Handler",
    0x40000022: "P_CNCONTROLS_Handler",
    0x40000023: "P_NFDFCTY_Handler",
    0x40000025: "P_NFSELECT_Handler",
    0x40000027: "P_MPBOTS_Handler",
    0x40000028: "P_MPENVIROMODS_Handler",
    0x4000002b: "P_DOSSIER_Handler",
    0x4000002c: "P_MPBOTSETUP_Handler",
    0x4000002d: "P_CNOPTIONS_Handler",
    0x4000002e: "P_CNMPOPTIONS_Handler",
    0x40000030: "P_CREDITS_Handler",
    0x40000031: "P_CNAVOPTIONS_Handler",
    0x40000032: "P_INTRO_Handler",
    0x40000033: "P_MPDEBRIEFING_Handler",
    0x40000034: "P_LANGUAGE_Handler",
    0x40000035: "P_ATTRACT_Handler",
    0x40000036: "P_NFRESULTS_Handler",
    0x40000037: "P_NFSTATS_Handler",
    0x40000038: "P_NFBONUS_Handler",
    0x4000003a: "P_DSRECORDS_Handler",
    0x4000003b: "P_DSREWARDS_Handler",
    0x4000003c: "P_DSGADGETS_Handler",
    0x4000003d: "P_DSWEAPONS_Handler",
    0x4000003f: "P_MPBOTCHOOSE_Handler",
    0x40000042: "P_ENDMISSION_Handler",
    0x40000043: "P_ESTHERO_Handler",
    0x40000044: "P_TWEAKS_Handler",
    0x40000046: "P_TWEAKS2_Handler",
    # TODO: Non-contiguous block of crap
    0x40000049: "P_MPCONFIRM_Handler",
    0x4000004a: "P_PARISENUM_Handler",
    0x4000004b: "P_PAUSE_Handler",
    0x4000004c: "P_CHEATMEDAL_Handler",
    0x4000004e: "P_TRAILER_Handler",
    0x4000004f: "P_FMVTEST_Handler",
    0x40000050: "P_FMVPLAYER_Handler",
    0x40000051: "P_MPSETUP_Handler",
    0x40000053: "P_WINGAME_Handler",
    0x40000002: "P_MAIN_Handler",
    # Switch to controls rather than pages?
    0x10000002: "C_GONIGHTFIRE_Handler",
    0x10000003: "C_GOMULTIPLAYER_Handler",
    0x10000004: "C_GOCODENAMES_Handler",
    # TODO: Non-contiguous block of crap
    0x10000006: "C_SBMPMAP_Handler",
    0x1000000e: "C_SBNFMAP_Handler",
    0x10000010: "C_LBPMMAP_Handler",
    0x10000012: "C_CHCHWS_Handler",
    0x10000015: "C_CHCHDRONES_Handler",
    0x10000016: "C_CHCHBLIND_Handler",
    0x10000017: "C_CHCHDEBUG_Handler",
    0x10000018: "C_CHCHZEROG_Handler",
    0x10000019: "C_CHCHFLY_Handler",
    0x1000001a: "C_CHCHCOORDS_Handler",
    0x1000001b: "C_CHCHMUSIC_Handler",
    0x1000001c: "C_CHCHHUD_Handler",
    0x10000025: "C_RBCONTROL_Handler",
    0x10000028: "C_GCPAUSE_Handler",
    0x1000003e: "C_NIS_Handler",
    0x10000074: "C_KEYBOARD_Handler",
    0x1000009c: "C_SBMPSCEN_Handler",
    0x100000ba: "C_KEYPAD_Handler",
    0x100000bd: "C_CHCHDUMMY_Handler",
    0x100000bf: "C_SBNFDFCTY_Handler",
    0x100000c7: "C_CHCHALLOWFREEZE_Handler",
    0x100000e5: "C_SBNFCN_Handler",
    # TODO: Default case - CHCHWEAP?
    0x10000173: "C_SBDSWPSCROLL_Handler", # Dossier Weapon Scroll?
    0x10000174: "C_RBDSRECORDS_Handler",
    # TODO: Non-contiguous block of crap
    0x1000017a: "C_RBDSREWARDS_Handler",
    0x10000193: "C_SBMPBTCHOOSE_Handler",
    0x1000019f: "C_RBMPSTART_Handler",
    0x100001a1: "C_RBMPSETUP_Handler",
    # TODO: Jump
    0x100001f7: "C_CHCHBRIGHT_Handler",
    0x10000216: "C_CHCHUNLOCK_Handler",
    0x1000021d: "C_CHCHCONTROLS_Handler",
    0x1000022a: "C_RBMPFINISH_Handler",
    0x1000022b: "C_RBMPCNAME_Handler",
    0x100000e8: "C_CHCHHEALTH_Handler",
    # TODO: Non-contiguous block of crap
    0x100000eb: "C_SBCNSELECT_Handler",
    0x100000f4: "C_SBMPOPTIONS_Handler",
    0x100000fc: "C_LBERROPTIONS_Handler",
    0x10000100: "C_SBCNOPTIONS_Handler",
    0x10000101: "C_CHCHDRAWALL_Handler",
    0x1000010c: "C_SBDOSSIER_Handler", # TODO: What does C_SBDOSSIER_Handler do when sending 0x1000010a/0x1000010d/...? Subpages?
    0x10000114: "C_SBBOTS_Handler",
    0x10000120: "C_LBMSGOPTIONS_Handler",
    0x1000015d: "C_MPDBG_Handler",
    0x1000015e: "C_LANGUAGE_Handler",
    0x10000169: "C_CHCHLOCKUP_Handler",
    # Default case - SBDSGTSCROLL. Confirmed against PS2.
    0x0000016f: "C_SBDSGTSCROLL_Handler", # Dossier Gadgets Scroll?
}

implemented = ["C_SBDOSSIER_Handler"]

def generate_handler_switch():
    output = """
#include "../helpers.h"
#include "ui.h"

#include <stdio.h>

"""

    for _, name in mapping.items():
        if not name in implemented:
            output += f"// AUTOGEN\nuint32_t {name}(uchar param_1, M_CONTROL *param_2, uint hashcode, uint param_3, int param_4, int param_5);\n"


    output += """
// AUTOINJECT
long Handler_HandleMessage(uchar param_1, M_CONTROL *param_2, uint param_3, int param_4, int param_5)
{
    uint hashcode = param_2->hashcode;
    switch(hashcode) {
"""

    for addr, name in mapping.items():
        output += f"        case 0x{addr:08x}: return {name}(param_1, param_2, hashcode, param_3, param_4, param_5);\n"
    
    output += """
        default: {printf(\"UNHANDLED MESSAGE HANDLER FOR TYPE: 0x%08x\\n\",hashcode);}
    }
    return (hashcode & 0xffffff00);
}
"""

    # Must be a CPP file because of the autogen/autoinject comments
    with open("src/action/ui/ui.cpp", 'w') as file:
        file.write(output)
