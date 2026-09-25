# PS2 audit: known addresses the alignment disputes

Each row was hidden and re-placed by alignment with margin >= threshold, somewhere else. Causes seen: a
wrong address in the sheet, a mis-named PS2 function, overloads (two constructors). For review only.

| row | colour | sheet name | known at | from | sheet size | retail size there | alignment says | its name now | retail size | margin |
|---|---|---|---|---|---|---|---|---|---|---|
| 1093 | green | DebugItem type_info function | 00143668 | sheet | 40 | 78 | 00143580 | FUN_00143580 | 40 | 7.0 |
| 1284 | green | EClearProgrammerEvent::EClearProgrammerEvent(int) | 00149c88 | sheet | 18 | 68 | 00149c70 | EClearProgrammerEvent::EClearProgrammerEvent | 18 | 6.4 |
| 3150 | orange | RHighLevelLightManager::AddPositionalLight(COORD4 &, COORD4 &, | 001af8c0 | sheet | f8 | 1c8 | 001afc58 | FUN_001afc58 | f8 | 8.9 |
| 3499 | green | RSceneObj::Load(char *, char *, char *, unsigned int) | 001c7360 | sheet | 1d0 | 128 | 001c7488 | RSceneObj::Load | 1d0 | 3.5 |
| 3765 |  | RViewCamera::Camera(void) const | 001d50b0 | ghidra | 8 | 8 | 001d50a8 | RViewCamera::Camera_const | 8 | 6.0 |
| 5124 |  | WWorld::Close(void) | 00230730 | sheet | e8 | 380 | 00230648 | WWorld::Close | e8 | 8.6 |
| 5297 | green | GGirlieMaterial type_info function | 0023a790 | sheet | 40 | 58 | 0023a758 | FUN_0023a758 | 38 | 3.5 |
| 5489 | green | GSystem::COLOR_ConvertColorToCoord4(unsigned int, COORD4 *) | 0024b4a8 | sheet | 60 | 138 | 0024b5e0 | FUN_0024b5e0 | 60 | 7.1 |
| 6360 | orange | EAGLAnim::ResetStats(void) | 00264570 | sheet | 28 | 8 | 002644e0 | FUN_002644e0 | 28 | 3.1 |
| 7145 | orange | rmpipe | 0025d400 | sheet | 200 | a0 | 0025ef00 | FUN_0025ef00 | 200 | 6.2 |
