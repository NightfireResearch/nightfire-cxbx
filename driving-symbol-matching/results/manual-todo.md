# Functions to create by hand in DRIVING.ELF

None outstanding (the seven listed on 25 Sept 2026 were freed by hand and named in batch P018).

If create_function is refused where the code is clear: the function's first word is often 00 00 xx xx, and Ghidra
takes it for padding at the end of the previous function; shrink that function and it will go through.
