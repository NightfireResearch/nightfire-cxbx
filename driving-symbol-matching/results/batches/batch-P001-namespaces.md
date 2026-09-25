# Batch P001: namespace moves

In Ghidra: Edit Function (F) on each address, and set the name to the full text in the last column.

| done | address | now | set name to |
|---|---|---|---|
| yes | 0x001184b8 | ActWeapon::operator_new | ActWeapon::operator_new |
| yes | 0x00119560 | AICharacter::operator_new | AICharacter::operator_new |
| yes | 0x00119580 | AICharacter::operator_delete | AICharacter::operator_delete |
| yes | 0x0017e4f0 | Explosion::operator_new | Explosion::operator_new |
| yes | 0x002e9688 | AStream::Get | AStream::Get |
|  | 0x00165bc8 | ITimer::Update | IFeedback::Update |
|  | 0x00208a00 | GetObjectiveByID | SMissionManager::GetObjectiveByID |
|  | 0x00228b70 | ~WSoundGroup | WSoundGroup::~WSoundGroup |
