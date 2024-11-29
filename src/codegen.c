#include "codegen.h"
#include "tree.h"
#include "strtab.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>  // for strcmp

// Global variables for register management
#define FIRST_SAVED_REG 0    // $s0
#define LAST_SAVED_REG 7     // $s7
#define NUM_SAVED_REGS (LAST_SAVED_REG - FIRST_SAVED_REG + 1)

static int registers[NUM_SAVED_REGS];  // Track $s0-$s7 only
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
static void generateFunctionEpilogue(const char* functionName, int numLocalVars);
static void generateMainSetup(void);
static void generateOutputFunction(void);
static int countLocalVariables(tree* funBody);
// Label counter for unique labels
static int labelCounter = 0;

extern char *nodeNames[];  // Defined in tree.c

void initRegisters(void) {
    for (int i = 0; i < NUM_SAVED_REGS; i++) {
        registers[i] = 0;  // Mark all registers as free
    }
    currentRegister = NO_REGISTER;
}

int nextRegister(void) {
    // Only allocate saved registers ($s0-$s7)
    for (int i = FIRST_SAVED_REG; i <= LAST_SAVED_REG; i++) {
        if (registers[i] == 0) {
            registers[i] = 1;
            currentRegister = i;
            return i;
        }
    }
    fprintf(stderr, "Error: No available saved registers\n");
    exit(1);
}

void freeRegister(int regNum) {
    if (regNum >= FIRST_SAVED_REG && regNum <= LAST_SAVED_REG) {
        registers[regNum] = 0;
        if (currentRegister == regNum) {
            currentRegister = NO_REGISTER;
        }
    }
}

int getCurrentRegister(void) {
    return currentRegister;
}

void setCurrentRegister(int regNum) {
    if (regNum >= -1 && regNum < NUM_SAVED_REGS) {  // -1 is valid for NO_REGISTER
        currentRegister = regNum;
    } else {
        fprintf(stderr, "Error: Invalid register number %d\n", regNum);
    }
}

