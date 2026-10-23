#ifndef PSIFILE_H_
#define PSIFILE_H_

int psiFileOpen(char* filename);
int ** psiFileLoad(char *filename, unsigned short allocType, int *sizeOut);
void psiFileLoadForParse(char *name);
int psiFileClose(void);
void *psiFileRead(int length);        // the current position in the open file, then advances it

char psiFileSetSingleFileMode(char mode);

#endif // PSIFILE_H_