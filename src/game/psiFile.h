#ifndef PSIFILE_H_
#define PSIFILE_H_

int __cdecl psiFileOpen(int param_1);
int ** __cdecl psiFileLoad(char *filename, unsigned short allocType, int *sizeOut);

#endif // PSIFILE_H_