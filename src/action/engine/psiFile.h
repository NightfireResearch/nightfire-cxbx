#ifndef PSIFILE_H_
#define PSIFILE_H_

int psiFileOpen(char* param_1);
int ** psiFileLoad(char *filename, unsigned short allocType, int *sizeOut);
void psiFileLoadForParse(char *param_1);
int psiFileClose(void);
int maybePsiFileRead(int length);   // the current position in the open file, then advances it

char psiFileSetSingleFileMode(char mode);

#endif // PSIFILE_H_