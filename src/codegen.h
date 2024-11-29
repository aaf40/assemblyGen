#ifndef CODEGEN_H
#define CODEGEN_H

#include "tree.h"
#include <stdarg.h>

// Register management constants
#define NUM_REGISTERS 10
#define NO_REGISTER -1
#define ERROR_REGISTER -2

// Label generation
char* generateLabel(const char* prefix);

// Instruction emission
void emitInstruction(const char* format, ...);

// Memory management helpers
int hasSeen(const char* name);
int base(tree* node);
int offset(tree* node);

// Type checking
int isCompatibleType(dataType type1, dataType type2);
const char* typeToString(dataType type);

// Error reporting
void reportError(const char* format, ...);

// Register management functions
void initRegisters(void);
int nextRegister(void);
void freeRegister(int regNum);
int getCurrentRegister(void);
void setCurrentRegister(int regNum);

// Code generation functions
int generateCode(tree* node);
int output(tree* node);

#endif