int generateCode(tree* node) {
    int result = NO_REGISTER;
    if (!node) {
        //printf("DEBUG: Null node received\n");
        return NO_REGISTER;
    }
    
    //printf("DEBUG: Processing node type %d (%s)\n", node->nodeKind, nodeNames[node->nodeKind]);
    if (node->name) {
        //printf("DEBUG: Node name: %s\n", node->name);
    }
    //printf("DEBUG: Number of children: %d\n", node->numChildren);
    
    switch(node->nodeKind) {
        case PROGRAM:
            //printf("DEBUG: Generating PROGRAM\n");
            generateHeader(stdout);
            generateMainSetup();
            for (int i = 0; i < node->numChildren; i++) {
                //printf("DEBUG: Processing PROGRAM child %d\n", i);
                generateCode(node->children[i]);
            }
            generateOutputFunction();
            break;
            
        case FUNDECL: {
            //printf("DEBUG: Generating FUNDECL for %s\n", node->children[1]->name);
            const char* funcName = node->children[1]->name;
            // Count actual local variables in the function body
            int numLocals = countLocalVariables(node->children[3]);  // Pass funBody node
            generateFunctionPrologue(funcName, numLocals);
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            generateFunctionEpilogue(funcName, numLocals);
            break;
        }
            
        case FUNBODY:
            //printf("DEBUG: Handling FUNBODY node\n");
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            break;
            
        case VAR: {
            //printf("DEBUG: Processing VAR node\n");
            if (node->parent && node->parent->nodeKind == VARDECL) {
                symEntry* entry = ST_lookup(node->children[0]->name);
                //printf("DEBUG: VAR is part of VARDECL, entry scope: %d\n", 
                //       entry ? entry->scope : -1);
                if (entry && entry->scope == GLOBAL_SCOPE) {
                    return NO_REGISTER;
                }
            }
            int reg = generateIdentifier(node->children[0]);
            //printf("DEBUG: VAR node returned register: %d\n", reg);
            return reg;
        }
            
        case EXPRESSION:
            //printf("DEBUG: Processing EXPRESSION node\n");
            result = generateCode(node->children[0]);
            //printf("DEBUG: EXPRESSION returning register: %d\n", result);
            return result;
            
        case DECL:
            //printf("DEBUG: Handling DECL node\n");
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            break;
            
        case VARDECL:
            //printf("DEBUG: Handling VARDECL node\n");
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            break;
            
        case TYPESPEC:
            //printf("DEBUG: Handling TYPESPECIFIER node\n");
            // No code generation needed for type specifier
            break;
            
        case STATEMENTLIST:
            //printf("DEBUG: Handling STATEMENTLIST node\n");
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            break;
            
        case ADDOP:
        case MULOP:
            //printf("DEBUG: Handling arithmetic operation\n");
            result = generateArithmeticOp(node);
            break;
            
        case RELOP:
            //printf("DEBUG: Handling relational operation\n");
            result = generateRelationalOp(node);
            break;
            
        case INTEGER:
            //printf("DEBUG: Handling integer node with value: %d\n", node->val);
            result = generateInteger(node);
            break;
            
        case IDENTIFIER:
            //printf("DEBUG: Handling identifier\n");
            result = generateIdentifier(node);
            break;
            
        case ASSIGNSTMT:
            //printf("DEBUG: Handling assignment statement\n");
            result = generateAssignment(node);
            break;
            
        case LOOPSTMT:
            //printf("DEBUG: Handling while loop\n");
            result = generateWhileLoop(node);
            break;
            
        case CONDSTMT:
            //printf("DEBUG: Handling if statement\n");
            result = generateIfStatement(node);
            break;
            
        case FUNCCALLEXPR:
            //printf("DEBUG: Handling function call\n");
            result = generateFunctionCall(node);
            break;
            
        default:
            //printf("DEBUG: Unhandled node type %d (%s)\n", 
            //       node->nodeKind, 
            //       nodeNames[node->nodeKind]);
            // Continue processing children even for unhandled nodes
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            break;
    }
    
    return result;
}

static int generateArithmeticOp(tree* node) {
    if (node->children[0]->nodeKind == INTEGER && 
        node->children[1]->nodeKind == INTEGER) {
        
        int val1 = node->children[0]->val;
        int val2 = node->children[1]->val;
        int result_val;
        
        // Debug output to help diagnose
        //printf("DEBUG: Arithmetic op: val1=%d, val2=%d, op=%c\n", 
        //       val1, val2, node->val);
        
        if (node->nodeKind == ADDOP) {
            result_val = (node->val == '+') ? val1 + val2 : val1 - val2;
        } else if (node->nodeKind == MULOP) {
            result_val = (node->val == '*') ? val1 * val2 : val1 / val2;
        }
        
        static int s_reg = 0;
        emitInstruction("\t# Integer expression");
        emitInstruction("\tli $s%d, %d", s_reg, result_val);
        return s_reg++;
    }
    return NO_REGISTER;
}

static int generateIdentifier(tree* node) {
    //printf("DEBUG: Generating identifier for: %s\n", node->name);
    if (!node || !node->name) return ERROR_REGISTER;
    
    symEntry* entry = ST_lookup(node->name);
    //printf("DEBUG: Symbol table entry found: %s\n", entry ? "yes" : "no");
    
    // Only generate load instruction if this is a variable reference
    if (entry && node->parent && 
        (node->parent->nodeKind == VAR || 
         node->parent->nodeKind == ASSIGNSTMT)) {
        int result = nextRegister();
        //printf("DEBUG: Allocated register: %d\n", result);
        
        emitInstruction("\t# Variable expression");
        // Add 'var' prefix for global variables
        if (entry->scope == GLOBAL_SCOPE) {
            emitInstruction("\tlw $s%d, var%s", result, node->name);
        } else {
            emitInstruction("\tlw $s%d, %s", result, entry->id);
        }
        return result;
    }
    
    return NO_REGISTER;
}

