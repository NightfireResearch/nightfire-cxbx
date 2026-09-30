#ifndef LOADER_H
#define LOADER_H

bool LoaderProcess(void);
bool LoaderLoad(int mode, uint fileHash, undefined4 unused, int variant);
void parsemap_block_entity_params(void);
bool isLoadable(HASHCODE param_1);
bool LoadableReload(HASHCODE hashcode);

extern uint32_t MemType; // defined in Loader.cpp
extern uint *dirFileBuf; // defined in Loader.cpp: the data of the file being loaded (psiFileLoad returns it)
extern uint DirFileLen;  // defined in Loader.cpp: its size

#endif // LOADER_H