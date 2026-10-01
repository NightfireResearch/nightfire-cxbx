# Batch P012: not applied

| row | sheet name | PS2 | why |
|---|---|---|---|
| 1048 | DebugVariable<unsigned int>::DebugVariable(LString, unsigned in | 00142d90 | defer: (downgraded on review: weak (agent's own flag)) Stores LString copy, vtable and five args into a DebugVariable-shaped object; called from DebugIndexer and immediately precedes TypeTraits<unsigned int>. |
| 2146 | IOModule::ControllerRemovedContinuePlaying(void) | 001685d0 | defer: (downgraded on review: weak (agent's own flag)) Zeroes all device scalar values then calls SendGameMessages with a forcing flag set, between ReleaseAllButtons and SendGameMessages. |
| 2339 | basic_string<char, string_char_traits<char>, chunky_alloc<false | 00177260 | accepted, but name cut short by the sheet's 63 characters |
| 2435 | Float_InvertMatrix4(MATRIX4 *) | 0017bae0 | defer: (downgraded on review: weak (agent's own flag)) Writes the transpose of the input MATRIX4 into the returned MATRIX4 (inverse of an orthonormal matrix), directly after Float_Matrix4toMatrix3. |
| 2518 | Missile::Debug(void) | 001838b8 | defer: (downgraded on review: empty stub, order only) Empty 8-byte stub between Missile::SetTarget and the type_info function; debug functions are stubs in retail. |
| 2751 | PhysicsObject::NotTracer(int) | 00196040 | reject: Returns Sim.GetRigidBody(this->slot) and ignores the int argument; it is one of the GetSimpleRigidBody/GetRigidBody accessors (see extras), not NotTracer. |
| 2833 | Shell::GetWeaponType(void) | 0019e0d8 | reject: Body is an atexit destructor for a static AttributeSet (~AttributeSet(0x3d0d58,2)) passed from Smackable::Smackable, not a weapon-type getter. |
| 3051 | RDrawGroup::AddToEffectsList(RRenderRequest &) | 001ab288 | reject: Candidate is an SGI _Rb_tree _M_erase (recurse right, walk left, FastFree 0x1c), not a list add. |
| 3125 | DrawTest(RSceneObj &, int) | 001ae138 | defer: (downgraded on review: weak (agent's own flag)) Unreferenced debug draw using both args (RSceneObj physics body position, int index into world data) to build and draw a textured quad, before gDrawGlares' ctor. |
| 3220 | _List_base<RPathEngine::RPathHandle, allocator<RPathEngine::RPa | 001b22c0 | accepted, but name cut short by the sheet's 63 characters |
| 3352 | RPlayerCamState::IsDriveMissileOn(void) | 001be8b8 | reject: Candidate is a static-initialiser stub (helper(1,0xffff), ctors table), not IsDriveMissileOn; row already sits at 0x1be6f8. |
| 3663 | RTextureContext::GetShapeFile(void) const | 001cfd18 | reject: Candidate is the USymbolTable::Namespace type_info function; next function 0x1cfd58 is an atexit dtor, no GetShapeFile body found. |
| 3966 | _Rb_tree<unsigned int, pair<unsigned int, USimpleVec<REmpBolts: | 001de470 | accepted, but the _Rb_tree shorthand or the method is unknown |
| 4143 | _Rb_tree<unsigned int, pair<unsigned int, MungedGenericParticle | 001e8d80 | accepted, but the _Rb_tree shorthand or the method is unknown |
| 4239 | global constructors keyed to RSky::Draw(CARP::Instance *, bool) | 001ece18 | reject: Candidate is RSky::Draw itself (row 4238); following functions are lexicographical_compare copies, no ctor stub found nearby. |
| 4304 | RGlareManager::DrawGlaresReflected(void) | 001f2508 | defer: (downgraded on review: empty stub, order only) corrected from 0x1f26a8 (a static-init/destroy helper for glare textures); 0x1f2508 is the 8-byte stub directly after DrawGlares, others in the gap are lexicographical_compare/_M_erase/static init. |
| 4557 | SimTrackedInstance ** remove<SimTrackedInstance **, SimTrackedI | 002049b8 | accepted, but name cut short by the sheet's 63 characters |
| 4628 | SMissionManager::GetEnemyHitPointScale(void) | 00208468 | reject: 0x208468 is GetEnemyGlueFactor (float, DoAttackMode callers as in AUF); no HitPointScale-shaped function remains before ShowMissionText. |
| 4927 | void __unguarded_insertion_sort_aux<InstanceAndDistance *, Inst | 00220aa0 | accepted, but name cut short by the sheet's 63 characters |
| 5074 | WTargetPicker::DrawAutoDriveTargeting(void) | 0022be68 | defer: (downgraded on review: empty stub, order only) corrected from 0x22be70 (lexicographical_compare); 0x22be58/60/68 are three 8-byte empty stubs matching DebugDrawCursors, DebugDrawTargets, DrawAutoDriveTargeting in sheet order (retail body stubbed). |
| 5133 | WWorldMath::CalcBoundingBox(COORD4 *, unsigned int, COORD3 &, C | 002313c0 | reject: Candidate is the static init helper (called from ctor stub 0x2313f8); CalcBoundingBox is likely the unfunctioned code at 0x231418 (see extras). |
| 5262 | GGallery::LINE_Draw2D(float, float, GGallery::Lines *) | 00238028 | reject: Candidate sets verts (+0x18) and colours (+0x20) then Draw(count) - that is LINE_Draw2DStrip(COORD4*,Colour*,int), row 5264 (see extras). |