static int generateInteger(tree* node) {
    int result = nextRegister();
    emitInstruction("\t# Integer expression");
    emitInstruction("\tli $s%d, %d", result, node->val);  // Changed $t to $s
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
    if (!node || node->numChildren < 2) return ERROR_REGISTER;
    
    tree* var_node = node->children[0];
    if (!var_node || var_node->numChildren < 1) return ERROR_REGISTER;
    
    tree* id_node = var_node->children[0];
    int valueReg = generateCode(node->children[1]);
    if (valueReg == ERROR_REGISTER) return ERROR_REGISTER;
    
    symEntry* entry = ST_lookup(id_node->name);
    if (!entry) return ERROR_REGISTER;
    
    emitInstruction("\t# Assignment");
    if (entry->scope == GLOBAL_SCOPE) {
        emitInstruction("\tsw $s%d, var%s", valueReg, id_node->name);
    } else {
        emitInstruction("\tsw $s%d, 4($fp)", valueReg);
    }
    
    return valueReg;
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
    emitInstruction("\tmove $a0, $t%d", t1);  // Put number in $a0
    emitInstruction("\tli $v0, 1");           // 1 = print integer syscall
    emitInstruction("\tsyscall");             // Do the print
    
    freeRegister(t1);
    return NO_REGISTER;
}

static int generateFunctionCall(tree* node) {
    if (!node || node->numChildren < 1) return ERROR_REGISTER;

    emitInstruction("\t# Saving return address");
    emitInstruction("\tsw $ra, ($sp)");
    
    emitInstruction("\n\t# Evaluating and storing arguments");
    if (node->numChildren > 1) {
        tree* argList = node->children[1];
        if (argList) {
            for (int i = 0; i < argList->numChildren; i++) {
                emitInstruction("\n\t# Evaluating argument %d", i);
                int argReg = generateCode(argList->children[i]);
                //printf("DEBUG: Function call argument register: %d\n", argReg);
                
                // Only proceed if we got a valid register
                if (argReg >= 0) {  // Changed condition
                    emitInstruction("\t# Storing argument %d", i);
                    //printf("DEBUG: Storing argument from register $s%d\n", argReg);
                    emitInstruction("\tsw $s%d, -4($sp)", argReg);
                    emitInstruction("\tsubi $sp, $sp, 8");
                    freeRegister(argReg);
                } else {
                    //printf("DEBUG: ERROR - Invalid register returned for argument\n");
                    // Consider adding error handling here
                }
            }
        }
    }
    
    emitInstruction("\n\t# Jump to callee");
    emitInstruction("\t# jal will correctly set $ra as well");
    emitInstruction("\tjal start%s", node->children[0]->name);
    
    // Clean up stack and restore return address
    if (node->numChildren > 1) {
        tree* argList = node->children[1];
        if (argList && argList->numChildren > 0) {
            emitInstruction("\n\t# Deallocating space for arguments");
            emitInstruction("\taddi $sp, $sp, %d", 4);
        }
    }
    
    emitInstruction("\n\t# Resetting return address");
    emitInstruction("\taddi $sp, $sp, 4");
    emitInstruction("\tlw $ra, ($sp)");
    
    // Get return value
    int retReg = nextRegister();
    emitInstruction("\n\t# Move return value into another reg");
    emitInstruction("\tmove $s%d, $v0", retReg);
    
    return retReg;
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
    
    //printf("DEBUG: Traversing %s node (type %d)%s%s\n", 
    //       nodeKindStr,
    //       node->nodeKind,
    //       node->name ? ", name: " : "",
    //       node->name ? node->name : "");
    
    // Print number of children
    //printf("DEBUG: Node has %d children\n", node->numChildren);
    
    // Recursively traverse all children
    for (int i = 0; i < node->numChildren; i++) {
        //printf("DEBUG: Processing child %d of %s node\n", i + 1, nodeKindStr);
        traverseAST(node->children[i]);
    }
    
    return NO_REGISTER;
}

