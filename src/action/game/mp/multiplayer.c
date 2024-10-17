#include "multiplayer.h"
#include "../game.h"

#include <string.h> // for memset

// For now, just link to the locations in memory
// When we completely reimplement the game functions, we can replace these with locally defined variables


//MPSpawnPoint SpawnPoints[64]; // Quantity confirmed: 0x1C0 words (0x700 bytes) are cleared in MP_Init, size of each element is known as 0x1C bytes
#define AddressOfSpawnPoints 0x00261d58
#define NumberOfSpawnPoints 64
#define SpawnPoints (*(MPSpawnPoint(*)[NumberOfSpawnPoints])AddressOfSpawnPoints)
static_assert(sizeof(SpawnPoints) == 0x700, "Size of SpawnPoints array not correct");
static_assert((int)&(SpawnPoints[0]) == AddressOfSpawnPoints, "Address of SpawnPoints not correct");




void MP_Init(void) {

    if(GameState.nextLevelHashcode == HT_Level_Menu_Pre)
        return;

    Pickup_MakeRandomWeaponSet();
    
    // Reset the MP objects
    memset(SpawnPoints, 0, sizeof(SpawnPoints));
    memset(SpawnPntTeamCount,0,sizeof(SpawnPntTeamCount));
    SpawnPntCount = 0;
    memset(&MPObjects, 0, sizeof(MPObjects));
    memset(&Flags, 0, sizeof(Flags));
    memset(&Bases, 0, sizeof(Bases));
    memset(&Uplinks, 0, sizeof(Uplinks));
    UplinkCount = 0;
    memset(&DemolitionPlaces, 0, sizeof(DemolitionPlaces));
    memset(&Demolition, 0, sizeof(Demolition));
    DemolitionCount = 0;
    memset(&ProtectionPlaces, 0, sizeof(ProtectionPlaces));
    memset(&Protection, 0, sizeof(Protection));
    ProtectionCount = 0;
    memset(&GoldenEye, 0, sizeof(GoldenEye));
    memset(&GoldenEyeSpawns, 0, sizeof(GoldenEyeSpawns));
    GoldenEyeSpawnCounts = 0;
    // TODO: Implement the rest of this

    for(int i = 0; i < MAX_MP_AGENTS; i++) {
        // TODO: This
    }

}