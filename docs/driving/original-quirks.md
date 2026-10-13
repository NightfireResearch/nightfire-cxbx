# The driving engine's quirks: what the port keeps, and where it differs

Behaviours of the original Driving.xbe that the port keeps on purpose, and the places where it cannot or does not
match the original. Every address is Driving.xbe's. The rule is exact behaviour first (docs/style-guide.md): an odd
branch, a leak or a read past an array stays in the port when something could observe it. The only exception is
uninitialised stack the original leaves in a record. The port writes zero there and lists the place below. The
list also works as a checklist for comparing with the GameCube and PS2 builds: each entry is a question to ask of
the other build's function.

Each integration adds its round's entries; the latest are the missions (`game.missions`,
docs/driving/game-missions.md), the mission events (`game.events`, docs/driving/game-events.md), the AI
(`game.ai`, docs/driving/game-ai.md), the HUD and front end (`game.frontend`, docs/driving/game-frontend.md) and the C
runtime (`sys.crt`, docs/driving/c-runtime.md).

Differences of one ulp in floating point are accepted and not listed one by one. For example, an FSIN/FCOS result
that is rounded through a double-returning helper is one ulp off (docs/driving/maths.md 3.2, section 8).

Contents: 1 deviations (the port differs), 2 harness masks, 3 quirks kept faithfully.

---------------------------------------------------------------------------------------------------------------

## 1. Deviations: where the port differs from the original

Almost all of these are uninitialised stack in the original. "Zero" means the port writes 0 (or 0.0f, false) there.

### 1.1 Uninitialised stack in a record or a vector

