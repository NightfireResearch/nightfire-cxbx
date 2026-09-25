# PS2 audit: known addresses the alignment disputes

Each row was hidden and re-placed by alignment with margin >= threshold, somewhere else. Causes seen: a
wrong address in the sheet, a mis-named PS2 function, overloads (two constructors). For review only.

| row | colour | sheet name | known at | from | sheet size | retail size there | alignment says | its name now | retail size | margin |
|---|---|---|---|---|---|---|---|---|---|---|
| 1233 | green | EAwardHitToPlayer::EAwardHitToPlayer(int) | 00148540 | sheet | 18 | 68 | 00148528 | EAwardHitToPlayer::EAwardHitToPlayer | 18 | 6.4 |
| 2331 |  | GameLoop_StartUsingMainBigFile(void) | 00175ab8 | sheet | 90 | 20 | 00175a28 | UseNamedVivFile | 90 | 5.2 |
| 3092 |  | RFog::Debug(void) | 001aca28 | sheet | b0 | 48 | 001ac978 | RFog::SetFogParams | b0 | 3.0 |
| 4469 |  | Simulation::SpawnNewtonObject(COORD3 &, COORD3 &, COORD3 &, COO | 00200200 | sheet | f8 | e0 | 002002e0 | Simulation::SpawnNewtonObject | e8 | 3.5 |
| 7191 |  | PRINT_string(char *,...) | 00260818 | ghidra | 50 | 48 | 00260860 | FUN_00260860 | 50 | 7.0 |
