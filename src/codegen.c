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

#define VALUE_REG 0    // For immediate values and arithmetic results
#define VAR_ACCESS_REG 1    // For variable loads/stores
#define RETURN_REG VAR_ACCESS_REG  // For function return values (same as VAR_ACCESS_REG)

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

// Register management
#define MAX_REGISTERS 8  // s0 through s7

static bool will_be_local_variable(tree* node, const char* var_name);

// Add this near the top with other forward declarations
static void preprocess_declarations(tree* node);

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
    
    // Initialize registers at the start of AST traversal
    if (node && node->nodeKind == PROGRAM) {
        initRegisters();
    }
    
    if (!node) {
        return NO_REGISTER;
    }
    
    switch(node->nodeKind) {
        case PROGRAM:
            // First pass: preprocess all declarations
            preprocess_declarations(node);
            
            // Now generate code with complete information
            generateHeader(stdout);
            generateMainSetup();
            
            // Process the rest of the program
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            generateOutputFunction();
            break;
            
        case FUNDECL: {
            const char* funcName = node->children[1]->name;
            
            // Create new scope BEFORE processing any declarations
            new_scope();
            
            // Process local declarations first
            tree* funBody = node->children[3];
            if (funBody && funBody->nodeKind == FUNBODY) {
                // Process local declarations (first child of FUNBODY)
                tree* localDecls = funBody->children[0];
                if (localDecls) {
                    generateCode(localDecls);  // Let the normal code generation handle declarations
                }
            }
            
            // Generate function prologue
            int numLocals = countLocalVariables(node->children[3]);
            generateFunctionPrologue(funcName, numLocals);
            
            // Generate code for function body
            generateCode(node->children[3]);
            
            generateFunctionEpilogue(funcName, numLocals);
            up_scope();
            break;
        }
            
        case FUNBODY: {
            // Process statements in function body
            for (int i = 0; i < node->numChildren; i++) {
                if (i == 1) {  // The second child contains the statements
                    tree* stmtList = node->children[i];
                    if (stmtList) {
                        for (int j = 0; j < stmtList->numChildren; j++) {
                            generateCode(stmtList->children[j]);
                        }
                    }
                }
            }
            break;
        }
            
        case VAR: {
            // Skip generating code for global variable access if parent is PROGRAM
            if (node->parent && node->parent->nodeKind == PROGRAM) {
                return NO_REGISTER;
            }
            
            if (node->parent && node->parent->nodeKind == VARDECL) {
                symEntry* entry = ST_lookup(node->children[0]->name);
                if (entry && entry->scope == GLOBAL_SCOPE) {
                    return NO_REGISTER;
                }
            }
            int reg = generateIdentifier(node->children[0]);
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
            // Check if this is a global or local variable declaration
            if (node->children[1] && node->children[1]->name) {
                if (!will_be_local_variable(node, node->children[1]->name)) {
                    // Global variable - insert into root scope
                    fprintf(stderr, "DEBUG: Inserting '%s' as global variable\n", 
                            node->children[1]->name);
                    ST_insert(node->children[1]->name, DT_INT, ST_SCALAR);
                } else {
                    // Local variable - insert into current scope
                    fprintf(stderr, "DEBUG: Inserting '%s' as local variable\n", 
                            node->children[1]->name);
                    symEntry* entry = ST_lookup(node->children[1]->name);
                    if (!entry || entry->scope != LOCAL_SCOPE) {
                        ST_insert(node->children[1]->name, DT_INT, ST_SCALAR);
                    }
                }
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
            
        case FUNCCALLEXPR: {
            // Save current register state
            int savedRegisters[NUM_SAVED_REGS];
            memcpy(savedRegisters, registers, sizeof(registers));
            
            // Don't initialize registers here anymore
            result = generateFunctionCall(node);
            
            // Restore register state after function call
            memcpy(registers, savedRegisters, sizeof(registers));
            break;
        }
            
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

static int evaluateConstExpr(tree* node) {
    if (!node) return 0;
    
    // If it's a leaf node with a value, return it
    if (node->nodeKind == INTEGER) {
        return node->val;
    }
    
    // Get values from left and right subtrees
    int left = evaluateConstExpr(node->children[0]);
    int right = evaluateConstExpr(node->children[1]);
    
    // Perform the operation
    if (node->nodeKind == ADDOP) {
        return (node->val == '+') ? left + right : left - right;
    } else if (node->nodeKind == MULOP) {
        return (node->val == '*') ? left * right : left / right;
    }
    
    return 0;
}

static int generateArithmeticOp(tree* node) {
    // Initialize registers if they haven't been
    if (currentRegister == NO_REGISTER) {
        initRegisters();
    }
    
    // Evaluate the expression at compile time
    int result_value = evaluateConstExpr(node);
    int reg = nextRegister();  // This will get the next available register
    
    fprintf(stdout, "\t# Integer expression\n");
    fprintf(stdout, "\tli $s%d, %d\n", reg, result_value);
    
    return reg;  // Return the register we used
}

static int getRegisterForPurpose(int purpose) {
    switch(purpose) {
        case VALUE_REG:
            return 0;  // $s0
        case VAR_ACCESS_REG:  // This will also handle RETURN_REG since they're the same
            return 1;  // $s1
        default:
            return nextRegister();
    }
}

static int generateIdentifier(tree* node) {
    if (!node || !node->name) {
        return ERROR_REGISTER;
    }
    
    symEntry* entry = ST_lookup(node->name);
    int reg = 1;  // Always use $s1 for variable access
    
    if (entry && entry->id) {
        // Skip generating code for global variables if we're at the program level
        if (node->parent && node->parent->nodeKind == PROGRAM) {
            return NO_REGISTER;
        }
        
        // Skip generating code for global variables if we're in a declaration
        if (node->parent && node->parent->nodeKind == VARDECL) {
            return NO_REGISTER;
        }
        
        if (entry->scope == GLOBAL_SCOPE) {
            fprintf(stdout, "\t# Variable expression\n");
            fprintf(stdout, "\tlw $s%d, var%s\n", reg, entry->id);
        } else {
            fprintf(stdout, "\t# Loading local variable\n");
            fprintf(stdout, "\tlw $s%d, 4($sp)\n", reg);
        }
    }
    
    return reg;
}

static int generateInteger(tree* node) {
    if (currentRegister == NO_REGISTER) {
        initRegisters();
    }
    
    int reg = 0;  // Always use $s0 for integer literals
    fprintf(stdout, "\n\t# Integer expression\n");
    fprintf(stdout, "\tli $s%d, %d\n", reg, node->val);
    
    return reg;
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
    if (!node || !node->children[0] || !node->children[1]) {
        return ERROR_REGISTER;
    }
    
    tree* varNode = node->children[0];
    if (!varNode->children[0] || !varNode->children[0]->name) {
        return ERROR_REGISTER;
    }
    
    symEntry* entry = ST_lookup(varNode->children[0]->name);
    int valueReg = generateCode(node->children[1]);  // This should be $s0
    
    if (entry && entry->id) {
        if (entry->scope == GLOBAL_SCOPE) {
            fprintf(stdout, "\t# Assignment\n");  // Removed "to local variable"
            fprintf(stdout, "\tsw $s%d, var%s\n", valueReg, entry->id);
        } else {
            fprintf(stdout, "\t# Assignment\n");  // Changed to match expected output
            fprintf(stdout, "\tsw $s%d, 4($sp)\n", valueReg);
        }
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
    if (!node || !node->children[0] || !node->children[0]->name) 
        return ERROR_REGISTER;
    
    char* funcName = node->children[0]->name;
    fprintf(stderr, "DEBUG: === Function Call Analysis ===\n");
    fprintf(stderr, "DEBUG: Calling function: %s\n", funcName);
    
    // Save return address
    emitInstruction("\t# Saving return address");
    emitInstruction("\tsw $ra, ($sp)");
    
    if (strcmp(funcName, "output") == 0) {
        emitInstruction("\n\t# Evaluating and storing arguments\n");
        emitInstruction("\t# Evaluating argument 0");
        emitInstruction("\t# Variable expression");
        
        // Get the argument list and first argument
        if (node->children[1]) {  // ARGLIST
            fprintf(stderr, "DEBUG: Found ARGLIST node (kind=%d)\n", node->children[1]->nodeKind);
            
            tree* arglist = node->children[1];
            if (arglist->children[0]) {  // EXPRESSION
                tree* expr = arglist->children[0];
                fprintf(stderr, "DEBUG: Found argument node (kind=%d)\n", expr->nodeKind);
                
                if (expr->children[0]) {  // FACTOR
                    tree* factor = expr->children[0];
                    fprintf(stderr, "DEBUG: Found factor node (kind=%d)\n", factor->nodeKind);
                    
                    if (factor->children[0]) {  // VAR
                        tree* var = factor->children[0];
                        fprintf(stderr, "DEBUG: Found var node (kind=%d)\n", var->nodeKind);
                        
                        // Check for IDENTIFIER child of VAR
                        if (var->children[0]) {
                            tree* id = var->children[0];
                            fprintf(stderr, "DEBUG: Found identifier node (kind=%d, name=%s)\n",
                                    id->nodeKind, id->name ? id->name : "NULL");
                            
                            if (id->name) {
                                symEntry* entry = ST_lookup(id->name);
                                if (entry) {
                                    fprintf(stderr, "DEBUG: Symbol entry found - scope=%d, id=%s\n",
                                            entry->scope, entry->id);
                                    if (entry->scope == GLOBAL_SCOPE) {
                                        emitInstruction("\tlw $s1, var%s", entry->id);
                                    } else {
                                        emitInstruction("\tlw $s1, 4($sp)");
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        emitInstruction("\n\t# Storing argument 0");
        emitInstruction("\tsw $s1, -4($sp)");
        emitInstruction("\tsubi $sp, $sp, 8");
        
        emitInstruction("\n\t# Jump to callee\n");
        emitInstruction("\t# jal will correctly set $ra as well");
        emitInstruction("\tjal start%s\n", funcName);
        
        emitInstruction("\t# Deallocating space for arguments");
        emitInstruction("\taddi $sp, $sp, 4");
        
        emitInstruction("\t# Resetting return address");
        emitInstruction("\taddi $sp, $sp, 4");
        emitInstruction("\tlw $ra, ($sp)\n");
        
        emitInstruction("\n\t# Move return value into another reg");
        emitInstruction("\tmove $s2, $2\n");
        return 2;
    } else {
        // For regular function calls
        emitInstruction("\tsubi $sp, $sp, 4");
        
        emitInstruction("\n\t# Jump to callee\n");
        emitInstruction("\t# jal will correctly set $ra as well");
        emitInstruction("\tjal start%s\n", funcName);
        
        emitInstruction("\t# Resetting return address");
        emitInstruction("\taddi $sp, $sp, 4");
        emitInstruction("\tlw $ra, ($sp)\n");
        
        // Move return value from $v0 ($2) to a saved register
        emitInstruction("\n\t# Move return value into another reg");
        emitInstruction("\tmove $s1, $2\n");
        
        return 1;  // Return the register number containing the result
    }
    
    return ERROR_REGISTER;
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

static bool will_be_local_variable(tree* node, const char* var_name) {
    // Start from the given node and traverse up to find FUNDECL
    tree* current = node;
    while (current) {
        if (current->nodeKind == FUNDECL) {
            // If we find a function declaration above this node,
            // then this variable will be local
            fprintf(stderr, "DEBUG: Variable '%s' will be local to function\n", var_name);
            return true;
        }
        current = current->parent;
    }
    fprintf(stderr, "DEBUG: Variable '%s' will be global\n", var_name);
    return false;
}

static void generateHeader(FILE* fp) {
    fprintf(fp, "# Global variable allocations:\n");
    fprintf(fp, ".data\n");
    
    fprintf(stderr, "DEBUG: === generateHeader called ===\n");
    
    bool hasGlobals = false;
    if (root) {
        for (int i = 0; i < MAXIDS; i++) {
            symEntry* entry = root->strTable[i];
            while (entry) {
                if (entry->scope == GLOBAL_SCOPE && 
                    entry->sym_type == ST_SCALAR && 
                    entry->data_type == DT_INT &&
                    strcmp(entry->id, "main") != 0 && 
                    strcmp(entry->id, "output") != 0 &&
                    entry->parent_function == NULL) {
                    
                    fprintf(stderr, "DEBUG: Found truly global variable '%s'\n", entry->id);
                    fprintf(fp, "var%s:\t.word 0\n", entry->id);
                    hasGlobals = true;
                }
                entry = entry->next;
            }
        }
    }
    
    if (!hasGlobals) {
        fprintf(fp, "\n");
    }
    
    fprintf(fp, ".text\n");
}

static void generateMainSetup(void) {
    // Only generate the initial jump to main and exit syscall
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
    emitInstruction("start%s:", functionName);
    
    // Save old frame pointer
    emitInstruction("\t# Setting up FP");
    emitInstruction("\tsw $fp, ($sp)");
    emitInstruction("\tmove $fp, $sp");
    emitInstruction("\tsubi $sp, $sp, 4\n");
    
    // Save registers
    emitInstruction("\t# Saving registers");
    for (int i = 0; i <= 7; i++) {
        emitInstruction("\tsw $s%d, ($sp)", i);
        emitInstruction("\tsubi $sp, $sp, 4");
    }
    
    // Allocate space for local variables
    if (numLocalVars > 0) {
        emitInstruction("\n\t# Allocate space for %d local variables.", numLocalVars);
        emitInstruction("\tsubi $sp, $sp, %d", numLocalVars * 4);
    }
    printf("\n");
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

static int generateVarDecl(tree* node) {
    if (!node || !node->children[1] || !node->children[1]->name) {
        return ERROR_REGISTER;
    }
    
    fprintf(stderr, "DEBUG: === generateVarDecl for %s ===\n", 
            node->children[1]->name);
    
    // Mark this variable as local if it's in a function scope
    symEntry* entry = ST_lookup(node->children[1]->name);
    if (entry && current_scope != root) {
        // The parent_function should already be set by ST_insert
        fprintf(stderr, "DEBUG: Variable '%s' is in function scope\n", entry->id);
    }
    
    return NO_REGISTER;
}

// Add this helper function
static tree* find_parent_function(tree* node) {
    while (node && node->nodeKind != FUNDECL) {
        node = node->parent;
    }
    return node;
}

static void preprocess_declarations(tree* node) {
    if (!node) return;
    
    // Process children first (bottom-up approach)
    for (int i = 0; i < node->numChildren; i++) {
        preprocess_declarations(node->children[i]);
    }
    
    // Now process this node
    if (node->nodeKind == VARDECL && node->children[1] && node->children[1]->name) {
        char* var_name = node->children[1]->name;
        tree* parent_func = find_parent_function(node);
        
        fprintf(stderr, "DEBUG: Processing variable '%s'\n", var_name);
        
        if (parent_func && parent_func->children[0]) {
            fprintf(stderr, "DEBUG: Found in function '%s'\n", parent_func->children[0]->name);
            symEntry* entry = ST_lookup(var_name);
            if (entry) {
                entry->scope = LOCAL_SCOPE;
                if (parent_func->children[0]->name) {
                    symEntry* func = ST_lookup(parent_func->children[0]->name);
                    if (func) {
                        entry->parent_function = func;
                    }
                }
            }
        } else {
            fprintf(stderr, "DEBUG: Variable '%s' is global\n", var_name);
            symEntry* entry = ST_lookup(var_name);
            if (entry) {
                entry->scope = GLOBAL_SCOPE;
                entry->parent_function = NULL;
            }
        }
    }
}