static void generateHeader(FILE* fp) {
    fprintf(fp, "# Global variable allocations:\n");
    fprintf(fp, ".data\n");
    
    for (int i = 0; i < MAXIDS; i++) {
        symEntry* entry = root->strTable[i];
        while (entry) {
            if (entry->scope == GLOBAL_SCOPE && entry->sym_type == ST_SCALAR) {
                fprintf(fp, "var%s:\t.word 0\n", entry->id);  // Removed underscore
            }
            entry = entry->next;
        }
    }
    
    fprintf(fp, "\n.text\n");
}

static void generateMainSetup(void) {
    emitInstruction("\tjal startmain");
    emitInstruction("\tli $v0, 10");
    emitInstruction("\tsyscall");
}

static char* getFunctionLabel(const char* functionName, const char* prefix) {
    static char label[100];  // Static buffer for the label
    snprintf(label, sizeof(label), "%s%s", prefix, functionName);
    return label;
}

static void generateFunctionPrologue(const char* functionName, int numLocalVars) {
    emitInstruction("\t# Function definition");
    emitInstruction("%s:", getFunctionLabel(functionName, "start"));  // e.g., "startmain:"
    
    // Setting up FP
    emitInstruction("\t# Setting up FP");
    emitInstruction("\tsw $fp, ($sp)");
    emitInstruction("\tmove $fp, $sp");
    emitInstruction("\tsubi $sp, $sp, 4\n");
    
    // Saving registers ($s0 to $s7)
    emitInstruction("\t# Saving registers");
    for (int i = 0; i <= 7; i++) {
        emitInstruction("\tsw $s%d, ($sp)", i);
        emitInstruction("\tsubi $sp, $sp, 4");
    }
    
    // Only allocate space if we actually have local variables
    if (numLocalVars > 0) {
        emitInstruction("\n\t# Allocate space for %d local variables.", numLocalVars);
        emitInstruction("\tsubi $sp, $sp, %d\n", numLocalVars * 4);
    }
}

static void generateFunctionEpilogue(const char* functionName, int numLocalVars) {
    emitInstruction("%s:", getFunctionLabel(functionName, "end"));
    
    // Deallocate space for local variables
    if (numLocalVars > 0) {
        emitInstruction("\n\t# Deallocate space for %d local variables.", numLocalVars);
        emitInstruction("\taddi $sp, $sp, %d", numLocalVars * 4);
    }
    
    // Restoring registers ($s7 to $s0)
    emitInstruction("\n\t# Reloading registers");
    for (int i = 7; i >= 0; i--) {
        emitInstruction("\taddi $sp, $sp, 4");
        emitInstruction("\tlw $s%d, ($sp)", i);
    }
    
    // Restoring FP
    emitInstruction("\n\t# Setting FP back to old value");
    emitInstruction("\taddi $sp, $sp, 4");
    emitInstruction("\tlw $fp, ($sp)");
    
    // Return to caller
    emitInstruction("\n\t# Return to caller");
    emitInstruction("\tjr $ra\n");
}

static void generateOutputFunction(void) {
    emitInstruction("# output function");
    emitInstruction("startoutput:");
    emitInstruction("\t# Put argument in the output register");
    emitInstruction("\tlw $a0, 4($sp)");
    emitInstruction("\t# print int is syscall 1");
    emitInstruction("\tli $v0, 1");
    emitInstruction("\tsyscall");
    emitInstruction("\t# jump back to caller");
    emitInstruction("\tjr $ra");
}

static int countLocalVariables(tree* funBody) {
    if (!funBody || funBody->nodeKind != FUNBODY) return 0;
    
    // Local declarations are in the first child of FUNBODY
    tree* localDeclList = funBody->children[0];
    if (!localDeclList || localDeclList->nodeKind != LOCALDECLLIST) return 0;
    
    // Only count actual variable declarations
    int count = 0;
    for (int i = 0; i < localDeclList->numChildren; i++) {
        if (localDeclList->children[i]->nodeKind == VARDECL) {
            count++;
        }
    }
    return count;
}
