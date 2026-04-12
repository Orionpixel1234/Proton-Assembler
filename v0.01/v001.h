#ifndef v001_H
#define v001_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

const char *allowedOptions[] = {"-h", "--help", "-o", "-f", "--file"};
const char *currentOption = NULL;
const char *outputFileFormat = NULL;
const char *inputFileName = NULL;
const char *outputFileName = NULL;
char *inputBuffer = NULL;
int outputFileDefined = 0;

int inputBufferFull(char *buffer, size_t bufferSize);
int handle();
int handleOption(const char *option, const char *value);
int checkArgs(int argc, char *argv[]);
int main(int argc, char *argv[]);
#endif