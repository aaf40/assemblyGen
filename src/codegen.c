#include "codegen.h"
#include "tree.h"
#include "strtab.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

// Global variables for register management
static int registers[NUM_REGISTERS];  // 0: free, 1: in use
static int currentRegister = NO_REGISTER;

void initRegisters(void) {
    for (int i = 0; i < NUM_REGISTERS; i++) {
        registers[i] = 0;  // Mark all registers as free
    }
    currentRegister = NO_REGISTER;
}

int nextRegister(void) {
    for (int i = 0; i < NUM_REGISTERS; i++) {
        if (registers[i] == 0) {
            registers[i] = 1;  // Mark as in use
            currentRegister = i;
            return i;
        }
    }
    // Handle register spilling if necessary
    fprintf(stderr, "Error: Out of registers\n");
    exit(1);
}

void freeRegister(int regNum) {
    if (regNum >= 0 && regNum < NUM_REGISTERS) {
        registers[regNum] = 0;  // Mark as free
        if (currentRegister == regNum) {
            currentRegister = NO_REGISTER;
        }
    } else {
        fprintf(stderr, "Error: Invalid register number %d\n", regNum);
    }
}

int getCurrentRegister(void) {
    return currentRegister;
}

void setCurrentRegister(int regNum) {
    if (regNum >= -1 && regNum < NUM_REGISTERS) {  // -1 is valid for NO_REGISTER
        currentRegister = regNum;
    } else {
        fprintf(stderr, "Error: Invalid register number %d\n", regNum);
    }
}

int generateCode(tree* node) {
    if (!node) return NO_REGISTER;
    
    int result, t1, t2;
    
    switch (node->nodeKind) {
        // Group all arithmetic operations
        case ADDOP:
        case MULOP:
            t1 = generateCode(node->children[0]);  // left child
            t2 = generateCode(node->children[1]);  // right child
            result = nextRegister();
            
            // Addition and Multiplication use addi
            if (node->nodeKind == ADDOP || node->nodeKind == MULOP) {
                emitInstruction("    addi $t%d, $t%d, $t%d", result, t1, t2);
            }
            // Subtraction and Division use subi
            else {
                emitInstruction("    subi $t%d, $t%d, $t%d", result, t1, t2);
            }
            
            freeRegister(t1);
            freeRegister(t2);
            break;
            
        case IDENTIFIER:
            if ((result = hasSeen(node->name))) {
                // If value is already in a register, use that
                break;
            } else {
                t1 = base(node);    // Get base address
                t2 = offset(node);   // Get offset
                result = nextRegister();
                emitInstruction("    lw $t%d, %d($t%d)", result, t2, t1);
            }
            break;
            
        case INTEGER:
            result = nextRegister();
            emitInstruction("    li $t%d, %d", result, node->val);
            break;
            
        case RELOP:
            t1 = generateCode(node->children[0]);
            t2 = generateCode(node->children[1]);
            result = nextRegister();
            
            switch(node->val) {  // Check the val field instead of nodeKind
                case 1:  // OPER_LT (val = 1 from parser.y)
                    emitInstruction("    slt $t%d, $t%d, $t%d", result, t1, t2);
                    break;
                case 2:  // OPER_GT (val = 2 from parser.y)
                    emitInstruction("    slt $t%d, $t%d, $t%d", result, t2, t1);
                    break;
                case 4:  // OPER_EQ (val = 4 from parser.y)
                    emitInstruction("    seq $t%d, $t%d, $t%d", result, t1, t2);
                    break;
            }
            freeRegister(t1);
            freeRegister(t2);
            break;
            
        case ASSIGNSTMT:  // Assignment statement (=)
            t1 = generateCode(node->children[0]);  // left side (variable)
            t2 = generateCode(node->children[1]);  // right side (expression)
            emitInstruction("    sw $t%d, ($t%d)", t2, t1);
            freeRegister(t1);
            freeRegister(t2);
            break;
            
        case LOOPSTMT:  // While loop
            {
                char *startLabel = generateLabel("while_start");
                char *endLabel = generateLabel("while_end");
                
                emitInstruction("%s:", startLabel);
                t1 = generateCode(node->children[0]);  // condition
                emitInstruction("    beq $t%d, $zero, %s", t1, endLabel);
                freeRegister(t1);
                
                generateCode(node->children[1]);  // body
                emitInstruction("    j %s", startLabel);
                emitInstruction("%s:", endLabel);
            }
            break;
            
        case CONDSTMT:  // If statement
            {
                char *elseLabel = generateLabel("else");
                char *endLabel = generateLabel("endif");
                
                t1 = generateCode(node->children[0]);  // condition
                emitInstruction("    beq $t%d, $zero, %s", t1, elseLabel);
                freeRegister(t1);
                
                generateCode(node->children[1]);  // then part
                emitInstruction("    j %s", endLabel);
                
                emitInstruction("%s:", elseLabel);
                if (node->children[2]) {  // else part (if it exists)
                    generateCode(node->children[2]);
                }
                emitInstruction("%s:", endLabel);
            }
            break;
            
        case FUNCCALLEXPR:  // Function calls
            {
                // Save registers before call
                emitInstruction("    # Save registers");
                // Generate code for arguments
                for (int i = 0; i < node->numChildren; i++) {
                    t1 = generateCode(node->children[i]);
                    emitInstruction("    move $a%d, $t%d", i, t1);
                    freeRegister(t1);
                }
                emitInstruction("    jal %s", node->name);
                result = nextRegister();
                emitInstruction("    move $t%d, $v0", result);  // Get return value
            }
            break;
    }
    return result;
}
