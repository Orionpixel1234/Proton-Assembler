#ifndef PROTON_H
#define PROTON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

/* CLI globals — shared between proton.c functions */
const char *allowedOptions[] = {"-h", "--help", "-o", "-f", "--file"};
const char *outputFileFormat = NULL;
const char *inputFileName    = NULL;
const char *outputFileName   = NULL;
char       *inputBuffer      = NULL;
int         outputFileDefined = 0;

int inputBufferFull(char *buffer, size_t bufferSize);
int handle(void);
int handleOption(const char *option, const char *value);
int checkArgs(int argc, char *argv[]);
int main(int argc, char *argv[]);

#endif /* PROTON_H */
