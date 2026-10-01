# PS2 audit: known addresses the alignment disputes

Each row was hidden and re-placed by alignment with margin >= threshold, somewhere else. Causes seen: a
wrong address in the sheet, a mis-named PS2 function, overloads (two constructors). For review only.

| row | colour | sheet name | known at | from | sheet size | retail size there | alignment says | its name now | retail size | margin |
|---|---|---|---|---|---|---|---|---|---|---|
| 683 |  | AICharacterPedestrian::DoAvoiding(void) | 00127a30 | ghidra | 370 | 328 | 00127d58 | AICharacterPedestrian::DoDodging | 370 | 5.1 |
| 1386 | green | EInflictDamage::EInflictDamage(unsigned int, bool, WTrigger *, | 0014fd20 | sheet | 28 | f0 | 0014fcf8 | EInflictDamage::EInflictDamage | 28 | 5.6 |
| 4094 |  | RMissileStreak::Update(COORD4 &, float) | 001e4640 | ghidra | 2a8 | 2a8 | 001e4400 | FUN_001e4400 | 240 | 3.5 |
| 7980 | orange | sceSifStopModule | 002bdec0 | sheet | 208 | a0 | 002bdc28 | FUN_002bdc28 | 208 | 3.5 |
