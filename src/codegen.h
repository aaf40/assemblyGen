#ifndef CODEGEN_H
#define CODEGEN_H

#include "tree.h"
#include <stdarg.h>



// Label generation
char* generateLabel(const char* prefix);

// Instruction emission
void emitInstruction(const char* format, ...);

// Generate a unique label for a loop
char* generateLoopLabel(void);

// Register management functions
void initRegisters(void);
int getCurrentRegister(void);
void setCurrentRegister(int regNum);

// Code generation functions
int generateCode(tree* node);
int output(tree* node);

// Current tree node
extern tree* current_tree;

#endif
