#ifndef LOADER_H
#define LOADER_H

bool LoaderProcess(void);
void parsemap_block_entity_params(void);
bool isLoadable(HASHCODE param_1);
bool LoadableReload(HASHCODE hashcode);

#endif // LOADER_H