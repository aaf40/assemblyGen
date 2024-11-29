#include "codegen.h"
#include "tree.h"
#include "strtab.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>  // for strcmp

// Global variables for register management
static int registers[NUM_REGISTERS];  // 0: free, 1: in use
static int currentRegister = NO_REGISTER;
// Forward declarations
static int generateArithmeticOp(tree* node);
static int generateIdentifier(tree* node);
static int generateInteger(tree* node);
static int generateRelationalOp(tree* node);
static int generateAssignment(tree* node);
static int generateWhileLoop(tree* node);
static int generateIfStatement(tree* node);
static int generateFunctionCall(tree* node);
static int traverseAST(tree* node);
static void generateHeader(FILE* fp);
char* generateLabel(const char* prefix);
static void generateFunctionPrologue(const char* functionName, int numLocalVars);
static void generateFunctionEpilogue(int numLocalVars);
static void generateMainSetup(void);
static void generateOutputFunction(void);

// Label counter for unique labels
static int labelCounter = 0;

extern char *nodeNames[];  // Defined in tree.c

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
    
    // Enhanced debug output
    printf("DEBUG: Processing node type %d (%s)\n", node->nodeKind, nodeNames[node->nodeKind]);
    if (node->name) {
        printf("DEBUG: Node name: %s\n", node->name);
    }
    printf("DEBUG: Number of children: %d\n", node->numChildren);
    
    int result = NO_REGISTER;
    
    // Process this node
    switch(node->nodeKind) {
        case PROGRAM:
            printf("DEBUG: Handling PROGRAM node\n");
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            break;
            
        case DECLLIST:
            printf("DEBUG: Handling DECLLIST node with %d children\n", node->numChildren);
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            break;
            
        case DECL:
            printf("DEBUG: Handling DECL node\n");
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            break;
            
        case VARDECL:
            printf("DEBUG: Handling VARDECL node\n");
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            break;
            
        case TYPESPEC:
            printf("DEBUG: Handling TYPESPECIFIER node\n");
            // No code generation needed for type specifier
            break;
            
        case STATEMENTLIST:
            printf("DEBUG: Handling STATEMENTLIST node\n");
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            break;
            
        case ADDOP:
        case MULOP:
            printf("DEBUG: Handling arithmetic operation\n");
            result = generateArithmeticOp(node);
            break;
            
        case RELOP:
            printf("DEBUG: Handling relational operation\n");
            result = generateRelationalOp(node);
            break;
            
        case INTEGER:
            printf("DEBUG: Handling integer node with value: %d\n", node->val);
            result = generateInteger(node);
            break;
            
        case IDENTIFIER:
            printf("DEBUG: Handling identifier\n");
            result = generateIdentifier(node);
            break;
            
        case ASSIGNSTMT:
            printf("DEBUG: Handling assignment statement\n");
            result = generateAssignment(node);
            break;
            
        case LOOPSTMT:
            printf("DEBUG: Handling while loop\n");
            result = generateWhileLoop(node);
            break;
            
        case CONDSTMT:
            printf("DEBUG: Handling if statement\n");
            result = generateIfStatement(node);
            break;
            
        case FUNCCALLEXPR:
            printf("DEBUG: Handling function call\n");
            result = generateFunctionCall(node);
            break;
            
        default:
            printf("DEBUG: Unhandled node type %d (%s)\n", 
                   node->nodeKind, 
                   nodeNames[node->nodeKind]);
            // Continue processing children even for unhandled nodes
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            break;
    }
    
    return result;
}

static int generateArithmeticOp(tree* node) {
    int t1 = generateCode(node->children[0]);
    int t2 = generateCode(node->children[1]);
    int result = nextRegister();
    
    if (node->nodeKind == ADDOP || node->nodeKind == MULOP) {
        emitInstruction("    addi $t%d, $t%d, $t%d", result, t1, t2);
    } else {
        emitInstruction("    subi $t%d, $t%d, $t%d", result, t1, t2);
    }
    
    freeRegister(t1);
    freeRegister(t2);
    return result;
}

static int generateIdentifier(tree* node) {
    int result;
    if ((result = hasSeen(node->name))) {
        return result;
    }
    
    int t1 = base(node);
    int t2 = offset(node);
    result = nextRegister();
    emitInstruction("    lw $t%d, %d($t%d)", result, t2, t1);
    return result;
}

static int generateInteger(tree* node) {
    int result = nextRegister();
    emitInstruction("    li $t%d, %d", result, node->val);
    return result;
}

static int generateRelationalOp(tree* node) {
    int t1 = generateCode(node->children[0]);
    int t2 = generateCode(node->children[1]);
    int result = nextRegister();
    
    // Generate appropriate comparison based on the operator
    switch(node->val) {
        case 1:  // OPER_LT (from parser.y line 517-521)
            emitInstruction("\tslt $t%d, $t%d, $t%d", result, t1, t2);
            break;
        case 2:  // OPER_GT (from parser.y line 522-526)
            emitInstruction("\tsgt $t%d, $t%d, $t%d", result, t1, t2);
            break;
        case 4:  // OPER_EQ (from parser.y line 532-536)
            emitInstruction("\tseq $t%d, $t%d, $t%d", result, t1, t2);
            break;
    }
    
    freeRegister(t1);
    freeRegister(t2);
    return result;
}

