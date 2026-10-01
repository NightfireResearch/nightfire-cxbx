# Batch P011: not applied

| row | sheet name | PS2 | why |
|---|---|---|---|
| 302 | ActTextureDatabase::ResetTextures(void) | 00115148 | defer: decompiles to a bare return but spans 0xd8 bytes; called 4x by StopUsingResources; could be StopUsingTexture (8 bytes in the sheet) |
| 700 | AICharacterPedestrian::TooFarAway(COORD3 &) | 00129818 | defer: (downgraded on review) body ignores the COORD3 argument the sheet signature has; nothing else corroborates TooFarAway. Body 0x44 plus an 8-byte gap before UpdateAnimSpeed, which fits TooFarAway (0x48) followed by IsVisible (0x8); it tests AIRoadSpawn::PedRespawnAvailable at the ped position. |
| 718 | global constructors keyed to AICharacterPedestrian::fgPedConvOf | 0012acf0 | accepted, but name cut short by the sheet's 63 characters |
| 802 | basic_string<char, string_char_traits<char>, chunky_alloc<false | 00137e28 | accepted, but name cut short by the sheet's 63 characters |