| Function | Address | The original | The port | What it can affect | Doc |
|---|---|---|---|---|---|
| REmpBolts::CreateVehicleBolt | 0x0009c0e0 | subtracts an uninitialised stack vector from the spark direction it hands Shell::ApplyWorldEffects | zero | the sparks' direction only | game-effects |
| RScorchWorld::ScorchWorld | 0x000a50c0 | `across` w and `centre` w are uninitialised (AddDecal copies the centre's w into the corners) | zero | the decal corners' w; nothing found reads it | game-effects |
| RTyreTrack::Add | 0x000ac4c0 | the perpendicular's w is uninitialised | zero | nothing reads it | game-effects |
| SWeaponManager::FireWeapon | 0x000bb2b0 | the kWeaponQSlick scorch is placed from a local the function never sets | zero | the scorch's position; the shadow does not log it | game-weapons |
| Sentry::ComputeTargetAngle | 0x000731e0 | for a `which` the switch does not cover, the invalid-axis flag is an uninitialised stack byte | 0 | that call's answer | game-weapons |
| Sentry::RotateSRB | 0x00073730 | for an invalid axis the rotation matrix is uninitialised stack | zero | the body's orientation for an invalid axis (no placed sentry has one) | game-weapons |
| Sentry::DrawMuzzleFlashes | 0x00073500 | the direction's w is uninitialised | zero | nothing reads it | game-weapons |
| RayShell, Shell | 0x00071930-0x000752f0 | a vector's w that only the xyz functions touch, and WorldCollisionInfo's segment words (from its inlined constructor's stack) are uninitialised | zero | nothing found reads them | game-weapons |
| Missile::CheckForCollision | 0x0005f6d0 | the w of the world segment's start, which VU0_v4sub (0x0005fd92) leaves alone, goes to CheckHitWorld (0x0005ff75) and into WorldCollisionInfo's copy of the segment (0x0005fed0) | zero | nothing found reads it; the shadow logs the starts' xyz only | game-weapons |
| RShadowMap::SetCamera | 0x000a5e3a | transforms the car's box corners with a w neither it nor its callers write (a NaN there makes the fit zero) | zero | the projected shadow's fit | render |
| RPlayerCamera::UpdateSplineCam | 0x00084a60 | on a road of one lane, eases toward an uninitialised stack offset | zero | the spline camera on a one-lane road | camera |
| RPlayerCamera::UpdateMomentumHeliCam | 0x00087880 | `turned` w is stack garbage, copied into cameraOffset.w | zero | cameraOffset.w | camera |
| RPlayerCamera::UpdateEllipseCam | 0x000886f0 | cameraOffset.w is set from a local whose w is never written | `offset.w = 0` | cameraOffset.w | camera |
| RPlayerCamera::UpdateTumbleCam | 0x00088380 | as above | never writes cameraOffset.w, so the old value stays | cameraOffset.w | camera |
| RPlayerCamera::UpdateAutoDriveCam | 0x000857d0 | the rotated offset's w is uninitialised stack, copied into cameraOffset.w | the same, from the port's own uninitialised `Coord4 offset` (the value differs) | cameraOffset.w | camera |
| RPlayerCamera::UpdateBumperCam | 0x00080b90 | position.w is stack, copied to eye.w | zero | eye.w | camera |
| RPlayerCamera::UpdateDashboardCam | 0x00081d10 | position.w and offset.w are stack, copied to eye.w and cameraOffset.w; the acceleration vector is stack when the anchor gives none | zero | those w words; the acceleration with no anchor value | camera |
| RPlayerCamera::UpdateRelativeAnimationCam | 0x00082840 | heading.w is stack (not read); the `playing` byte is stack when there is no animation | zero | nothing / the no-animation case | camera |
| RCameraIniLoader::LoadFile | 0x00078770 | heli and auto-drive arm fields the file does not give are stack; the "heights" buffer is uninitialised past the values read | zero | the arms' unset fields | camera |
| RigidBody::CollideWithWorld | 0x000b1420 | hands its box hits the lever arm left by the last vehicle world hit (stack garbage when there was none) | zero | box-hit response with no earlier vehicle hit | physics |
| RigidBody::CollideWithObject, ResolveWorldOBBCollision | 0x000b0510, 0x000af960 | vector w words and WorldCollisionInfo segment words are stack | zero | nothing found reads them | physics |
| SetFrame (inlined in RigidBody::Construct, ResetObject) | 0x000b0b20, 0x000adcd0 | worldInverseInertia row 3 is stack leftovers | zero | nothing reads row 3 | physics |
| RigidBody::CollideWithGround | 0x000ae4c0 | leverContacts.unknown00 is unset | zero | nothing reads it | physics |
| Newton::SpawnFromEvent | 0x00061310 | `centre` is stack on paths that set neither | zero | those paths | physics |
| Util_GenerateMatrix | 0x000bce90 | the w words of rows 0 and 1 are stack | zero | nothing found reads them | collision |
| PBondCar::AddSnowmobileForces | 0x000632a0 | the last w of the scratch pad's axle (+0x280) is uninitialised | zero | nothing found reads it | vehicles |
| RLightManager::AddSpecularLight | 0x0007e950 | copies an uninitialised stack word into the specular light's w | zero | specularLight.w | render |
| RLensFlareManager::DrawFlares | 0x0009e540 | the Glare fields it does not set are stack garbage | zero | nothing found reads them | render |
| RotateOffsetToHeading | 0x000808f0 | side.w is uninitialised (the scalings keep each destination's w) | zero | nothing reads it | render |
| RStateManager::GetGeoPrimState | 0x00092d20 | `handled` is uninitialised before the first tag | false | the first tag's handling | render |
| DrawInstance | 0x0007de30 | the kept draw's fourth word is stack | zero | nothing found reads it | render |
| WWorldPos::FindClosestFace (two overloads) | 0x000d3050, 0x000d3110 | the candidate's corner[0] count word (+0x0c) is stack garbage | zero | nothing reads it (face normal, plane and point tests read x, y, z) | collision |
| WRoadNav::InitAtPoint | 0x000cc100 | roadT is left as stack when the cell has no segments | 0.0f | that case | world |
| WCollisionMgr::GetOBBObjectIntersection | 0x000bf8c0 | slot[0] can be read unwritten (a segment through a corner) | zero | that case | collision |
| DAudio::InfoDraw | 0x000371b0 | prints a stack byte for a StreamHold outside 0..2 | 0 | debug page text | game-audio |
| AIndex::Construct | 0x001276c0 | a name shorter than four characters is searched into uninitialised stack | the buffer is zeroed | that case | audio |
| ActCharacter::LoadCharacter | 0x00014490 | ActCharacterInfo leaves its type unset for a model letter other than h/p/c/a (stack) | the stack copy is zeroed | that case | anim |
| Schedule::AddTask | 0x0005c490 | two padding fields of the task record are stack | zero | nothing reads them | core-data |
| UDataGroupDecodeTag | 0x001177b0 | the number is an uninitialised local when sscanf matches nothing | -1 (the four characters kept) | malformed tags only | core-data |
| SNDI_parsetimbre, SNDI_patchtohdr | 0x00140aa0, 0x0013f1c0 | the tag reader's value before the first tag of length 0..4; patchtohdr's channel-1 azimuth offset with no 0x9d tag (stereo, output mode not 2) | zero | that azimuth | sound |
| SNDBANKI_playtimbre | 0x00140260 | the voice range when SNDPLATFORM_getvoicerange writes none | zero | that case | sound |
| SNDSTRMI_parseheader, SNDSTRMI_parsedata, SNDPKTPLAY_start | 0x0013bc20, 0x0013bdb0, 0x0013e980 | user-data message +0xc; packet +0 and +8; the patch header outside the channel count | zero | nothing reads them | sound |
| dsndMixInit | 0x0013d5e0 | byte 7 of the mixer parameters is never written | zero | nothing | sound |
| SMissionManager::UpdateObjectiveSpecial | 0x000b81a0 | the w of three Coord4s is uninitialised | zero | nothing: only x, y and z feed the dot product | game-missions |
| SRulePlayerDir::CheckRule | 0x000ba690 | the w of three stack vectors is uninitialised | zero | nothing reads w | game-missions |
| ECinematicCamera::Process | 0x00049e27 | the placement's w is stack | zero | nothing found reads it | game-events |
| ECollision::Process | 0x00049ee3 | the zone record's position and velocity w, and its third vector, are stack | zero | nothing: AIZoneController::RegisterZone reads the position's x, y and z only | game-events |
| EBashCarAlongObjectAxis::Process | 0x00049c8b | the impulse's w is stack | zero | nothing reads it | game-events |
| EHitWindow::Process; EKickAIElement::Process; EFireWeapon::Process | 0x00047b90, 0x00040710, 0x0004ac40 | the smoke position's w; the push and spin vectors' w; the lob's and the aim point's w | zero | nothing reads them | game-events |
| EResetPlayerCarPos::Process | 0x0004d56e | the momentum's w is stack | zero | nothing: the body takes x, y and z | game-events |
| EPlayActorEffect, EPlayEffect, EPowerUp (Process) | 0x0004211c, 0x0004254f, 0x00042c4e | GFX positions in stack vectors whose w (or, for EPowerUp, the rotation's x) is never written | Coord3 locals | nothing: GFX::Trigger copies x, y and z only (0x000d3343) | game-events |
| ESpawnExplosionStatic::Process | 0x00045199 | with neither a position nor an instance, the position is stack | zero | that explosion's position | game-events |
| EWakeupSmackable::Process | 0x00046d10 | the ray's and the body's w are stack | zero | nothing reads them | game-events |
| ESpawnTraffic::Process, EStartMission::Process | 0x00045d70, 0x0004e860 | the momentum's w is stack | zero | nothing: it is never stored | game-events |
| AIGroundVehicle::GetDirectLaneWeights | 0x0002b7a0 | the inlined collision-record constructor copies stack garbage into the record | zero | nothing found reads it | game-ai |
| WorldCollisionInfo::Construct | 0x0001d9f0 | segmentStart and segmentEnd (+0x20..+0x3f) are copied from its own uninitialised stack | zero | nothing found reads them (the collision shadows mask them) | game-ai |
| AIElementController::WakeUp | 0x00028bb0 | a helicopter's seated riders (seats 3-17) get an uninitialised stack word as their vehicle, for the seat's attribute lookup and the rider's constructor | the helicopter | a helicopter element with seated riders (none seen); the AI shadow leaves the case out | game-ai |
| AICharacterEnemyGround::AvoidThreat, DoRunning, HandleInterrupts | 0x0001da80, 0x0001f180, 0x0001f3c0 | local vectors' w are never written | zero | nothing reads them | game-ai |
| AIElementController::WakeUp, Update; AIElementTracking::UpdatePosition; AIZoneController::InZone; AICharacterHands::UpdateRotPos | 0x00028bb0, 0x00029880, 0x00027f00, 0x00036b20, 0x000232b0 | local vectors' w (the player's position, the seat, the hands' offsets) are never written | zero | nothing reads them | game-ai |
| AICharacter, AICharacterPedestrian; AIHelicopter, AIVehicleController, AIRoadSpawn | 0x0001c190-0x00027c80; 0x00031850-0x00036a00 | local vectors' w are never written | zero | nothing reads them | game-ai |
| GGallery::Get_InterpolatedKey | 0x000d5910 | copies 0x34 bytes of a stack key whose header bytes 0x22-0x33 it never writes | zero | nothing found reads them (QUAD_SetState reads the position and orientation); GameFrontEndShadowC logs the first 0x22 bytes | game-frontend |
| GFXGallery::GLARE_Draw | 0x000d4160 | the Glare record's fields it does not set are uninitialised | zero | AddGlare's copy of them | game-frontend |
| GFXGallery::DEBRIS_Draw | 0x000d4b90 | the velocity spread's w is uninitialised | zero | nothing: RDebrisType::Add overwrites it | game-frontend |

### 1.2 Allocator bytes

An STL map's empty allocator byte is copied from an uninitialised local. Nothing reads it.

| Function | Address | The port |
|---|---|---|
| REmpBolts::Construct (the anchor map); REmpBolts::FindAnchors (`counts`) | 0x0009dc90, 0x0009d9a0 | 0 |
| RVehicleParticle::Init (the set) | 0x000a8240 | 0 |
| AIndexMap::Construct | 0x00127620 | 0 |
| CARP::ResolverMap::Construct | 0x00119e40 | 0 |
| WSoundGroup::Construct | 0x000cd5a0 | 0 |
| PVehicle::BuildCarNames | 0x00071730 | 0 |
| RParticleLibrary::RPartLibraryData::Construct, RParticleSystemManager::Construct | 0x000a2e20, 0x000a2f00 | 0 |
| WindowPaneMap::Construct (and CheckHitWindow's local map, copied by CopyConstruct 0x000c3ab0) | 0x000c45d0, 0x000c4d70 | not written (the port's own stack) |
| WCollisionMgr::Construct (its maps) | 0x000c5500 | not written |
| Simulation::Construct (the object map and the dead set) | 0x000b4fe0 | 0 |
| AIRoadSpawn::RefreshSpawnData (the grid cell vector) | 0x00034ed0 | 0 |

### 1.3 Other differences

| Function | Address | The original | The port | Doc |
|---|---|---|---|---|
| ROffscreenBuffer::MakeTexCopy | 0x0007f870 | asked for more than four copies, reads past its four offsets into its own stack frame | draws those copies at offset (0, 0), with an untested warning. **To revisit** if a caller ever asks for more than four (none seen) | render |
| RayShell::CollideWithObjects | 0x00072190 | computes the first offset from the origin to the object (0x000722fe), which nothing reads before the simple-body branch overwrites it | left out. (The bottom of a rigid body's box, computed after the overlap test, is not dead: it goes into the frame GetOBBObjectIntersection takes.) | game-weapons |
| FILESYS_allocop | 0x0010cbf0 | running out of op records crashes | the submit fails and returns 0 | filesys 7.1 |
| FILESYS_size | 0x0010cf90 | clears worker 0's running op, so a concurrent wait can return early | leaves it | filesys 7.2 |
| GGallery::QUAD_Interpolate | 0x000d5b70 | interpolates the corners on the x87: with both corners NaN, (b - a) x t + a leaves the NaN of larger payload | MSVC puts `a` first in the SSE add, so that NaN is `a`'s. Only a key that holds no quad has NaN corners | game-frontend |
| FILEDEV_opensub (subfile seek) | 0x0010d290 | the seek test compares `base - pos` | always seeks, which is what the original did in practice | filesys 7.3 |
| Q_initshared | 0x0014a640 | the done queue's lock is a byte copy of the pending queue's critical section | its own lock | filesys 7.4 |
| FILESYS_addbig, worker | 0x0010d030, 0x0010c110 | a failed addbig leaks the directory; an unknown archive format reads a negative size | freed; checked | filesys 7.8 |
| FILEDEV_open; the archive and path name copies | 0x0010d2f0 | an open with no free slot fails with error 0; unbounded strcpy/strncpy/sprintf of names into 0x100-byte buffers | a real error; bounded copies | filesys 7.9, 7.10 |
| FsWorkerThread | 0x0010c110 | a read error with no handler, or a declining one, hangs the worker | reports the error | filesys |
| MissionList::BuyHead | 0x000b8490 | when FastAlloc fails, stores NULL at address 4 | skips that store | game-missions |
| SMissionManager::Destruct | 0x000b8e70 | calls the empty timer destructor (NullFunction) ten times | the calls are left out | game-missions |
| SMissionManager::Construct | 0x000b8c30 | constructs the 64 stage lists by calling 0x0011cc10 | constructs them inline (the same writes) | game-missions |
| SimPointerVector::InsertN; the trees' throws | 0x000b3b80; 0x000b2e00-0x000b4080 | a catch block frees the new storage and rethrows; std::length_error and the trees' errors are thrown | no catch block (copying pointers cannot throw); the throws go through TreeThrow | game-missions |
| SimPointerVector::Reserve | 0x000b4080 | past max_size, _Xlen throws | Xlen, then returns | game-missions |
| Simulation::SpawnExplosion | 0x000b4cd0 | with no object hit, the simple flag's dword carries left-over upper bytes (only the low byte is read) | a clean bool | game-missions |
| SRuleRange::CheckRule | 0x000ba570 | zeroes three words of its position | zeroes four | game-missions |
| SRulePlayerDir::CheckRule | 0x000ba690 | angle * -1.0 | -angle (the sign of a NaN can differ; the result is the same) | game-missions |
| SRuleTimer::Construct | 0x000ba080 | copies the threshold through the x87 | through QuietNaN (the same quieting) | game-missions |
| the events' MakeEvent callbacks | 0x0003dc00-0x0004f170 | copy the records' floats through the x87 (fld/fstp, which would quiet a signalling NaN) | copy the bits | game-events |
| AVoice::Destruct | 0x0003f400 | destroys its views through the vector destructor iterator | calls View::Destruct for each, last first (the same calls) | game-events |
| EFireWeapon::Process | 0x0004c544-0x0004c550 | for the guns fired by a holder other than the player (types 1, 2, 5, 0x10, 0x13 and 0x15), passes a mix-name buffer it never writes | passes "" | game-events |
| EForceCarStop, ELavaHaze, ELetterBoxOn/Off, EObjectiveDisplayText, ESpawnSentry (Process) | 0x0004c7a4, 0x000458dd | a setne result pushed with the register's left-over upper three bytes | a clean bool (the callees read one byte) | game-events |
| EStreamEvent::Process | 0x0004f0b2 | the "nis" case passes AFader::QueuePrimary a third argument with left-over upper bytes | a bool | game-events |
| ESetCullDistanceFactor, ESetJumpGravity (Destruct) | 0x00043fa0, 0x00044040 | the compiler dropped the first of the two vtable stores | both written | game-events |
| AIGroundVehicle::FireBullets, FireRockets, FireMissiles | 0x0002e910, 0x0002eb40, 0x0002ee00 | call GetPhysicsObject first and drop the answer | the pure call left out | game-ai |
| AIGroundVehicle::CheckAgentRoadNetworkCollision | 0x0002fde0 | a floor computed and discarded | left out | game-ai |
| AIGroundVehicle::CheckAgentDirectCollision | 0x000306b0 | an inlined copy of InCollisionRange | calls InCollisionRange (the same result) | game-ai |
| AIGroundVehicle::DoAgentStateUpdate; AIElementController::Update | 0x0002cd80; 0x00029880 | ProgrammerDefinedEvent's text is the game's empty string (0x00189fb1) | our own "" | game-ai |
| AICharacterPedestrian::DoDying | 0x00025fc0 | reads the table before checking the index | checks first (the same result) | game-ai |
| AICharacterPedestrian::Init | 0x00026800 | the count's decrement inlined | calls KillSelf (the same writes) | game-ai |
| AIVehicleController::RegisterSimCarList | 0x00036990 | RegisterSimCar's body inlined | calls RegisterSimCar | game-ai |
| AIVehicleController, AIRoadSpawn, AIHelicopter; WRoadNav::SegmentSpeed | 0x00031850-0x00036a00; 0x00029cd0 | WRoadNetwork::Get called again for nothing | the pure calls left out | game-ai |
| AIElementController::Construct | 0x00028190 | placement new's NULL check | left out | game-ai |
| AIElementController::WakeUp | 0x00028bb0 | the binary's measured facts have ESI read on entry (through the seat jump table) | a plain method tagged AUTOLTCG (placed by Ghidra's name, without the register check); ESI is set before the riders' loop and the one caller passes `this` in ECX and ESI | game-ai |
| GFXGallery::CANVAS_HasStopped | 0x000d3d90 | reads each particle system's type flags and drops them | the dead read left out (it matters only for a NULL type, where the original faults) | game-frontend |
| GFXGallery::FRAME_Draw | 0x000d55f0 | keeps a running maximum that nothing reads | left out | game-frontend |
| GGirl::DoGirl | 0x000d7a50 | ends with WBINVD (a CPU cache flush for the GPU's sake) | left out | game-frontend |
| GLoadingScreen::InitLoadScreen | 0x000e2390 | builds "HINT_ID%d" in an 8-byte buffer, which overflows from HINT_ID10 | a 24-byte buffer | game-frontend |
| GLoadingScreen::LoadAndDrawUnLoadingScreen | 0x000e2110 | unbounded sprintf into a stack buffer | bounded snprintf (Status's formatting is the game's vsprintf and sprintf, c-runtime) | game-frontend |
| GSubtitleSplitter::Reset | 0x000e33e0 | tests `this` for NULL before the loop only | no test (both fault on NULL) | game-frontend |
| GHud::DrawMissionResult | 0x000df2e0 | inlines QUAD_SetState, and copies Get_InterpolatedKey's answer once more | calls QUAD_SetState (the same writes); uses the answer directly | game-frontend |
| GFX::Trigger (the core) | 0x000d32f0 | takes the effect in ESI and the offset in EDI | not patched (no tag can name it); its three callers (0x000d34e0, 0x000d3520, 0x000d3610) are ours and call our core, so the original is dead | game-frontend |
| mainXapiStartup (for `_rtinit`, `_cinit`) | 0x001106bb, 0x00110663 | runs `_ioinit` (0x00139acd: lowio's 0x500-byte table from the heap), `_initstdio` (0x0013594c: stdio's stream table, from the heap), `__initmbctable` (0x00139a7b) and installs the C++ unhandled-exception filter (0x001363ba) | none of them: their state is read only by the runtime's stdio, lowio and multibyte code and by XAPI's thread start, none of which runs; the heap lacks those two early blocks | c-runtime |
| `_fpmath` | 0x00131d28 | points `_cfltcvt_tab` at the float conversions (`_cfltcvt_init` 0x00131cf0) | leaves it out: our printf and scanf call the conversions directly | c-runtime |
| `_exit` (doexit) | 0x0013438b (0x001342e1) | walks the terminator table, whose one entry (0x001363cc) puts back XAPI's unhandled-exception filter | does not: the filter was never taken over | c-runtime |
| `_Atexit` | 0x00131cb0 | calls abort (0x00135245) when its ten slots are full | `_amsg_exit(10)`; only two functions are registered | c-runtime |
| `_NMSG_WRITE` | 0x001392ac | compares one entry past its message table for a code not in it | stops at the table's end; every code passed is in it | c-runtime |
| `eh vector constructor iterator` | 0x0013326e | destroys the elements made so far if a constructor throws | does not unwind; nothing throws | c-runtime |
| printf | 0x00132192 | formats and writes through the runtime's stdio | logging.cpp's dbg_wprintf, the host's printf: debug output only | c-runtime |

---------------------------------------------------------------------------------------------------------------

## 2. Harness masks for these reads

A shadow test masks a field, or leaves it out, when the original fills it from its own stack:

- PlayerCamShadowB: UpdateSplineCam is run only on roads of several lanes. UpdateAutoDriveCam's cameraOffset.w is
  not compared.
- PlayerCamShadowC: cameraOffset.w of the heli, tumble and ellipse cameras.
- CamDataShadow: the arm fields the file never gives, and the heap's padding words.
- GameEffectsShadowC: the spark normal of REmpBolts::CreateVehicleBolt, and the anchor map's allocator byte.
- GameEffectsShadowB: an emitter system's lastPosition, which comes from the uninitialised emitter position.
- GameEffectsShadowD: RDecalManager::AddDecal is logged by the vectors' x, y and z only, because the centre and
  axis w are uninitialised.
- GameWeaponsShadowA, B: the Q-Slick scorch position and the uninitialised w values are not logged.
- LightShadow: specularLight.w.
- ColListShadow, ColQueryShadow, ColGridShadow: WorldCollisionInfo +0x20..+0x3f, +0x4c, +0x56 and +0x5c, and the
  fourth words of a found face's first two corners. GeomShadow and RigidBasicsShadow: the face's +0x0c word.
- RigidResolveShadow: GenerateImpulse's relativeVelocity.w.
- CoreLoopShadow: the task records' two padding fields. CoreUtilShadow: the key buffers after the terminator.
- CarpShadow: a tag that sscanf does not match after its four characters is left out.
- TexContextShadow: the padding after `used` (CreateNewTextureElement, 0x00093ac2..0x00093ae6).
- LoaderShadow: a symbol table's bytes +0x0d..+0x0f.
- SndPlatformShadow: byte 7 of the mixer parameters.
- SndTagShadow: patchtohdr's channel-1 azimuth word is counted as "masked". Inputs whose originals read
  undefined values are kept out: a long tag before any short one, more than six channels, more than four
  user-data tags.
- SndStreamShadow: the channel pointers of a stream with no header parsed.
- BondCarShadowA: AddSnowmobileForces' axle w (not covered).
- GameEventsShadowD: ESpawnForceEffect's effect field when it holds the address of the stack ArticleEffect it leaves there,
  and the new explosion's position when ESpawnExplosionStatic has neither a position nor an instance.
- GameEventsShadowA-D: when both sides fault (made-up data only), the event's vtable word, which the original's
  exception funclet puts back to Event's as the fault unwinds through it.
- GameEventsShadowD: ESpawnForceEffect's effect field when it holds the address of the stack ArticleEffect it leaves there.
- GameAIShadowE: WorldCollisionInfo::Construct's segment words (+0x20..+0x3f).
- GameAIShadowF: a helicopter element's seated riders are left out (the original's vehicle for them is uninitialised
  stack).
- GameFrontEndShadowC: QUAD_Interpolate's corners are logged with any NaN as one value (the payload above).

---------------------------------------------------------------------------------------------------------------

## 3. Quirks kept faithfully

### 3.1 Uninitialised or never-set values the port reproduces, or that cannot matter

| Function | Address | Quirk | Doc |
|---|---|---|---|
| Render_InitLibRender | 0x0007d710 | hands RRenderer::Init (0x0007d5e0) an uninitialised 0x70-byte stack buffer, which is never read; the port does the same | render |
| PBondCar::ProcessSimplePhysics, ProcessSnowmobilePhysics | 0x00069710, 0x00063ad0 | copy rollingResistance, which was not set that step; the copy is not read | vehicles |
| ATrafficVehicle::Destruct | 0x0012d300 | reads two counted flags (TrafficCountA/B 0x00243b5c/0x00243b60) that ATrafficVehicle::Construct (0x0012cb90) never sets | game-audio |
| AGun (+0x18) | 0x00129550 | unknown18 is never written | game-audio |
| InputConfigManager::Shutdown | 0x00050080 | frees a pointer at +0x2c that nothing sets (only Get, 0x00050270, nulls it) | input |
| IFeedback (records +0x2c, +0x30) | 0x0004fbc0 | two float inputs (x100 for the pulse level, x125 for the steady speed) that only the inline Reset writes | input |
| ACharacter::Say | 0x0011dad0 | FPedLinesWrapped (0x00243960) is never set (MPedLinesWrapped 0x00243968 is) | game-audio |
| EAGL anim frame search | 0x000fd840 | going back, it compares the time with the frame pointer's bits read as a float, so any time >= 0 stops at once | eagl |
| Simulation::Construct | 0x000b4fe0 | leaves several fields (the owners' slots among them) unset: zero only because the one Simulation is a static | game-missions |
| SMissionManager::Construct | 0x000b8c30 | leaves missionSets (+0x28) unset | game-missions |
| SRuleProperty::Construct | 0x000b9960 | strncpy(8) leaves an eight-character name unterminated; the compare reads eight at most | game-missions |
| AIElementController::Construct | 0x00028190 | the tracking records' position, speed and lastHitPoints are left uninitialised, so the first damage figure is garbage | game-ai |
| AICharacter::Destruct | 0x0001c1d0 | does not NULL the voice or the actor link | game-ai |
| AIVehicle::Destruct | 0x00035b40 | does not clear driveToNav | game-ai |
| AICharacterEnemySnow | 0x0001fd40 | aimPitchDir is never written | game-ai |
| AICharacterEnemyGround::ThreatenedByVehicle | 0x0001e440 | unknown16c is written and never read | game-ai |
| GGirl::UpdateTexture | 0x000d6f60 | GirlUnmirrored (0x0023f3e4) is never written, so the figure is always drawn mirrored | game-frontend |

### 3.2 Reads and writes past an array, or before it

| Function | Address | Quirk | Doc |
|---|---|---|---|
| AVehicle::PlayCollision | 0x0011f1a0 | its search of the impact sounds steps past busy entries with no bound, so CollisionTicks (0x00243988, 12 entries) stays at the game's address with the globals after it | game-audio |
| SWeaponManager::Update | 0x000bbdc0 | the targeting test reads weapons[special] with special -1, and weapons[secondary] with -1 after UnloadWeapon: the heap word before the array | game-weapons |
| SWeaponManager::FireWeapon | 0x000bb2b0 | can write fireSteps[4] (+0x34, one past the four) | game-weapons |
| ASnowMobile::PlayCarving | 0x0012b060 | reads one word past the surfaces (previousSurfaces[0] as a fifth) | game-audio |
| ASubmersible::PlayCreaks | 0x0012bb80 | indexes a 4-entry local by creakLevel - 1, checking only below | game-audio |
| DAudio::MixerDraw | 0x00036d10 | a mix group above 6 reads its colour past the 7-entry table (clamped only after) | game-audio |
| RVehicle::PostLoad | 0x000955d0 | the wheel-instance search carries `next` across wheels while each loops to `count`, so it can read past the array | game-effects |
| REmp::Init | 0x0009b680 | its clear overruns into the sphere's first position | game-effects |
| REmpBolts::FindAnchors | 0x0009d9a0 | a long locator name overflows `links[5]` into the next anchor | game-effects |
| RMissileStreak::Update(const Coord4 *) | 0x000a0ea0 | with count 0 at an odd tick it writes points[-2] and, as points[-1], its own header | game-effects |
| Handle::Stop, RAnimEngine::DeactivateSystem | 0x00077be0, 0x00077b60 | move the active list down by one entry more than it holds | anim |
| RPlayerCamera::SetAutoDriveRotation | 0x00081540 | its 97-entry response table has no bounds check | camera |
| RCameraIniLoader::LoadFile, RPlayerCamera::DirectorChangeCameraMode | 0x00078770, 0x00084130 | the heli "Cinematic" index is stored one past its record (read back as [index - 1]); 16 bytes are copied over Heli_Sideways, Heli_Height, Heli_Distance and the word after | camera |
| InputToAction::LoadParseDefFile | 0x00050a20 | an unknown control name indexes the table -1; a line without a carriage return runs off the text | input |
| InputConfigManager::ParseDefFileForFrontEnd | 0x0004ff50 | an unknown slot name writes localeIDs[-1], the configuration's text pointer (+0x20) | input |
| RReflection::PrivateSubmitSceneObj | 0x00098520 | does not check that it found the slot ranked 3 (writes at slot -1) | render |
| WRender::FindVisibleTreeNodesAndCurtains, SimpleDrawVisibleTreeNodes, DrawWorldAtPoint | 0x000c7330, 0x000c77f0, 0x000c8490 | the visible node list and the draws are not bounds-checked (SetCull, 0x000c7700, is) | world |
| SNDCTRLI_getfxbus | 0x0013cc80 | the main path's buses index FxBuses (0x00244f44) from entry 0 and the hardware path's from entry 1, unchecked. They overlap from bus 1, and a bus past the array lands in the globals after it. Only bus 0 is used | sound |
| SNDPLATFORM_setfxlevel (and the per-bus voice fields) | 0x00140070 | bus 1's send and levels are the next fields' bytes | sound |
| SNDI_parsetimbre | 0x00140aa0 | past six channels the azimuth offsets come from the tag tables, and ChannelAzimuth[channels - 1] (0x00244f70) reads past its six rows | sound |
| SNDI_pantoazimuth | 0x00142e90 | a pan over 127 reads past the 128-entry table (0x001d9d88) | sound |
| SND::decodexac | 0x00149d80 | a predictor nibble above 3 reads past the four predictors into the next table | sound |
| SNDMIXI_initfx | 0x00142fc0 | an unknown node type reads past the ten-entry size tables | sound |
| SNDBANKI_playpatch | 0x001405d0 | past twelve timbres the handles run into the header | sound |
| SNDI_patchtohdr | 0x0013f1c0 | past four user-data tags the pointer runs into the sizes and what follows | sound |
| FUN_0013d550 | 0x0013d550 | with both pools' free lists empty, pops from FreeBufferLists[-1] (the PCM active list) and faults if that is empty too | sound 8.5 |
| EAGL Model image lookup | 0x000e9010 | cuts the comparison string at four characters by writing a NUL into the file's long name, past its end when it is shorter | eagl |
| ECollision::Process | 0x0004a183 | the box corner indexes the eight-entry twist table unchecked | game-events |
| EAIElementFireOn/Off, EAIElementSetAccuracy, EAIElementSetCharParams (Process) | 0x0003de30, 0x0003dd70, 0x0003def0, 0x0003dfb0 | run to the element's memberCount over its three members and four member characters, unbounded | game-events |
| EInflictDamage::Process; EKickObject::Process | 0x000405b0, 0x000408f0 | checks only index < 64; the rigid-body owner index unchecked | game-events |
| EPowerUpTimer, EPowerUpAmmo, EPlayCarCameraAnim (Process) | 0x00042fe3, 0x00042d5d, 0x0004cee4 | the timer and the weapon indexes unchecked; the body slot used without the -1 test | game-events |
| AICharacterPedestrian::DoTurning | 0x00026290 | would read peds[-1] if turnTowards were -1 | game-ai |
| AICharacterEnemyGround::PlayZoneInjuryAnim; AICharacterEnemySunroof::DoLeaning, DoArmed | 0x0001ea60; 0x00021740, 0x000213c0 | (pick + 1) % count; zone 4's empty death list plays animation 0; the lean table is indexed [direction - 1] (its base folded one row early) | game-ai |
| AIElementController::RedirectCarpElement, GetCarpElementTracking | 0x000286d0, 0x00028700 | the index comes from the global controller's records, not `this`'s (an arithmetic shift of the byte difference, made positive) | game-ai |
| AIElementController::WakeUp | 0x00028bb0 | an empty member slot ends the riders' loop; a seat with no rider leaves the slot as it was | game-ai |
| AIZoneController::InZone | 0x00036b20 | uses the zone type's radius, indexed by the type's low 16 bits | game-ai |
| GHud::DrawHealth | 0x000d81d0 | with the shield over its maximum, armPuppets[armLit] reads past the array into healthBorderPuppet (GHud +0x068) | game-frontend |
| isdigit, isalnum, isspace, tolower | 0x0013399d, 0x001339ef, 0x001339c6, 0x001327f9 | index `_pctype` with any int, so a value outside -1..255 reads outside the class table | c-runtime |
| ios_base::_Addstd | 0x001310fa | does not stop at eight standard streams: a ninth is written to the slots after `_Stdstr` and `_Stdopens` (kept in StandardStreamTable) | c-runtime |

### 3.3 Writes through a possibly NULL pointer

| Function | Address | Quirk | Doc |
|---|---|---|---|
| SWeaponManager::HandleNuclearSequence | 0x000bb9b0 | the explosion sounds' four fields are written whether or not the allocation succeeded | game-weapons |
| Missile::Construct; Missile::CheckForCollision | 0x0005e8c0, 0x0005f6d0 | the transit sound and the EMP detonation sound are written through without a NULL check | game-weapons |
| Shell::ApplyWorldEffects | 0x000749b0 | writes the impact sound's fields when the allocation is NULL | game-weapons |
| Sentry::GetTarget | 0x00073330 | writes through NULL when an allocation fails | game-weapons |
| Smackable::GoToSleep | 0x00075370 | no NULL check on the proc-anim state | game-weapons |
| Simulation::Spawn*Object | 0x000b5700-0x000b5fb0 | a failed allocation hands NULL to SpawnPhysicsObject, which faults | game-missions |
| AGun::Play | 0x00129250 | a new one-shot's fields and `voice->Play` go unchecked for NULL | game-audio |
| ATurret::Play, ASmackable::PlayCollision, ACharacter::Say | 0x0012e2b0, 0x0012d9c0, 0x0011dad0 | ClearLoops (or the inline loop clear) on a fresh NewVoice result unchecked | game-audio |
| ASnowMobile::PlayMotor | 0x0012aa10 | AVoice_PlayOnce(revVoice) without a NULL check | game-audio |
| RDebris::Construct | 0x000a91b0 | calls Init(NULL) when an allocation fails | game-effects |
| RVehicleParticle::Construct | 0x000a6de0 | writes active = 0 even when the allocation failed | game-effects |
| REmpBolts::TrySpawnBolt | 0x0009d320 | a car with one anchor: GetRandomAnchor (0x0009d2d0) returns NULL and CreateVehicleBolt(from, NULL) faults (the shadow expects it) | game-effects |
| WRoadNav::CalcNextSegmentDirection, CalcNextSegmentLane, CalcNextSegmentSidewalk | 0x000ca120, 0x000ca530, 0x000cad40 | use a NULL GetAttachedDirectionalSegment (0x000c9aa0) result unchecked | world |
| WGridDynamicList::BuyHead | 0x000c5970 | tests &node->prev, never NULL, so node->prev = node runs on a failed allocation | world |
| SFILTER_remove | 0x001444a0 | removing the head does not clear the new head's back link; an empty list with another node faults | sound |
| EAudioUpdate::Process | 0x000498c5 | writes through the listener operator new returned without a NULL check | game-events |
| EHitWindow, EPathSFX, EFireWeapon (Process) | 0x00047c3f, 0x00047cdc, 0x00047d66, 0x0004179f, 0x0004b1cc, 0x0004c410, 0x0004c507, 0x0004c605 | the new one-shot sounds and temporary targets are used without a NULL check | game-events |
| EPathSFXUpdate::Process; EPlaySoundPos::Process | 0x00048254, 0x000485e6 | writes through a NULL one-shot (EPathSFXUpdate reads the sound before it tests it, 0x00048257 against 0x00048277) | game-events |
| ESpawnExplosion::Process | 0x0004e775, 0x0004e80c | writes through NULL one-shot and limited sounds | game-events |
| ESpawnSmackable::Process | 0x00045bc2 | UseArticle on NULL when FastAlloc fails | game-events |
| EAIElementSetWakeRange, EDropMine, EEndControlToSpline, EEnablePowerUp, EPlayActorEffect, ESetAvoidZone, the Dynamic path setters, EShowInstance, EShake, ESetTerrain, EWakeupSmackable (Process) | 0x0003e210, 0x0003f720, 0x0004abc0, 0x0003f7f0, 0x0004211c, 0x00043d50, 0x00044d00, 0x0004dcb0, 0x0004db70, 0x00046d10 | no NULL checks on what they look up (ESetAvoidZone at 0x00043d7c and 0x00043da0, EShowInstance at 0x00044d09, EShake's feedback 0x0004dcea, EWakeupSmackable's third reference 0x00046d55) | game-events |
| AIGroundVehicle::UpdateNavPos | 0x0002a3e0 | the target's body is read unchecked in two cases | game-ai |
| AIGroundVehicle::DoAttackMode | 0x0002bf40 | mode 14 reads the target unchecked | game-ai |
| AIGroundVehicle::DoAgentStateUpdate | 0x0002cd80 | chase reads the target's velocity unchecked | game-ai |
| AIGroundVehicle::DriveToPoint | 0x0002f610 | a sub reads its target unchecked | game-ai |
| AIVehicleController::RegisterAgentGroundVehicle, RegisterHelicopter | 0x00036270, 0x00036660 | no NULL check on the slot found | game-ai |
| AIElementController::WakeUp | 0x00028bb0 | FastAlloc unchecked before SetActor | game-ai |
| AICharacterEnemyGround::Construct | 0x0001dc80 | registers its target even when the allocation failed | game-ai |
| AICharacterHands::Init | 0x00022930 | no NULL check before SetOwnerObject; SetAICharacter(NULL) on allocation failure | game-ai |
| AICharacterEnemyGround::HandleInterrupts | 0x0001f3c0 | InRegion before the NULL check | game-ai |
| GHud::DrawPauseMenu | 0x000dd340 | a hint whose text has no button marker leaves its button NULL and dereferences it | game-frontend |
| GFXGallery::CANVAS_Add | 0x000d47a0 | tests the canvas's address (never NULL) for NULL | game-frontend |

### 3.4 Leaks and wrong frees

| Function | Address | Quirk | Doc |
|---|---|---|---|
| ActEvents::Destruct | 0x000156c0 | leaks the physics-off handler made in Construct (0x000157e0) | anim |
| ActWeaponDatabase::Destruct | 0x0001bcb0 | frees every weapon it loaded whatever the reference count says | anim |
| AFader::Remove | 0x00125d60 | frees the fader whatever its reference count | audio |
| ASnowMobile::PlayMotor | 0x0012aa10 | a finished rev voice is overwritten by a new one without being freed | game-audio |
| AWorldSound::Destruct | 0x0012e090 | does not free its pass voice | game-audio |
| ATurret::Destruct | 0x0012e250 | uses the out-of-line AVoice destructor (0x0003f400) | game-audio |
| RReflection::ReflPrivateData::Destruct | 0x000982a0 | frees the lighting records with delete, not delete[], and leaves their names | render |
| RDecalManager::Destruct | 0x0009b3c0 | frees its GeoPrims and keeps their addresses in the table | render |
| RColorize::Kill, RDecalManager::Kill, RGain::Kill, RLensFlareManager::Kill | 0x0009a480, 0x0009af50, 0x0009de80, 0x0009e1d0 | leave the instance pointer set | render |
| RMissileStreak::Kill | 0x000a1460 | frees the GeoPrims without clearing the pointers | game-effects |
| RSceneObj::Destruct | 0x000905a0 | writes 0xabababab into its next pointer after unlinking itself | render |
| RVehicle::Destruct | 0x00096430 | deletes `particles` without clearing it | game-effects |
| WRender::ShutDown | 0x000c7d50 | deletes the object without its destructor, so the curtains leak | world |
| WWorld::Close | 0x000d1b30 | leaves the scene objects, the sound group and the trigger manager global set | world |
| OptionParser::Construct | 0x0005b870 | a match not at a line start leaks that attempt's copy of the key | core-data |
| EncryptDecrypt; GameStd::Numpunct::Tidy | 0x0011c5c0, 0x0003abd0 | delete where delete[] belongs | core-data |
| EAGL::RenderMethod::Destruct | 0x000f12c0 | frees shader arrays from EAGLMalloc with __builtin_delete | eagl |
| EAGL TAR image | 0x000ec420 | when the SHAPE has nowhere to keep a record, the one allocated here is replaced (and leaked) by Create's | eagl |
| SNDPKTPLAY_stop | 0x0013f040 | frees the per-channel stretch data without clearing the pointers | sound |
| AIVehicleController::Shutdown | 0x000364d0 | the agent and helicopter arrays are destroyed but never freed; the static player and traffic AI are destroyed here and again at exit | game-ai |
| AICharacterPedestrian::Destroy | 0x00023fd0 | frees the navigators and grounds only | game-ai |
| AICharacterHands::Shutdown | 0x000227c0 | forgets the hands and their Human, does not delete them | game-ai |
| AIElementController::GoToSleep | 0x000287d0 | only the Humans are deleted | game-ai |
| AICharacterEnemyGround::Destruct, AICharacterEnemySunroof::Destruct | 0x0001dea0, 0x00020ec0 | ownerType = 0 before RemoveReference | game-ai |
| GHud::Destruct | 0x000dc420 | leaves fHudActionQueue and fHudView pointing at the freed objects | game-frontend |
| GHud::DrawPauseMenu | 0x000dd340 | leaks the objectives page's line-count array when the objective has gone | game-frontend |
| GGirl::KillGirl | 0x000d6f20 | leaves GirlMaterials pointing at the freed materials | game-frontend |
| GLoadingScreen::Shutdown | 0x000e2a70 | clears the screen only when it is not kept, and frees the picture and font only when it is; LoadingFont and LoadingFontFile are left dangling | game-frontend |
| GGallery::Destruct, GGallery::FONT_Load | 0x000d6e30, 0x000d5a80 | freed pointers are not cleared | game-frontend |
| GFXGallery::Destruct | 0x000d5170 | frees a gallery loaded with FileLoad through UMemory::Free | game-frontend |
| GFX::Shutdown | 0x000d35d0 | leaves TheEffectsGallery pointing at the freed gallery | game-frontend |
| GSubtitleSplitter::Reset, GSubtitles::ClearSubtitles, Destruct, LoadSubtitles | 0x000e33e0, 0x000e3630, 0x000e3730, 0x000e3840 | scalar delete of blocks from operator new[] | game-frontend |
| GSubtitles::Destruct | 0x000e3730 | does not clear font and fontFile; frees through MEM_free's second copy (0x00117110; in our build the linker folds the two copies into one function) | game-frontend |
| std::_Mutex::_Mutex | 0x001313fe | does not check its critical section's allocation | c-runtime |
| std::exception's copy, bad_cast | 0x0013263c, 0x001326ae | a failed copy of the message leaves none but still marks one to free | c-runtime |

### 3.5 Dead stores, dead code, arguments ignored

| Function | Address | Quirk | Doc |
|---|---|---|---|
| PBondCar::InitAudioObject | 0x0006cc20 | builds "SFX_<engine>LdEn" and never uses it (the port leaves the buffer out) | vehicles |
| PBondCar::Simulate | 0x0006a840 | asks for the car's class twice and drops the first answer | vehicles |
| PHelicopter::ApplyDamage; PHelicopter::Simulate | 0x0006e330, 0x0006dd30 | gathers the zones at damage stage 4 and does not send them; computes a dot product it does not use | vehicles |
| Newton::Simulate | 0x00060b70 | computes a midpoint, its distance and a clamped height and uses none of them | physics |
| RigidBody::UpdatePositionAndOrientation | 0x000b0ec0 | ignores its time-step argument and uses the global step | physics |
| AttributeSystem::ConfigEditParameters; PVehicle::InitializeGlobals | 0x000525f0, 0x0006f930 | empty in the retail game; each CarPhysics field's range is still registered | vehicles |
| StateTagAlphaBlendXbox | 0x00091db0 | looks source, destination and operation all up among the operations (0x001f28f8), so the first two are never found and nothing is set; the blend factor table (0x001f2878) is never looked up | render |
| RGlareManager::DrawGlares | 0x000aa5d0 | flips GlareDrawParity (0x00209d28) every call; nothing reads it | render |
| RLightning::BeginBoltDraw | 0x0009f520 | joining one bolt's strip to the next, jitters a pair of points into locals and throws them away (the random tables step all the same) | render |
| RDebris::DrawShellCasings | 0x000a9680 | computes an inverse it never uses | game-effects |
| RVehicle::StartColourDissolve, TriggerIlluminate | 0x00095e20, 0x000959b0 | ignore both arguments / the third | game-effects |
| RVehicleParticle::TriggerSparks | 0x000a6f70 | never reads its float | game-effects |
| REmp::Init, DrawShootingEffect | 0x0009b680, 0x0009bb30 | the sphere is never drawn (counts set, no position or texture coordinate pointers); the EmpLightParams record Init fills is read by nothing | game-effects |
| record 0x001c54f8 | 0x001c54f8 | a particle type record nothing references | game-effects |
| ASnowMobile::PlayCarving; ATrafficVehicle::PlaySnowmobileSpecifics | 0x0012b060, 0x0012ceb0 | the landing one-shots are built on the stack and destroyed at once, so never heard | game-audio |
| AVehicle::Play | 0x0011f9b0 | the base Play (0x00017550) it calls is empty; the call is left out | game-audio |
| ARaceEngine::Play | 0x0012ffb0 | asks for the mix's volume twice (in the crossfade branch) | game-audio |
| AMenuSound::Trigger(int, const char *, const char *, int) | 0x0011e010 | looks its patch up twice | audio |
| AFader::Create, AStream::Create | 0x00125cb0, 0x001236c0 | look a name up and ignore the answer | audio |
| AStream::DecodeError | 0x001224d0 | adds 19 to its argument and discards it | audio |
| RPlayerCamera::SetOldCameraMode | 0x00089200 | constructs a whole RPlayerCamera on the stack, runs the target mode's update on it and destroys it | camera |
| RWorldCamera::GetAnchorAcceleration | 0x000972c0 | calls GetRigidBody three times | camera |
| RPlayerCamera::UpdateDashboardCam | 0x00081d10 | repeats a look-back test it has already returned from | camera |
| WWorld::Reset | 0x000d1750 | looks up 'Wmap' and never uses it | world |
| WTriggerManager::Restart | 0x000cff90 | builds a "Validating stream" line (ValidateStreams, inlined) and never prints it | world |
| WRoadNetwork::Init | 0x000c8950 | fills 32 lane-weight row pointers (from 0x001ca0c0 or 0x001ca200) | world |
| WRoadNetwork::GetLinePointIntersect | 0x000c9000 | normalises its direction twice | world |
| Sentry::Construct | 0x00073a90 | calls GetSimpleRigidBody once and discards the result | game-weapons |
| Missile::Destruct | 0x0005f350 | the empty OBB and RMissileStreak destructor calls (NullFunction) are kept | game-weapons |
| RayShell::AddFlash | 0x000720d0 | never reads `this` | game-weapons |
| ActWeapon::Construct | 0x0001abd0 | sets the manual-render flag, so its vtable Render (0x0001ae60) never draws | anim |
| SNDSTRMI_create | 0x0013c350 | hands STREAM_buffersize / 3 to SNDSTRM_setgreedylevel with the STREAM pointer where a stream index belongs, so the level is never set | sound |
| EAGL runtime allocation | 0x000ed390 | GetAddr's answer is looked up and thrown away (code no shipped data reaches) | eagl |
| Simulation::FindPhysicsObjectSignature | 0x000b27d0 | reuses its argument's slot as a bool (no effect) | game-missions |
| Simulation::SpawnExplosion, SpawnExplosionObject, CanSpawnSimpleRigidBody | 0x000b4cd0, 0x000b5d50, 0x000b47a0 | the eighth argument, the second argument and the position are not read | game-missions |
| SMissionManager::GetObjectiveArrowHeight, CheckObjectiveVisibleToUser | 0x000b6cd0, 0x000b6d50 | a dead load of the element's +0x77; a dead copy of the objective | game-missions |
| SMissionManager::UpdateCharInfo | 0x000b6b20 | clears CharInfoBeforeHud (0x001c8fc4), which nothing reads | game-missions |
| SMissionRule::RunEvents | 0x000b9500 | a dead store of -1 to the event index; re-reads the count each pass | game-missions |
| SRuleRange::CheckRule | 0x000ba570 | stores the distance in `last` and never uses it | game-missions |
| the SRule* constructors | 0x000b96c0-0x000ba080 | write SMissionRule's vtable, then their own | game-missions |
| EAIElementSetNonScoreable and two other element events; ECustomTransition | 0x0003e0eb, 0x0003e245, 0x0003f096; 0x0004a9d0 | answers asked for and not used (GetCarpElementTracking); MEM_totalunused called twice for nothing | game-events |
| EAICommand (CommandGroundVehicle); EKickAIElement::Process | 0x0003f769, 0x00040777 | a dead load of the element's +0x77 | game-events |
| EFireWeapon::Process | 0x0004af3f | asks for the owner's rigid body twice and ignores the second | game-events |
| EForceNearPedRespawn::Construct | 0x00040176 | a second AIRoadSpawn::Get, ignored | game-events |
| ERandomExplosion; EScheduleEvent; EPlayerLose, ESetAutoDriveCamOffMovie (Process) | 0x000430e0, 0x00043740, 0x0004cf50, 0x0004d850 | the record's +4 and +8 are not read; EScheduleEvent's +0xc and EPlayerLose's +8 are not read; EPlayerLose and ESetAutoDriveCamOffMovie ask for GetPlayerHealth and GetAudio twice | game-events |
| ESetCreakLevel::Process | 0x0004d985 | copies the car's CarPhysics (0x100 bytes) to the stack to read one field | game-events |
| ESpawnSmackable::Process | 0x00045abb, 0x00045ac3 | a 'Name' lookup and a GetArray whose answers are not used | game-events |
| EWakeupSmackable::Process | 0x00046d95, 0x00046da1, 0x00046e77, 0x00046eb4 | GetVelocity twice; a GetScalarVelocity and a v3length thrown away | game-events |
| ESpawnTraffic::Process; ESetRenderVariation::Process | 0x00045f16, 0x0004454b | asks for each traffic car again for every call; dereferences the model twice | game-events |
| AIGroundVehicle::UpdateNavPos | 0x0002a3e0 | an unused WRoadNetwork::Get | game-ai |
| AIGroundVehicle::DoAttackMode | 0x0002bf40 | chase fetches a body it does not use | game-ai |
| AIGroundVehicle::DoAgentStateUpdate | 0x0002cd80 | a dead re-test | game-ai |
| AIGroundVehicle::DoPlayerLogic, TestForCollision | 0x0002a7c0, 0x0002b4d0 | an unused body; the body fetched twice | game-ai |
| AIGroundVehicle::DisengageSplinePath | 0x0002a850 | clears the spline path twice | game-ai |
| AIGroundVehicle::DrivableVec, BarriersInPath, GetDirectLaneWeights | 0x0002a9e0, 0x0002aaf0, 0x0002b7a0 | an unused WWorldPos each | game-ai |
| AIGroundVehicle::GetBestWeightedLane, GetLookAhead, CheckAgentRoadNetworkCollision | 0x0002b5e0, 0x0002dc90, 0x0002fde0 | the argument is not read (GetLookAhead also makes an unused heading) | game-ai |
| AIGroundVehicle::GetTargetPos | 0x0002a3c0 | a dead load | game-ai |
| AIGroundVehicle::Reset | 0x00029d60 | leaves the modes as they are | game-ai |
| AIGroundVehicle::DriveToPoint | 0x0002f610 | sets the hand brake false twice | game-ai |
| AIGroundVehicle::CheckAgentDirectCollision | 0x000306b0 | excludes the target twice | game-ai |
| AIGroundVehicle::DoTrafficStateUpdate | 0x0002f4c0 | the road counts through the out-of-line functions when it switches roads, the first registration inlined | game-ai |
| AIHelicopter::DoAgentLogic | 0x000331c0 | the chase fallback's rocket branch cannot run; the target's body is fetched a third time, unused; LeftDrivableDist's answer dropped | game-ai |
| AIRoadSpawn::GetRandomTrafficSpawn | 0x00035130 | the player's body fetched and not used; dead hit-point and lane reads | game-ai |
| AIHelicopter::ManeuverToPos | 0x00031cd0 | an unused flat-velocity unit vector | game-ai |
| AICharacterPedestrian::UpdateThreatRelativeDirection | 0x00023e60 | fetches a forward vector it does not use | game-ai |
| AICharacterEnemyGround::DoWandering, DoAiming, DoFiring, TargetIsAcquired | 0x0001ee60, 0x0001f000, 0x0001f0c0, 0x0001f8f0 | GetWeaponPosition called and its answer not used | game-ai |
| AICharacterEnemy::TargetIsAcquiredBase | 0x0001ca90 | GetRigidBody called twice | game-ai |
| AICharacterBond::GetRotPos, GetRideRotPos | 0x0001cc30, 0x0001ccc0 | GetRigidBody called 4 and 5 times; the argument is not read | game-ai |
| AICharacterPassenger::Init | 0x00023700 | the passenger's description is loaded even for the helicopter | game-ai |
| AICharacterEnemySnow::PlayZoneInjuryAnim | 0x00020870 | the death path makes no sound | game-ai |
| GHud::TriggerTimerOff | 0x000d8810 | ignores its argument | game-frontend |
| GHud::SetMissionPassed | 0x000dee80 | ignores its argument | game-frontend |
| GHud::DrawFadingCrossHairs | 0x000d8ea0 | calls GetSelectedTarget and drops the answer | game-frontend |
| GHud::dbDrawTarget | 0x000da8e0 | the spread is multiplied by 0.0; the third argument is not read | game-frontend |
| GHud::DrawPauseMenu | 0x000dd340 | the title's y is y - 4 + 4 | game-frontend |
| GGallery::GET_Puppet; GFXGallery::CULL_SetCanvas, PARTICLE_Add, DEBRIS_Draw | 0x000d5810; 0x000d3b30, 0x000d43a0, 0x000d4b90 | arguments not read | game-frontend |
| GGirl::UpdateTexture | 0x000d6f60 | writes palette speeds that nothing reads | game-frontend |
| GOrthoHudView::DoRender | 0x000e1ce0 | calls GetFoundationVideoMode and drops the answer; keeps the view port in HudViewPort (0x0023f410), which nothing reads | game-frontend |
| GLocale::GetSubDirectory | 0x000e3140 | calls GetFoundationVideoMode up to three times | game-frontend |
| GSystem::LOCALE_Symbol | 0x000e3af0 | the show-symbols path is dead (nothing sets LocaleShowSymbols) | game-frontend |
| atexit, `_onexitinit` | 0x00132a7b, 0x00132a1b | the exit list is filled (and grown) and never run: only exit() would, and nothing calls it | c-runtime |
| `_exit` | 0x0013438b | ignores its code: doexit ends in KeBugCheck(0) | c-runtime |
| `_Tolower`, `_Toupper` | 0x00130c00, 0x0013143d | ignore the ctype table they are handed and test the C locale's classes | c-runtime |

### 3.6 Values that never change

| Where | Address | Quirk | Doc |
|---|---|---|---|
| Missile::Construct | 0x0005e8c0 | the transit sound's pitch steps by 0.01 from 1 and wraps at 0.04, so it is always 1 | game-weapons |
| RSniperZoom::SetupVerts | 0x000a6a80 | the outer ring's outer grey (0x00201830) is never written, so always 0 | game-effects |
| PBondCar::ProcessPhysics and the landing gates; PBondCar::ImproveLanding | 0x00067bd0, 0x00062fe0 | the steps in the air before ImproveLanding (0x001c36c4, 50) and before it damps the spin (0x001c36c8, 10) are never written | vehicles |
| RVehicleParticle::TriggerSparks | 0x000a6f70 | adds a static (0x0020183c) nothing writes; that turns a -0 height into +0 | game-effects |
| REmpBolts::Update | 0x0009d6b0 | numBolts never rises, so it tries up to four spawns (one per step passed) every call | game-effects |
| RVehicle::RenderFX | 0x00095b60 | clears empVictim; ClearEmpVictim (0x001f2c6c) is never set | game-effects |
| SWeaponManager::Reset | 0x000bad70 | primary is -1 and nothing sets it | game-weapons |
| SMissionManager (MissionHealthTimes100, the car-override flags, gPlayerCanNeverLose) | 0x00239218-0x0023921e | never set; ResetCharInfo, GetCarType, EPlayerLose and SWeaponManager read them | game-missions |
| SMissionManager::CalcEndMissionScores | 0x000b6e80 | score 4 is value4 * 0.0 | game-missions |
| DAudio | 0x00036d10 | DAudioMixerVisible (0x001e4760) and DAudioMixerEditing (0x001e2270) are never set | game-audio |
| gLimitHenchSpawns | 0x001b64ec | 1, and nothing writes it: vehicle elements always wake within the hench limit (EActivateAIElement, EMaybeActivateAIElement) | game-events |
| EFireWeapon::Process | 0x001c3df0 | bullets are ray shells: the byte is 1 and nothing writes it, so the shell branch is dead | game-events |
| AICharacterPedestrian::GetRandomPedTexture | 0x000240e0 | round-robin, not random | game-ai |
| AIGroundVehicle::UnSpawn | 0x0002dac0 | the parking offset saturates at 5000, so after about 100 unspawns every car parks at z = 45000 | game-ai |
| AIGroundVehicle::SetNavigateMode | 0x0002a0d0 | the height phase runs 0 to 1.25 in steps of 0.25, then back to 0 | game-ai |
| AIZoneController::Init | 0x00036a20 | type 0's entries are never set | game-ai |
| AIZoneController::RegisterZone | 0x00036bb0 | types 0 and 1 (mergeSteps 0 or less) never register; only the first activeCount zones are checked | game-ai |
| AIElementController::GetProperty ("EMPTY", "BLOWOUT") | 0x00027ce0 | EMPTY is true for a vehicle with no members; NaN hit points count as alive for BLOWOUT | game-ai |
| AIElementController::GoToSleep | 0x000287d0 | a helicopter always counts as intact | game-ai |
| GHud::DrawCrossHairs | 0x000e0440 | the sniper scope's tick spacing is multiplied by 0, so every tick is drawn at one place | game-frontend |
| GGirl::UpdateTexture | 0x000d6f60 | the second palette offset never moves | game-frontend |
| rand | 0x00133ee0 | srand is never called (the XBE has none), so each thread's sequence starts from seed 1 | c-runtime |
| malloc, calloc, realloc | 0x001340e3, 0x0013439d, 0x00134815 | the new handler and its mode (0x00244ab8, 0x00244abc) and `__active_heap` stay 0 | c-runtime |
| `__lc_handle`, `__lc_codepage` | 0x00244ac0, 0x00244ad8 | the C locale's; setlocale (0x001347c2) knows only "C" | c-runtime |

### 3.7 Odd branches and data bugs

| Function | Address | Quirk | Doc |
|---|---|---|---|
| ACharacter::Say | 0x0011dad0 | two speech lists lack a comma between two names, which join, so the lists end in NULL entries and AIndex::Lookup(NULL) is called; the "F" lines' wrapped flag is never set, so Say loops until the bank has a line | game-audio |
| RayShell::ConsiderVictim | 0x00071e40 | the non-human second-victim test can never pass | game-weapons |
| RayShell::Fire | 0x00072d60 | case 2 damages the second victim at the first victim's impact point; a missed shot's tracer runs muzzle to muzzle + dir x range | game-weapons |
| RayShell::AddNewRayShellForTriggerCheck | 0x00071d80 | queues at most 63 rays | game-weapons |
| Shell::ApplyWorldEffects | 0x000749b0 | the "straight up" test is a four-component dot with (0, 1, 0, 1) | game-weapons |
| Shell::Simulate; Mine::Simulate; Smackable::Simulate | 0x00074e80, 0x0005e710, 0x00075420 | carry on (PhysicsObject::Simulate) after deleting the object, on two of Shell's three paths | game-weapons |
| Grenade::Simulate | 0x0005d7d0 | the first bounce sets off a type-3 explosion | game-weapons |
| STREAM_release | 0x0014b9f0 | knows a released chunk only by its tag (-2): a stale pointer to bytes the ring has since reused marks the new chunk skipped with its reader byte still in the size word, and the next walk over the ring (FUN_0014b730, STREAM_kill) jumps 16 MB past it | sound |
| STREAM_kill | 0x0014bcb0 | on a stream waiting for space with nothing unreleased and its ring starting past ringBase, puts parsePos and ringStart back to ringBase but leaves readPos past them; the next walk from readPos to parsePos runs off the ring | sound |
| SndStream_Deliver | 0x0014ad20 | marks a chunk no filter sends to a reader skipped before testing it for the end tag, so an end chunk sent nowhere never ends the request: the parse runs on into what follows in the file, taking any size word with a top byte for an end chunk and writing one there. Unreachable: the game never calls STREAM_setfilter, and the default catch-all sends every tag to reader 1 | sound |
| RayShell::CollideWithWorld | 0x00072790 | the segment's w grows by one each step | game-weapons |
| Missile::SteerMissile | 0x00060110 | overwrites detonate when there is a target; a torpedo drops its target once it faces away | game-weapons |
| Missile::Render | 0x0005f3d0 | the light is scaled by the square of twice the steps left, compared unsigned | game-weapons |
| Sentry::RotateSRB, RotateAnim | 0x00073730, 0x000738b0 | set the turning flags by comparing the new angle +-0.001 with the previous angle, not the target | game-weapons |
| Sentry::ComputeNewRotation | 0x00073600 | returns `limit` on one branch and `float(1 - limit)` on the other | game-weapons |
| Mine (every explosion) | 0x0005e180-0x0005e8c0 | always uses the proximity mine's damage (weapons[kProxMine]) | game-weapons |
| SWeaponManager::Update | 0x000bbdc0 | the secondary-fire event 0x16 is not sent for the remote torpedo | game-weapons |
| Simulation::CanSpawnRigidBody | 0x000b2c40 | reads the global Simulation's bodies, not this one's; only smackables not awake are candidates; at exactly the limit nothing sleeps and the answer is no | game-missions |
| Simulation::DeleteDeadObjects | 0x000b3fc0 | reads the object's type before its NULL check (when there is a camera) | game-missions |
| Simulation::UntrackInstance | 0x000b3000 | an inlined erase(remove(...)) that erases one element | game-missions |
| Simulation::DeletePhysicsObject | 0x000b4250 | walks only the first `count` nodes of the type and, if the object is not among them, puts the node after them (perhaps the end) in the dead set; erases from the vectors other than the cars' without checking remove found it | game-missions |
| Simulation::CanSpawnSimpleRigidBody | 0x000b47a0 | deletes the newtons through the global Simulation while testing this one's counts; the count only falls at DeleteDeadObjects, so every newton goes | game-missions |
| Simulation::Construct | 0x000b4fe0 | the grenades' and the tracked instances' vectors get no reserve | game-missions |
| SMissionManager::PassObjective | 0x000b87f0 | adds the score to glbMissionManager's scores, not this one's | game-missions |
| SMissionManager::IncShotsHit | 0x000b6720 | the hit window is the step count / 10, a tenth of the step count rather than of a second | game-missions |
| SMissionManager::Load | 0x000b9140 | writes 1.0 into the level's mission-set data when enemyGlueFactor[0] or missionText[0] is 0 | game-missions |
| SMissionManager::CalcEndMissionScores | 0x000b6e80 | score 3 is the set's unknown2e0 over the shots; score 9 is the mean of the accuracy and score-3 shares | game-missions |
| SMissionManager::AddObjective, InsertObjective | 0x000b9320, 0x000b93c0 | test size == 1 after the insert (with an unreachable NULL-head path) | game-missions |
| SRuleDeath::CheckRule | 0x000b9850 | target types other than the player and an element read health 0, so the rule passes | game-missions |
| SRuleDamage::CheckRule | 0x000b9770 | with the AI controller's figures compares the figure with the threshold and never updates lastHealth | game-missions |
| SRuleRange::CheckRule | 0x000ba570 | a plain comparison, no crossing test | game-missions |
| SMissionRuleTarget::GetSpeed, GetPos, GetMatrix, GetProperty | 0x000ba390, 0x000ba3e0, 0x000ba430, 0x000ba4a0 | an unknown target type answers 0, leaves the output alone, or answers false | game-missions |
| CompareWithinDegrees | 0x000b9ad0 | a NaN skips the wrap and passes both tests, so it answers 1.0 | game-missions |
| AVehicle::CalculateSpeed | 0x0011eb50 | with a horn, a speed between 5 and 14 (exclusive) becomes 14 | game-audio |
| AVehicle::PlayCollision | 0x0011f1a0 | on paved ground plays the tyre sound when `s0 \|\| (s1 && s2) \|\| s3` | game-audio |
| AGun::Play | 0x00129250 | fires only while the firing byte is exactly 1 | game-audio |
| ASmackable::Play | 0x0012da90 | the roll volume rises by two steps and falls by one | game-audio |
| DAudio::MixerDraw, InfoDraw, UpdateTargets | 0x00036d10, 0x000371b0, 0x00037750 | editing mode pauses the game every frame; mix and patch names passed as format strings; the camera's unknown50 passed as the listener's velocity | game-audio |
| DAudioTargetList::Erase | 0x00037160 | one function for the target list and WGrid's movers (the linker kept one) | game-audio |
| AFX::Pause | 0x00124850 | sends to bus 0x40, not AFX's own | audio |
| ASoundManager::ClearMission | 0x00121280 | asks for the streams as "music"/"speech" in lower case | audio |
| AIndex::Construct | 0x001276c0 | sizes its name block in a first pass and can overflow it when a prefix is not three characters; a file name with no '.' gives strncpy a negative count | audio |
| AMix::Load | 0x0011d790 | unbounded strcpy of the name | audio |
| ActGlobalPoseOverrideArray::SetGlobalPoseOverride | 0x00015b40 | in global mode swaps rows 0 and 2 of every override; index 0 then negates row 2 and swaps rows 1 and 2, index 1 negates row 0 and swaps rows 1 and 2 | anim |
| LightBlock::GetLight, SetLight | 0x000148a0, 0x000148f0 | Get returns the colour rotated (y, z, w, x), Set stores it straight | anim |
| ProcAnimSway | 0x0008af60 | reads the instance position after clearing it, so instances of one size sway in sync | anim |
| ActCharacterInfo::Construct | 0x00015260 | leaves its type unset for a model letter other than h/p/c/a | anim |
| RWorldCamera::GetAnchorLinearVelocity | 0x00097fc0 | uses the player car's AI spline path velocity for any rigid anchor | camera |
| RPlayerCamera::LimitPitchYaw | 0x00083190 | reuses a stale band middle when the bands end early | camera |
| RViewCamera::AspectRatio, SetWorldTransformMode | 0x00096960, 0x00096eb0 | the 4:3 aspect constant is 0x3faaaaaa, one unit below the nearest float to 4/3 | camera |
| InputConfigManager::GetLocaleID | 0x000501a0 | swaps some labels for 2721 by track: 1424 off "snow1a_mis3", 2708 on tracks with "uw_" in their name, 2712/2713 and 2723 on "paris_mis01" outside the driving type (2723 becomes 1709 for configurations 6 and 7), 2719 everywhere | input |
| InputConfigManager::ParseMasterConfigFile | 0x000502d0 | reads the first line (the .def parsers skip it); a name before any section goes to configs[2] | input |
| IFeedback::Update, Pause; IFeedback::Construct | 0x0004fbc0, 0x0004fa00, 0x0004fe30 | Update stops the motors and zeroes the durations but leaves the levels; Pause keeps each port's user count; the thread gets a static 0x2000-byte block (0x001e2430) that THREAD_create ignores | input |
| RigidBody::UpdatePositionAndOrientation; Construct, ResetObject | 0x000b0ec0; 0x000b0b20, 0x000adcd0 | builds the inertia matrix as transpose x scale x rotation; the constructor and ResetObject build it the other way round with 3x4 multiplies | physics |
| RigidBody::ControlSleep | 0x000b09d0 | never puts an upright vehicle to sleep | physics |
| PhysicsObject::ApplyForces | 0x0006f2a0 | ORs 1 into the word that also holds the body's radius; the flag tests read that bit | physics |
| Newton::Simulate | 0x00060b70 | normalises the quaternion by its squared length, and finishes its step on a body it has just deleted | physics |
| SimpleRigidBody::CheckCollisions | 0x000b1ff0 | a helicopter is tested higher depending on a byte of the checking body's owner; a body's own box is twice as large against rigid bodies as against simple ones | physics |
| RigidBody::InitLevers | 0x000ad840 | divides the mass by BASE_FRICTION_MASS (0x001c36d0), the cars' tuning global, not a constant 8 | vehicles |
| PBondCar::SetVisualDamage; InitializeCarVariables | 0x00066180, 0x00062440 | compares with the shared damage levels (0x001c3db4) that ApplyDamage uses, of which InitializeCarVariables clears `spread` | vehicles |
| PBondCar::CalculateRPM, ProcessPhysics, ProcessSubmarinePhysics | 0x00062a60, 0x00067bd0, 0x000644c0 | the gear bytes are signed (previousGear starts at -1); CalculateRPM keeps the previous gear on a downshift while gearChangeTimer (15 steps) runs; the reverse timer, the submarine's roll and the forced roll direction are compared signed | vehicles |
| PBondCar::Simulate | 0x0006a840 | the mission editor's switches test only bit 0 of each mask, shifted; class 3 cars beyond 0x001c36c0 (2500) simulate every other step | vehicles |
| RVehicle::ComputeWheels | 0x00096060 | asks for the wheel spin angles of 0 and 1 only: one for the front pair, one for the rear | vehicles |
| RaiseLandingImpact (inlined in ProcessSnowmobilePhysics, ProcessPhysics, ProcessSimplePhysics) | 0x00063ad0, 0x00067bd0, 0x00069710 | the same hard-landing code three times; one function here | vehicles |
| RReflection::RenderView | 0x000997d0 | stores the float 4080.0 in the fog's mode word while it draws | render |
| RVehicle::Render | 0x000965e0 | the projected shadow is drawn only when the view distance is below 0.125 | game-effects |
| RDebrisType::Draw | 0x000a92a0 | the colours are always DebrisColours[0] (0x00202a90); a piece fades by (SimStep - lifetime) / its throw tick | game-effects |
| RTyreTrack::Construct | 0x000abfc0 | initialises the last point's Coord3 with a four-word init, so 1.0f lands in its joined word | game-effects |
| RVehicleParticle::UpdateAll | 0x000a8120 | skips when SimStepCount equals the last tick but stores the game tick | game-effects |
| RVehicleParticle::UpdateCar | 0x000a7ac0 | snowmobiles throw no snow spray | game-effects |
| REmpBolts::TrySpawnBolt | 0x0009d320 | a segment's end has w 2 | game-effects |
| RMuzzleFlash::Init | 0x000a1b50 | turns its static model a further quarter round each run | game-effects |
| RBulletStreak::Init | 0x00099f10 | vertices 2 and 3 get v = 0.1 (its switch on v % 6, jump table 0x0009a1ac) | game-effects |
| RMissileCam::Draw | 0x000a0b50 | points its GeoPrim at its own stack | game-effects |
| RSniperZoom::Draw | 0x000a6bc0 | sets the shared material's state, not its own | game-effects |
| WWorld::Open | 0x000d21a0 | compares the 'Audi' lookup with the end of the root group, not the 'Map ' group's, so the check never fails | world |
| WTrigger::Size; WTriggerManager::Process | 0x000cf1f0, 0x000d0630 | the only caller calls it on a CARP::Instance | world |
| WTriggerManager::CheckCollide; WTrigger::UpdateRotPos | 0x000cf520, 0x000cf490 | box triggers keep their depth as a float in the packed-size word, never register a ray shell thinner than 0.01, and become cylinders in UpdateRotPos, which ignores its rotation | world |
| WRoadNav::PathForwardRoadSegment | 0x000c9810 | the inner loop compares the outer index's neighbour | world |
| WRoadNav::FindClosestSegmentInd | 0x000cbf20 | returns 0, not -1, for an empty grid cell | world |
| WRender::PrepareForCull | 0x000c75b0 | clears the draws of the list the tree walk filled last, not of its argument | world |
| WRender::Construct | 0x000c7a50 | an instance without an article joins the pass of the instance after it; the curtain count is left unset on a track with no 'VisC' | world |
| DAFI_setsectionbyindex | 0x0011a390 | a key line without '=' loops forever; an empty value takes the next line as its value | core-data |
| SNDSTRMI_parsedata | 0x0013bdb0 | writes the request id over the chunk's tag, and the chunk's address just before its first channel's data | sound |
| EAGL::Model::ConstructCopy | 0x000e9540 | +0x08, +0x4c..+0x9b, +0xac and +0xc8 are not copied | eagl |
| EAGL::Model::MorphModel | 0x000ea480 | an item whose element size does not match is skipped by its unrounded size in words; a group's input items are stepped as 8 bytes plus data, not 16 | eagl |
| EAGL_RMOp_DynamicStream | 0x000f6890 | the buffer is created 4 times the array's size and the capacity stored as count x 4 | eagl |
| EAGL view activation | 0x000e4be0 | a RenderContext's "previous" view is put back, a TextureRenderContext's left 0 | eagl |
| EAGL::UnRegisterShapes | 0x000edf50 | looks names up by the directory name padded with spaces, so a name RegisterShapes took from the long name or cut short is never removed | eagl |
| EAudioUpdate::Process | 0x000499bf | the listener filter between the band's edges is (band width + level + y) x 0.1; the listener is allocated 0x80 bytes for an AListener of 0x78 | game-events |
| EAICommand::Process | 0x00048ffc | disables a trigger whose flags lack kRigidType1 and carries on (to 0x00048f77) | game-events |
| ECollision::Process | 0x0004a43d-0x0004a453, 0x0004a53a | the second side tests unknown4a for 5, 0x50 and 0x30 but `tag` for 0x40, and has no corner rotation and no rumble; a non-player collision's incidental timer is the later of the two | game-events |
| EActivateAIElement, EMaybeActivateAIElement (Process) | 0x000495d0, 0x0004cbc0 | print "The Helicopter hit the Trigger." through the C runtime's printf | game-events |
| EFireWeapon::Process | 0x0004bfc9, 0x0004b276 | the snowmobile and FOD guns use the ultralight gun's damage; for the player's aim the camera's position replaces the muzzle, while ray shells keep the original origin | game-events |
| EFireEventList::Process; EMuzzleFlash::Process | 0x0003fcd5, 0x00040db1 | compares its count unsigned; the flash's "which" is position.x < 0 | game-events |
| EMuzzleFlash::Process | 0x00040d48-0x00040d5b | after placing the flash, zeroes the x, y and z of the instance frame's translation row, so the flash's direction is turned by the frame's rotation alone | game-events |
| EBashCarAlongObjectAxis::Process | 0x00049bee | zeroes the w of the y row in its copy of the instance's matrix, so the impulse's direction has w 0 | game-events |
| ERandomExplosion::Process | 0x000430e0 | y + 1.5 before the height lookup and + 1 after | game-events |
| ERollSub, ESetAreaBrightness, ESetCollisionGeometry, EPlayEffect (Process) | 0x000435c1, 0x000439b0, 0x00043f01, 0x0004254f | write their computed values back into the event | game-events |
| EPlayerLose::Process | 0x0004cf9f | makes an ERestart inside its own destructor | game-events |
| EStreamEvent::Process | 0x0004efd4 | compares the name with "nis" before its NULL test (0x0004efff) | game-events |
| ESpawnExplosion, ESpawnForceEffect (Process) | 0x0004e173, 0x000455a9, 0x000456ae | the "near up" test is a four-word dot; ESpawnForceEffect leaves its effect field pointing at a stack ArticleEffect | game-events |
| ESetReflectiveObject::Process | 0x00044475 | uses the CARP element without redirecting it | game-events |
| EStartEffect, EStopEffect, ESuppressEffect (Process) | 0x0004609a | 1 << (zone + 16), the shift taken mod 32 | game-events |
| ESpawnTraffic::Process | 0x00045f16 | the spawn counter goes back to 0 past 32767 | game-events |
| AIGroundVehicle::UpdateNavPos | 0x0002a3e0 | a sub with followTargetHeight measures in xz | game-ai |
| AIGroundVehicle::DoAttackMode | 0x0002bf40 | modes 9, 10 and the unknown ones set speed 0 | game-ai |
| AIGroundVehicle::DoAttackMode | 0x0002bf40 | chase calls a charge off when the target is within the two cars' half lengths (x1.25), not within the range it then uses (the larger of that and the attack range) | game-ai |
| AIElementController::CalcMightAsWellBeDead | 0x00028730 | counts every vehicle element with the flag, awake, asleep or dead, and its riders with hit points | game-ai |
| AIGroundVehicle::DoAgentStateUpdate | 0x0002cd80 | the zone loop has no break, so the event can fire once per zone | game-ai |
| AIGroundVehicle::BarriersInPath, GetDirectLaneWeights | 0x0002aaf0, 0x0002b7a0 | the second probe reuses the record; CheckHitWorld <= 1 counts as clear | game-ai |
| AIGroundVehicle::GetCollNav | 0x0002b210 | compares the lane unsigned with a signed one, so a negative lane places the navigator again on every call | game-ai |
| AIGroundVehicle::DriveToPoint | 0x0002f610 | the reverse flag only changes from 0 or 1 | game-ai |
| AIGroundVehicle::DriveToPointSub | 0x0002e0a0 | reversing zeroes steering and pitch; the pitch-limit test uses the replaced speed | game-ai |
| AIGroundVehicle::CheckAgentRoadNetworkCollision | 0x0002fde0 | the lane span is abs(first + span), capped at 64 | game-ai |
| AIGroundVehicle::CheckTrafficCollision | 0x00030b80 | stop points accumulate; compares ownerIndex with rigidBodySlot | game-ai |
| AIGroundVehicle::FireRockets, FireMissiles | 0x0002eb40, 0x0002ee00 | the side toggles as (side + 1) & 1 | game-ai |
| AIVehicleController::ApproveAdditionalAgentGroundVehicleSpawn, ApproveAdditionalHelicopterSpawn | 0x00036140, 0x00036180 | with no free slot, answer the count cast to a pointer | game-ai |
| AIVehicleController::RegisterPlayerGroundVehicle, RegisterTrafficVehicle | 0x000361c0, 0x000362e0 | with no free slot, the car is its own AI vehicle | game-ai |
| AIVehicleController::UpdateAgentGroundVehicles | 0x00035f10 | divides by zero with no agent cars | game-ai |
| AIVehicleController::IntersectionGreenLight | 0x00036060 | fabs applies to the comparison (dy < 5), not to the difference | game-ai |
| AIHelicopter::ControlMoveState | 0x00032af0 | direct mode's clamps give a speed factor of 50 outside the range | game-ai |
| AIHelicopter::FireBullets | 0x000324d0 | flattens and normalises the caller's heading in place | game-ai |
| AIHelicopter::Reset | 0x00032280 | clears index (only the controller's Init sets it) | game-ai |
| AIHelicopter::NewState | 0x00032700 | a shot count of 0 in guns mode becomes 255 | game-ai |
| AIRoadSpawn::GetTrafficSpawnPointOnSegment | 0x00034860 | a byte-wide lane-reversed test | game-ai |
| AICharacterPedestrian::FindClosestCar | 0x00024ee0 | compares the squared distance with the range (1000) | game-ai |
| AICharacterPedestrian::FindPointAgainstWall | 0x00024f80 | skips nearly level barriers; zeroes barrierMask only when it makes the collider again, then sets it to 0x10 | game-ai |
| AICharacterPedestrian::Recycle | 0x000254c0 | never resets an ordinary pedestrian's specialType | game-ai |
| AICharacterPedestrian::HandleInterrupts | 0x00027800 | the threat-3 test compares the old state with 3 | game-ai |
| AICharacterEnemyGround::HandleInterrupts | 0x0001f3c0 | the probe scales the unit step by its own length (1 or 0); a hit moves back and sets previousPosition = 2 x previous - position; newDamage is cleared at the end | game-ai |
| AICharacterEnemyDriver::DoNeutral | 0x0001d7f0 | the idle counter is compared unsigned | game-ai |
| AICharacterEnemySunroof::HandleInterrupts | 0x000217e0 | actor->visible tested against 1 | game-ai |
| AICharacterEnemySnow::DoDying | 0x000203c0 | passes the thrown matrix and velocity as GetRotPos's outputs | game-ai |
| AICharacterEnemySunroof::PlayZoneInjuryAnim | 0x00021b70 | an empty animation list gives animation 0 | game-ai |
| AIElementController::Construct | 0x00028190 | one `found` flag serves three lookups; on snow2a_mis4 at difficulty 2, "paris_hench" cars become "junglesnow" with no riders | game-ai |
| AIElementController::WakeUp | 0x00028bb0 | SetHitPointLoc inlined; the range squared x30 for ground characters, x9 for riders; a no-enemy character kind still gets a body if its pointer is set | game-ai |
| AIElementController::Update | 0x00029880 | slots handed out from the totals when there is not room; helicopters do not take their riders' slots; event 1 cleared within 120, set otherwise | game-ai |
| AICharacterHands::ChangeWeapon | 0x00022cc0 | the unarm branch switches the weapon at once | game-ai |
| AICharacterHands::DoArmed | 0x00022fb0 | AimRelease runs even while firing | game-ai |
| AICharacterBond::Init | 0x0001cd80 | keeps the characters only through the actor database | game-ai |
| AICharacterBond::HandleInterrupts | 0x0001d2a0 | select is ignored while arming; only arming clears BondSelectPressed | game-ai |
| EAIElementSetNonScoreable::Process, EKillAIElement::Process | 0x0003e0a0, 0x00040930 | test an element's helicopter flag the wrong way round: a helicopter is called as a car (vtable slot 44) and a car written as a helicopter (+0x138) | game-events, game-ai |
| GHud::TriggerMissionMsg | 0x000da030 | sets lastStep (+0x360), which shortens the next frame's time in Render | game-frontend |
| GHud::PauseMenuMsgHandler | 0x000dcc10 | the objectives page wraps asymmetrically; the failed page's Accept makes two CHAIN_RESTART lookups and drops them | game-frontend |
| GHud::GetButton | 0x000da1d0 | the text before the marker is copied unterminated | game-frontend |
| GHud::DrawStylin | 0x000da540 | fades by whole steps | game-frontend |
| GHud::DrawWeapon | 0x000dc990 | draws the ammunition with no font state set | game-frontend |
| GHud::DrawScreenFades | 0x000d8970 | sets the fade to 0, not 1, when it passes 1 | game-frontend |
| GHud::TriggerDamageFlash | 0x000d87a0 | an unsigned compare | game-frontend |
| GHud::DrawButtonMessage | 0x000def90 | with no marker draws the puppet's own text; the fade resets to 0.5, so it pulses | game-frontend |
| GHud::DrawPauseMenu | 0x000dd340 | SPRITE_SetState before a font draw; the score page divides unsigned; the objectives and select hints share the select picture | game-frontend |
| GHud::DrawMissionResult | 0x000df2e0 | state 2 draws at the interpolated key with key 0's quad; state 5 checks !paused again | game-frontend |
| GHud::DrawCrossHairs | 0x000e0440 | the jungle gun's path ignores GetSelectedTarget; the recharge goes through the sound's vtable slot 9 (only AFingoDeath has it; it is reached only with the FOD weapons) | game-frontend |
| GHud::Render | 0x000e0140 | actions 33-35 are not passed to the pause menu while paused | game-frontend |
| GHud::UpdateTargets | 0x000e1a90 | copies the article's DESCRIPTION float as it is | game-frontend |
| GGallery::FONT_Draw (three), GLoadingScreen::LoadAndDrawUnLoadingScreen, Update, GSubtitles::DrawFont | 0x000d6200, 0x000d63a0, 0x000d6520, 0x000e2110, 0x000e28a0, 0x000e3670 | the text is passed to FONT_drawtextfa as its format string | game-frontend |
| GLoadingScreen::Status | 0x000e2ff0 | the line is passed to Print as a format; the clock's third field is (tick % 60) / 6 | game-frontend |
| GGirl::UpdateTexture | 0x000d6f60 | scales real clock ticks (TIMER_gettick) by the simulation's step | game-frontend |
| GFXGallery::PARTICLE_Draw | 0x000d4f90 | copying the particle type takes the last library index into byte 1 of unknown2C when byte 2 is above 0 | game-frontend |
| GFXGallery::CANVAS_Draw | 0x000d51e0 | glare parts clear "stopped" even when culled; sound parts set it | game-frontend |
| GFX::Trigger | 0x000d32f0 | with the effect's Flgs bit 0x01 the effect is added but the answer is 0; the incidental flag (flag8000) is incidental && bit 0x01 | game-frontend |
| CANVAS_GetPosQuat | 0x000d3730 | its asserts test 16-byte alignment ("GFXGallery.cpp" lines 44-46) | game-frontend |
| AVoice::IsFinished | 0x000d36c0 | tests the shared view's finished flag first | game-frontend |
| GOrthoHudView::PostRender | 0x000daac0 | sends EExit and ERestart every frame while the HUD's flags are set | game-frontend |
| GOrthoHudView::DoRender | 0x000e1ce0 | in wide PAL (video mode 2) y is neither scaled by 0.75 nor offset; the margin is 0x3d4cccd0, not 0.05f | game-frontend |
| GLocale::GetSubDirectory | 0x000e3140 | NULL for video modes other than 1-3 | game-frontend |
| GSubtitleSplitter::WordWrap | 0x000e31a0 | divides by the signed total length; past ten lines the rest is dropped (the last cut at the width); the times always go to this->times | game-frontend |
| GSubtitleSplitter::GetString | 0x000e3430 | stops at line 9 but times it again; past the end answers NULL | game-frontend |
| GSubtitles::SetupFMVSubtitles | 0x000e3560 | restarts the subtitle on every call, so one lasts until the next cue; 1/25 s a frame in PAL 50, 1/30 otherwise | game-frontend |
| GSubtitles::LoadSubtitles | 0x000e3840 | assumes CR LF; the cursor counts from the file's start; the frame is read from the seventh character of the first '=' field; frames x5/6 in PAL 50 | game-frontend |
| GHud::DrawCrossHairs | 0x000e0440 | the remote rocket's recharge bar compares its segment count unsigned (0x000e08c7, 0x000e0917): a fire step after the current one, which play never makes, gives a negative charge and about four billion segments | game-frontend |
| malloc | 0x001340e3 | rounds every request up to 16 bytes (`__active_heap` is 0, which its "not the system heap" test takes); calloc and realloc, whose tests are "the small-block heap", do not | c-runtime |
| `__ld12cvt` | 0x0013949a | rounds a 12-byte long double to a double or float with a tie going down, after a parse (`__strgtold12`) that is not correctly rounded either: atof, strtod, sscanf and `_Stod` can be an ulp from a modern runtime | c-runtime |
| `_output` (sprintf, vsprintf, `_vsnprintf`) | 0x00132767, 0x00133aee, 0x001340f5 | floats through 17 rounded digits (`$I10_OUTPUT`) rounded again by the layout; `%e` prints three exponent digits, `%p` eight upper-case hex digits; 'L' is accepted and ignored; a precision over 163 takes a heap buffer; `_vsnprintf` writes no null when the text fills the count exactly | c-runtime |
| atol, strtol | 0x00133d51, 0x00133636 | atol wraps on overflow; strtol leaves the end at the text when no digit came | c-runtime |
| qsort | 0x00132db0 | not stable: equal elements land where its quicksort (median of three, partitions on a stack, runs of eight or fewer to a selection sort) leaves them | c-runtime |

### 3.8 x87 and float/double mixes kept

| Function | Address | Quirk | Doc |
|---|---|---|---|
| AVehicle::CalculateRpm | 0x0011ec80 | the log goes through FLDLN2/FYL2X (naked helper VehicleLogX87) | game-audio |
| ASubmersible::PlayCavitation | 0x0012bf00 | the pitch offset is the float 0.2 while playing, the double 0.2 while fading | game-audio |
| AUltraLite::PlayMotor | 0x00129cd0 | a NaN climb fails every branch and keeps the old pitch target | game-audio |
| RVehicleParticle::UpdateCar | 0x000a7ac0 | the spray and road-dirt clamps compare the double and keep the float | game-effects |
| RScorchWorld::ScorchWorld | 0x000a50c0 | the level test compares with a double holding 0.85f | game-effects |
| Sentry::ApplyDamage, Smackable::ApplyDamage, Sentry::DrawMuzzleFlashes, Sentry::Construct | 0x00073f80, 0x00075560, 0x00073500, 0x00073a90 | unrounded x87 tests: the first impact-level test, the flash fade test, the -1 clamp | game-weapons |
| Sentry::ComputeNewRotation | 0x00073600 | `float(1 - limit)` on one branch only | game-weapons |
| AIHelicopter::ManeuverToPos | 0x00031cd0 | the height and climb compared unrounded, then the stored float | game-ai |
| AICharacterEnemyGround::ThreatenedByVehicle; AICharacterEnemySSnow::DoAiming, AICharacterEnemyWindow::DoAiming, AICharacterEnemySnow::DoAiming | 0x0001e440; 0x00020a60, 0x00022150, 0x00020090 | values compared unrounded on the x87, then stored | game-ai |
| `_CIacos`, `_CIasin`, `_CIpow` | 0x001328d0, 0x00133f94, 0x00132a94 | answer on the x87 unrounded, through FPATAN, FYL2X and F2XM1 as the runtime has them | c-runtime |
| `_Stod` | 0x00131c45 | keeps its value at the x87's exponent range through the scaling loop and rounds it once, at the caller's store (here a power of two keeps it in range) | c-runtime |
