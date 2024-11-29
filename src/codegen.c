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

// Label counter for unique labels
static int labelCounter = 0;

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
    // Initialize registers before starting code generation
    initRegisters();
    
    if (!node) return NO_REGISTER;
    
    switch (node->nodeKind) {
        case ADDOP:
        case MULOP:
            return generateArithmeticOp(node);
            
        case IDENTIFIER:
            return generateIdentifier(node);
            
        case INTEGER:
            return generateInteger(node);
            
        case RELOP:
            return generateRelationalOp(node);
            
        case ASSIGNSTMT:
            return generateAssignment(node);
            
        case LOOPSTMT:
            return generateWhileLoop(node);
            
        case CONDSTMT:
            return generateIfStatement(node);
            
        case FUNCCALLEXPR:
            return generateFunctionCall(node);
    }
    return NO_REGISTER;
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
    
    switch(node->val) {
        case 1:  // Less than
            emitInstruction("    slt $t%d, $t%d, $t%d", result, t1, t2);
            break;
        case 2:  // Greater than
            emitInstruction("    slt $t%d, $t%d, $t%d", result, t2, t1);
            break;
        case 4:  // Equal
            emitInstruction("    seq $t%d, $t%d, $t%d", result, t1, t2);
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
    char *startLabel = generateLabel("while_start");
    char *endLabel = generateLabel("while_end");
    
    emitInstruction("%s:", startLabel);
    int t1 = generateCode(node->children[0]);
    emitInstruction("    beq $t%d, $zero, %s", t1, endLabel);
    freeRegister(t1);
    
    generateCode(node->children[1]);
    emitInstruction("    j %s", startLabel);
    emitInstruction("%s:", endLabel);
    
    return NO_REGISTER;
}

static int generateIfStatement(tree* node) {
    char *elseLabel = generateLabel("else");
    char *endLabel = generateLabel("endif");
    
    int t1 = generateCode(node->children[0]);
    emitInstruction("    beq $t%d, $zero, %s", t1, elseLabel);
    freeRegister(t1);
    
    generateCode(node->children[1]);
    emitInstruction("    j %s", endLabel);
    
    emitInstruction("%s:", elseLabel);
    if (node->children[2]) {
        generateCode(node->children[2]);
    }
    emitInstruction("%s:", endLabel);
    
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
    // Special case for the "output" function
    if (strcmp(node->children[0]->name, "output") == 0) {
        return output(node);  // Call the output function
    }

    // Normal function call handling for all other functions
    symEntry* func = ST_lookup(node->name);
    if (!func) {
        reportError("Undefined function '%s'", node->name);
        return ERROR_REGISTER;
    }

    // 2. Check argument count
    int expectedArgs = func->num_params;
    int providedArgs = node->numChildren - 1;
    if (expectedArgs != providedArgs) {
        reportError("Function '%s' expects %d arguments but got %d", 
                   node->name, expectedArgs, providedArgs);
        return ERROR_REGISTER;
    }

    // 3. Check argument types
    for (int i = 0; i < providedArgs; i++) {
        dataType expectedType = func->params[i].data_type;
        dataType providedType = getExpressionType(node->children[i+1]);
        if (!isCompatibleType(expectedType, providedType)) {
            reportError("Argument %d of function '%s' expects type %s but got %s",
                       i+1, node->name, typeToString(expectedType), 
                       typeToString(providedType));
            return ERROR_REGISTER;
        }
    }

    // If all checks pass, generate the call code
    emitInstruction("    # Save registers");
    for (int i = 0; i < node->numChildren; i++) {
        int t1 = generateCode(node->children[i]);
        emitInstruction("    move $a%d, $t%d", i, t1);
        freeRegister(t1);
    }
    
    emitInstruction("    jal %s", node->name);
    int result = nextRegister();
    emitInstruction("    move $t%d, $v0", result);
    
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