static int generateAssignment(tree* node) {
    int t1 = generateCode(node->children[0]);
    int t2 = generateCode(node->children[1]);
    emitInstruction("    sw $t%d, ($t%d)", t2, t1);
    freeRegister(t1);
    freeRegister(t2);
    return NO_REGISTER;
}

static int generateWhileLoop(tree* node) {
    char* startLabel = generateLabel("while_start");
    char* endLabel = generateLabel("while_end");
    
    emitInstruction("%s:", startLabel);
    
    // Generate condition code
    int condReg = generateCode(node->children[0]);
    emitInstruction("\tbeq $t%d, $zero, %s", condReg, endLabel);
    freeRegister(condReg);
    
    // Generate loop body
    generateCode(node->children[1]);
    
    // Jump back to start
    emitInstruction("\tj %s", startLabel);
    emitInstruction("%s:", endLabel);
    
    free(startLabel);
    free(endLabel);
    return NO_REGISTER;
}

static int generateIfStatement(tree* node) {
    char* elseLabel = generateLabel("else");
    char* endLabel = generateLabel("endif");
    
    // Generate condition code
    int condReg = generateCode(node->children[0]);
    emitInstruction("\tbeq $t%d, $zero, %s", condReg, elseLabel);
    freeRegister(condReg);
    
    // Generate 'then' part
    generateCode(node->children[1]);
    emitInstruction("\tj %s", endLabel);
    
    // Generate 'else' part if it exists
    emitInstruction("%s:", elseLabel);
    if (node->numChildren > 2) {
        generateCode(node->children[2]);
    }
    
    emitInstruction("%s:", endLabel);
    
    free(elseLabel);
    free(endLabel);
    return NO_REGISTER;
}

// Function to generate MIPS code for output
int output(tree* node) {
    // Generate code for the argument (the number to print)
    int t1 = generateCode(node->children[1]);
    
    // Generate MIPS code to print the number
    emitInstruction("    move $a0, $t%d", t1);  // Put number in $a0
    emitInstruction("    li $v0, 1");           // 1 = print integer syscall
    emitInstruction("    syscall");             // Do the print
    
    freeRegister(t1);
    return NO_REGISTER;
}

static int generateFunctionCall(tree* node) {
    // Save registers that will be used
    for (int i = 0; i < node->numChildren; i++) {
        int argReg = generateCode(node->children[i]);
        emitInstruction("\tmove $a%d, $t%d", i, argReg);
        freeRegister(argReg);
    }
    
    // Make the call
    emitInstruction("\tjal %s", node->name);
    
    // Get return value
    int result = nextRegister();
    emitInstruction("\tmove $t%d, $v0", result);
    
    return result;
}

char* generateLabel(const char* prefix) {
    char* label = malloc(50);  // Adjust size as needed
    sprintf(label, "%s_%d", prefix, labelCounter++);
    return label;
}

