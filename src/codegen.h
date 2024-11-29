#ifndef CODEGEN_H
#define CODEGEN_H

#include "tree.h"

// Register management constants
#define NUM_REGISTERS 10  // Using $t0-$t9 for simplicity
#define NO_REGISTER -1

// Register management functions
void initRegisters(void);
int nextRegister(void);
void freeRegister(int regNum);
int getCurrentRegister(void);
void setCurrentRegister(int regNum);

// Code generation function
int generateCode(tree* node);

// Instruction emission (we'll implement this next)
void emitInstruction(const char* format, ...);

#endif