void emitInstruction(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

int hasSeen(const char* name) {
    // Cast away const when calling ST_lookup
    symEntry* entry = ST_lookup((char *)name);
    if (entry) {
    // Return a register number if the variable has been assigned one
    // For now, we'll return NO_REGISTER to indicate it exists but needs loading
        return NO_REGISTER;
    }
    return ERROR_REGISTER;
}

int base(tree* node) {
    if (!node || !node->name) {
        return ERROR_REGISTER;
    }
    
    symEntry* entry = ST_lookup(node->name);
    if (!entry) {
        return ERROR_REGISTER;
    }
    
    // For global variables
    if (entry->scope == GLOBAL_SCOPE) {
        return 0;  // Global variables are at base of data segment
    }
    
    // For local variables and parameters
    // This is a simplified implementation
    return entry->scope == LOCAL_SCOPE ? 1 : 0;
}

int offset(tree* node) {
    if (!node || !node->name) {
        return ERROR_REGISTER;
    }
    
    symEntry* entry = ST_lookup(node->name);
    if (!entry) {
        return ERROR_REGISTER;
    }
    
    // For arrays, calculate offset based on index
    if (entry->sym_type == ST_ARRAY && node->numChildren > 0) {
        // The index expression should be the first child
        int indexReg = generateCode(node->children[0]);
        if (indexReg >= 0) {
            // Return the register containing the calculated offset
            return indexReg;
        }
    }
    
    // For simple variables, return their position in the frame
    // This is a simplified implementation
    return 0;  // You'll need to implement proper stack frame offsets
}

int isCompatibleType(dataType type1, dataType type2) {
    // Direct type equality
    if (type1 == type2) return 1;
    
    // Special cases (if any)
    // For example, if you want to allow int to char conversion:
    if ((type1 == DT_INT && type2 == DT_CHAR) ||
        (type1 == DT_CHAR && type2 == DT_INT)) {
        return 1;
    }
    
    return 0;
}

const char* typeToString(dataType type) {
    switch(type) {
        case DT_INT: return "int";
        case DT_CHAR: return "char";
        case DT_VOID: return "void";
        case DT_ARRAY: return "array";
        case DT_FUNC: return "function";
        default: return "unknown";
    }
}

void reportError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "Error: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static int traverseAST(tree* node) {
    if (!node) return NO_REGISTER;
    
    // Enhanced debug output with node kind names
    const char* nodeKindStr;
    switch(node->nodeKind) {
        case PROGRAM: nodeKindStr = "PROGRAM"; break;
        case DECLLIST: nodeKindStr = "DECLLIST"; break;
        case DECL: nodeKindStr = "DECL"; break;
        case VARDECL: nodeKindStr = "VARDECL"; break;
        case FUNDECL: nodeKindStr = "FUNDECL"; break;
        case FUNBODY: nodeKindStr = "FUNBODY"; break;
        case STATEMENTLIST: nodeKindStr = "STATEMENTLIST"; break;
        case ADDOP: nodeKindStr = "ADDOP"; break;
        case MULOP: nodeKindStr = "MULOP"; break;
        case IDENTIFIER: nodeKindStr = "IDENTIFIER"; break;
        case INTEGER: nodeKindStr = "INTEGER"; break;
        case RELOP: nodeKindStr = "RELOP"; break;
        case ASSIGNSTMT: nodeKindStr = "ASSIGNSTMT"; break;
        case LOOPSTMT: nodeKindStr = "LOOPSTMT"; break;
        case CONDSTMT: nodeKindStr = "CONDSTMT"; break;
        case FUNCCALLEXPR: nodeKindStr = "FUNCCALLEXPR"; break;
        default: nodeKindStr = "UNKNOWN"; break;
    }
    
    printf("DEBUG: Traversing %s node (type %d)%s%s\n", 
           nodeKindStr,
           node->nodeKind,
           node->name ? ", name: " : "",
           node->name ? node->name : "");
    
    // Print number of children
    printf("DEBUG: Node has %d children\n", node->numChildren);
    
    // Recursively traverse all children
    for (int i = 0; i < node->numChildren; i++) {
        printf("DEBUG: Processing child %d of %s node\n", i + 1, nodeKindStr);
        traverseAST(node->children[i]);
    }
    
    return NO_REGISTER;
}

static void generateHeader(FILE* fp) {
    fprintf(fp, ".data\n");     // Data section for global variables
    fprintf(fp, ".text\n");     // Text section for code
    fprintf(fp, ".globl main\n");  // Declare main as global
    char* mainLabel = generateLabel("main");
    fprintf(fp, "%s:\n", mainLabel);
    free(mainLabel);  // Don't forget to free the allocated memory
}

static void generateMainSetup(void) {
    emitInstruction("\tjal startmain");
    emitInstruction("\tli $v0, 10");
    emitInstruction("\tsyscall");
}

static void generateFunctionPrologue(const char* functionName, int numLocalVars) {
    emitInstruction("# Function definition");
    emitInstruction("%s:", functionName);
    
    // Setting up FP
    emitInstruction("\tsw $fp, ($sp)");
    emitInstruction("\tmove $fp, $sp");
    emitInstruction("\tsubi $sp, $sp, 4");
    
    // Saving registers ($s0 to $s7)
    for (int i = 0; i <= 7; i++) {
        emitInstruction("\tsw $s%d, ($sp)", i);
        emitInstruction("\tsubi $sp, $sp, 4");
    }
    
    // Allocate space for local variables
    if (numLocalVars > 0) {
        emitInstruction("\t# Allocate space for %d local variables", numLocalVars);
        emitInstruction("\tsubi $sp, $sp, %d", numLocalVars * 4);
    }
}

static void generateFunctionEpilogue(int numLocalVars) {
    // Deallocate space for local variables
    if (numLocalVars > 0) {
        emitInstruction("\t# Deallocate space for %d local variables", numLocalVars);
        emitInstruction("\taddi $sp, $sp, %d", numLocalVars * 4);
    }
    
    // Restoring registers ($s7 to $s0)
    for (int i = 7; i >= 0; i--) {
        emitInstruction("\taddi $sp, $sp, 4");
        emitInstruction("\tlw $s%d, ($sp)", i);
    }
    
    // Restoring FP
    emitInstruction("\t# Setting FP back to old value");
    emitInstruction("\taddi $sp, $sp, 4");
    emitInstruction("\tlw $fp, ($sp)");
    
    // Return to caller
    emitInstruction("\tjr $ra");
}

static void generateOutputFunction(void) {
    emitInstruction("\n# output function");
    emitInstruction("startoutput:");
    emitInstruction("\tlw $a0, 4($sp)");
    emitInstruction("\tli $v0, 1");
    emitInstruction("\tsyscall");
    emitInstruction("\tjr $ra");
}
