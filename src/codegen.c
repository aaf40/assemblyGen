#include "codegen.h"
#include "tree.h"
#include "strtab.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
//#include <unistd.h> 

// Register management
#define MAX_REGISTERS 8  // s0 through s7
#define FIRST_SAVED_REG 0    // $s0
#define LAST_SAVED_REG 7     // $s7
#define NUM_SAVED_REGS (LAST_SAVED_REG - FIRST_SAVED_REG + 1)
#define VALUE_REG 0    // For immediate values and arithmetic results
#define VAR_ACCESS_REG 1    // For variable loads/stores
#define RETURN_REG VAR_ACCESS_REG  // For function return values (same as VAR_ACCESS_REG)

static int labelCounter = 0; // Label counter for function and variable labels
static int loopLabelCounter = 1; // Label counter for loop labels
static int registers[NUM_SAVED_REGS];  // Track $s0-$s7 only
static int currentRegister = NO_REGISTER;

extern char *nodeNames[]; // defined in tree.c

// Forward declarations
static int generateArithmeticOp(tree* node);
static int generateIdentifier(tree* node);
static int generateInteger(tree* node);
static int generateRelationalOp(tree* node);
static int generateAssignment(tree* node);
static int generateWhileLoop(tree* node);
static int generateIfStatement(tree* node);
static int generateFunctionCall(tree* node);
static void generateHeader(FILE* fp);
char* generateLabel(const char* prefix);
static void generateFunctionPrologue(const char* functionName, int numLocalVars);
static void generateFunctionEpilogue(const char* functionName, int numLocalVars);
static void generateMainSetup(void);
static void generateOutputFunction(void);
static int countLocalVariables(tree* funBody);
static bool will_be_local_variable(tree* node, const char* var_name);
static void preprocess_declarations(tree* node);

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

void initRegisters(void) {
    for (int i = 0; i < NUM_SAVED_REGS; i++) {
        registers[i] = 0;  // Mark all registers as free
    }
    currentRegister = NO_REGISTER;
}

int nextRegister(void) {
    //fprintf(stderr, "DEBUG: Attempting to allocate next register\n");
    // Only allocate saved registers ($s0-$s7)
    for (int i = FIRST_SAVED_REG; i <= LAST_SAVED_REG; i++) {
        if (registers[i] == 0) {
            registers[i] = 1;
            currentRegister = i;
            //fprintf(stderr, "DEBUG: Allocated register $s%d\n", i);
            return i;
        }
    }
    //fprintf(stderr, "Error: No available saved registers\n");
    exit(1);
}

void freeRegister(int regNum) {
    //fprintf(stderr, "DEBUG: Attempting to free register $s%d\n", regNum);
    if (regNum >= FIRST_SAVED_REG && regNum <= LAST_SAVED_REG) {
        registers[regNum] = 0;
        if (currentRegister == regNum) {
            currentRegister = NO_REGISTER;
        }
        //fprintf(stderr, "DEBUG: Successfully freed register $s%d\n", regNum);
    }
}

int getCurrentRegister(void) {
    return currentRegister;
}

void setCurrentRegister(int regNum) {
    if (regNum >= -1 && regNum < NUM_SAVED_REGS) {  // -1 is valid for NO_REGISTER
        currentRegister = regNum;
    } else {
        //fprintf(stderr, "Error: Invalid register number %d\n", regNum);
    }
}

int generateCode(tree* node) {
    static int call_count = 0;
    call_count++;
    
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "CALL NUMBER: %d\n", call_count);
    write(2, buffer, strlen(buffer));

    if (node) {
        //fprintf(stderr, "NODE TYPE: %s | NODE KIND:(%d)\n", 
                //nodeNames[node->nodeKind], node->nodeKind);
    }
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
            //fprintf(stderr, "DEBUG: PROGRAM case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
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
            //fprintf(stderr, "\n=== FUNCTION DECLARATION PROCESSING ===\n");
            //fprintf(stderr, "DEBUG: FUNDECL case reached. Node type: %s, name: %s\n",
                    //nodeNames[node->nodeKind],
                    //node->name ? node->name : "(null)");
            
            const char* funcName = node->children[1] ? node->children[1]->name : "NULL";
            //fprintf(stderr, "DEBUG: Function name: %s\n", funcName);
            
            // Debug print children info
            //fprintf(stderr, "DEBUG: Number of children: %d\n", node->numChildren);
            for (int i = 0; i < node->numChildren; i++) {
                if (node->children[i]) {
                    //fprintf(stderr, "DEBUG: Child %d type: %s\n", 
                           // i, nodeNames[node->children[i]->nodeKind]);
                } else {
                    //fprintf(stderr, "DEBUG: Child %d is NULL\n", i);
                }
            }
            
            // Create new scope BEFORE processing any declarations
            new_scope();
            //fprintf(stderr, "DEBUG: Created new scope\n");
            
            // Process parameters and add them to symbol table
            tree* params = node->children[2];  // Parameter list node
            if (params) {
                //fprintf(stderr, "DEBUG: Found parameter list node, type: %s\n", 
                        //nodeNames[params->nodeKind]);
                
                for (int i = 0; i < params->numChildren; i++) {
                    tree* param = params->children[i];
                    if (param) {
                        //fprintf(stderr, "DEBUG: Parameter node type: %s\n", 
                                //nodeNames[param->nodeKind]);
                        
                        // The parameter name might be in a child node
                        tree* paramName = NULL;
                        if (param->numChildren > 0) {
                            paramName = param->children[1];  // Usually identifier is second child
                        }
                        
                        if (paramName && paramName->name) {
                            //fprintf(stderr, "DEBUG: Processing parameter %d: %s\n", 
                                   // i, paramName->name);
                            symEntry* paramEntry = ST_insert(paramName->name, DT_INT, ST_SCALAR);
                            if (paramEntry) {
                                //fprintf(stderr, "DEBUG: Successfully added parameter %s\n", 
                                       // paramName->name);
                            }
                        } else {
                            //fprintf(stderr, "DEBUG: Could not find parameter name in node\n");
                            // Debug print the parameter node structure
                            //fprintf(stderr, "DEBUG: Parameter node details:\n");
                            //fprintf(stderr, "  NumChildren: %d\n", param->numChildren);
                            for (int j = 0; j < param->numChildren; j++) {
                                if (param->children[j]) {
                                    //fprintf(stderr, "  Child %d type: %s, name: %s\n",
                                           // j, nodeNames[param->children[j]->nodeKind],
                                           // param->children[j]->name ? param->children[j]->name : "NULL");
                                }
                            }
                        }
                    }
                }
            }
            
            // Process local declarations first
            tree* funBody = node->children[3];
            if (funBody && funBody->nodeKind == FUNBODY) {
                // Process local declarations (first child of FUNBODY)
                tree* localDecls = funBody->children[0];
                if (localDecls) {
                    generateCode(localDecls);
                }
            }
            
            // Generate function prologue
            int numLocals = countLocalVariables(node->children[3]);
            generateFunctionPrologue(funcName, numLocals);
            
            // Generate code for function body
            generateCode(node->children[3]);
            
            generateFunctionEpilogue(funcName, numLocals);
            up_scope();
            
            //fprintf(stderr, "=== END FUNCTION DECLARATION PROCESSING ===\n\n");
            break;
        }
            
        case FUNBODY: {
            //fprintf(stderr, "DEBUG: FUNBODY case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
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
            //fprintf(stderr, "DEBUG: VAR case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
            
            if (node->parent && node->parent->nodeKind == PROGRAM) {
                //fprintf(stderr, "DEBUG: VAR - Skipping global variable\n");
                return NO_REGISTER;
            }
            
            if (node->parent && node->parent->nodeKind == VARDECL) {
                symEntry* entry = ST_lookup(node->children[0]->name);
                if (entry && entry->scope == GLOBAL_SCOPE) {
                    //fprintf(stderr, "DEBUG: VAR - Skipping global vardecl\n");
                    return NO_REGISTER;
                }
            }
            
            //fprintf(stderr, "DEBUG: VAR - About to call generateIdentifier\n");
            int reg = generateIdentifier(node->children[0]);
            //fprintf(stderr, "DEBUG: VAR - generateIdentifier returned $s%d\n", reg);
            return reg;
        }
            
        case EXPRESSION:
            //fprintf(stderr, "DEBUG: EXPRESSION case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
            //printf("DEBUG: Processing EXPRESSION node\n");
            result = generateCode(node->children[0]);
            //printf("DEBUG: EXPRESSION returning register: %d\n", result);
            return result;
            
        case DECL:
            //fprintf(stderr, "DEBUG: DECL case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
            //printf("DEBUG: Handling DECL node\n");
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            break;
            
        case VARDECL:
            //fprintf(stderr, "DEBUG: VARDECL case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
            //printf("DEBUG: Handling VARDECL node\n");
            // Check if this is a global or local variable declaration
            if (node->children[1] && node->children[1]->name) {
                if (!will_be_local_variable(node, node->children[1]->name)) {
                    // Global variable - insert into root scope
                    //fprintf(stderr, "DEBUG: Inserting '%s' as global variable\n", 
                            //node->children[1]->name);
                    ST_insert(node->children[1]->name, DT_INT, ST_SCALAR);
                } else {
                    // Local variable - insert into current scope
                    //fprintf(stderr, "DEBUG: Inserting '%s' as local variable\n", 
                            ////node->children[1]->name);
                    symEntry* entry = ST_lookup(node->children[1]->name);
                    if (!entry || entry->scope != LOCAL_SCOPE) {
                        ST_insert(node->children[1]->name, DT_INT, ST_SCALAR);
                    }
                }
            }
            break;
            
        case TYPESPEC:
            //fprintf(stderr, "DEBUG: TYPESPEC case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
            //printf("DEBUG: Handling TYPESPECIFIER node\n");
            // No code generation needed for type specifier
            break;
            
        case STATEMENTLIST:
            //fprintf(stderr, "DEBUG: STATEMENTLIST case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
            //printf("DEBUG: Handling STATEMENTLIST node\n");
            for (int i = 0; i < node->numChildren; i++) {
                generateCode(node->children[i]);
            }
            break;
            
        case ADDOP:
        case MULOP:
            //fprintf(stderr, "DEBUG: ADDOP case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
            //printf("DEBUG: Handling arithmetic operation\n");
            result = generateArithmeticOp(node);
            break;
            
        case RELOP:
            //fprintf(stderr, "DEBUG: RELOP case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
            //printf("DEBUG: Handling relational operation\n");
            result = generateRelationalOp(node);
            break;
            
        case INTEGER:
            //fprintf(stderr, "DEBUG: INTEGER case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
            //printf("DEBUG: Handling integer node with value: %d\n", node->val);
            result = generateInteger(node);
            break;
            
        case IDENTIFIER:
            //fprintf(stderr, "DEBUG: IDENTIFIER case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
            //printf("DEBUG: Handling identifier\n");
            result = generateIdentifier(node);
            break;
            
        case ASSIGNSTMT:
            //fprintf(stderr, "DEBUG: ASSIGNSTMT case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
            //printf("DEBUG: Handling assignment statement\n");
            result = generateAssignment(node);
            break;
            
        case LOOPSTMT:
            //fprintf(stderr, "DEBUG: LOOPSTMT case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
            //printf("DEBUG: Handling while loop\n");
            result = generateWhileLoop(node);
            break;
            
        case CONDSTMT:
            //fprintf(stderr, "DEBUG: CONDSTMT case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
           // node->name ? node->name : "(null)");
            //printf("DEBUG: Handling if statement\n");
            result = generateIfStatement(node);
            break;
            
        case FUNCCALLEXPR: {
            //fprintf(stderr, "DEBUG: FUNCCALLEXPR case reached. Node type: %s, name: %s\n",
           // nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
            // Save current register state
            int savedRegisters[NUM_SAVED_REGS];
            memcpy(savedRegisters, registers, sizeof(registers));
            
            // Don't initialize registers here anymore
            result = generateFunctionCall(node);
            
            // Restore register state after function call
            memcpy(registers, savedRegisters, sizeof(registers));
            break;
        }
            
        case FACTOR: {
            //fprintf(stderr, "DEBUG: FACTOR case reached\n");
            // Just pass through the register from the child
            int reg = generateCode(node->children[0]);
            //fprintf(stderr, "DEBUG: FACTOR - passing through register $s%d\n", reg);
            return reg;
        }
            
        default:
            //printf("DEBUG: Unhandled node type %d (%s)\n", 
            //       node->nodeKind, 
            //       nodeNames[node->nodeKind]);
            // Continue processing children even for unhandled nodes
            //fprintf(stderr, "DEBUG: Default case reached. Node type: %s, name: %s\n",
            //nodeNames[node->nodeKind],
            //node->name ? node->name : "(null)");
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

static int generateIdentifier(tree* node) {
    //fprintf(stderr, "DEBUG: Enter generateIdentifier\n");
    
    if (!node || !node->name) {
        //fprintf(stderr, "DEBUG: generateIdentifier - node or name is NULL\n");
        return ERROR_REGISTER;
    }
    
    symEntry* entry = ST_lookup(node->name);
    //fprintf(stderr, "DEBUG: generateIdentifier - looked up %s\n", node->name);
    
    if (entry) {
        //fprintf(stderr, "DEBUG: generateIdentifier - entry is not NULL\n");
        //fprintf(stderr, "DEBUG: generateIdentifier - entry->id is %s\n", entry->id);
        //fprintf(stderr, "DEBUG: generateIdentifier - entry->scope is %d\n", entry->scope);
        
        int reg = nextRegister();
        //fprintf(stderr, "DEBUG: generateIdentifier - allocated register $s%d\n", reg);
        
        if (entry->scope == GLOBAL_SCOPE) {
            emitInstruction("\tlw $s%d, var%s", reg, entry->id);
        } else {
            emitInstruction("\tlw $s%d, 4($fp)", reg);
        }
        //fprintf(stderr, "DEBUG: generateIdentifier - emitted load instruction\n");
        //fprintf(stderr, "DEBUG: generateIdentifier - returning register $s%d\n", reg);
        return reg;  // Return the allocated register number
    }
    
    //fprintf(stderr, "DEBUG: generateIdentifier - entry is NULL\n");
    return ERROR_REGISTER;
}

static int generateInteger(tree* node) {
    int reg = nextRegister();  // Get next available register instead of hardcoding
    emitInstruction("\tli $s%d, %d", reg, node->val);
    return reg;
}

static int generateRelationalOp(tree* node) {
    //fprintf(stderr, "DEBUG: Generating relational op\n");
    
    // Generate code for left operand (variable)
    emitInstruction("\t# Variable expression");
    int leftReg = generateCode(node->children[0]);
    //fprintf(stderr, "DEBUG: generateRelationalOp - leftReg = $s%d\n", leftReg);
    
    if (leftReg < 0) {
        //fprintf(stderr, "ERROR: Invalid left register in relational op\n");
        return ERROR_REGISTER;
    }
    
    // Generate code for right operand (constant)
    emitInstruction("\t# Integer expression");
    int rightReg = generateCode(node->children[1]);
    //fprintf(stderr, "DEBUG: generateRelationalOp - rightReg = $s%d\n", rightReg);
    
    if (rightReg < 0) {
        //fprintf(stderr, "ERROR: Invalid right register in relational op\n");
        freeRegister(leftReg);
        return ERROR_REGISTER;
    }
    
    // Generate comparison code
    emitInstruction("\t# Relational comparison");
    emitInstruction("\t# LT");
    
    int diffReg = nextRegister();
    int resultReg = nextRegister();
    
    //fprintf(stderr, "DEBUG: generateRelationalOp - diffReg = $s%d\n", diffReg);
    //fprintf(stderr, "DEBUG: generateRelationalOp - resultReg = $s%d\n", resultReg);
    
    emitInstruction("\tsub $s%d, $s%d, $s%d", diffReg, leftReg, rightReg);
    emitInstruction("\tslt $s%d, $s%d, $0", resultReg, diffReg);
    
    // Free registers in correct order
    freeRegister(leftReg);
    freeRegister(rightReg);
    freeRegister(diffReg);
    
    return resultReg;
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
    char* startLabel = generateLoopLabel();
    char* endLabel = generateLoopLabel();
    
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
    //fprintf(stderr, "DEBUG: Generating if statement\n");
    
    emitInstruction("\t# Conditional statement");
    
    // Generate condition code and get the register with the result
    int condReg = generateCode(node->children[0]);  // This calls generateRelationalOp
    
    // Now generate the branch using the result register
    char labelNum[10];
    sprintf(labelNum, "L%d", loopLabelCounter++);
    emitInstruction("\tbeq $s%d, $0, %s", condReg, labelNum);
    
    // Free all registers used in the condition
    freeRegister(0);  // Free $s0 (variable)
    freeRegister(1);  // Free $s1 (constant)
    freeRegister(2);  // Free $s2 (difference)
    freeRegister(3);  // Free $s3 (comparison result)
    
    // Generate 'then' part
    generateCode(node->children[1]);
    
    // Label for end of if
    emitInstruction("%s:", labelNum);
    
    return NO_REGISTER;
}

static int generateFunctionCall(tree* node) {
    if (!node || !node->children[0] || !node->children[0]->name) 
        return ERROR_REGISTER;
    
    char* funcName = node->children[0]->name;
    //printf("DEBUG === Function Call Analysis ===\n");
    //printf("DEBUG Calling function: %s\n", funcName);
    
    // Save return address
    emitInstruction("\t# Saving return address");
    emitInstruction("\tsw $ra, ($sp)");
    
    if (strcmp(funcName, "output") == 0) {
        emitInstruction("\n\t# Evaluating and storing arguments\n");
        emitInstruction("\t# Evaluating argument 0");
        emitInstruction("\t# Variable expression");
        
        // Get the argument list and first argument
        if (node->children[1]) {  // ARGLIST
            //printf("DEBUG Found ARGLIST node (kind=%d)\n", node->children[1]->nodeKind);
            
            tree* arglist = node->children[1];
            if (arglist->children[0]) {  // EXPRESSION
                tree* expr = arglist->children[0];
                //printf("DEBUG Found argument node (kind=%d)\n", expr->nodeKind);
                
                if (expr->children[0]) {  // FACTOR
                    tree* factor = expr->children[0];
                    //printf("DEBUG Found factor node (kind=%d)\n", factor->nodeKind);
                    
                    if (factor->children[0]) {  // VAR
                        tree* var = factor->children[0];
                        //printf("DEBUG Found var node (kind=%d)\n", var->nodeKind);
                        
                        // Check for IDENTIFIER child of VAR
                        if (var->children[0]) {
                            tree* id = var->children[0];
                            //printf("DEBUG Found identifier node (kind=%d, name=%s)\n",
                                    //id->nodeKind, id->name ? id->name : "NULL");
                            
                            if (id->name) {
                                symEntry* entry = ST_lookup(id->name);
                                if (entry) {
                                    //printf("DEBUG Symbol entry found - scope=%d, id=%s\n",
                                            //entry->scope, entry->id);
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

char* generateLoopLabel(void) {
    char* label = malloc(20);  // Smaller size since format is simpler
    sprintf(label, "L%d", loopLabelCounter++);
    return label;
}

void emitInstruction(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

static bool will_be_local_variable(tree* node, const char* var_name) {
    // Start from the given node and traverse up to find FUNDECL
    tree* current = node;
    while (current) {
        if (current->nodeKind == FUNDECL) {
            // If we find a function declaration above this node,
            // then this variable will be local
            //printf("DEBUG Variable '%s' will be local to function\n", var_name);
            return true;
        }
        current = current->parent;
    }
    //printf("DEBUG Variable '%s' will be global\n", var_name);
    return false;
}

static void generateHeader(FILE* fp) {
    fprintf(fp, "# Global variable allocations:\n");
    fprintf(fp, ".data\n");
    
    //printf("DEBUG === generateHeader called ===\n");
    
    // Create a hash table or array to track printed variables
    bool printed_vars[MAXIDS] = {false};  // Initialize all to false
    bool hasGlobals = false;
    
    if (root) {
        for (int i = 0; i < MAXIDS; i++) {
            symEntry* entry = root->strTable[i];
            while (entry) {
                // Create a temporary tree node to check scope
                tree* temp_node = maketree(IDENTIFIER);
                setName(temp_node, entry->id);
                
                if (has_global_scope(temp_node) && 
                    entry->sym_type == ST_SCALAR && 
                    entry->data_type == DT_INT &&
                    strcmp(entry->id, "main") != 0 && 
                    strcmp(entry->id, "output") != 0 &&
                    entry->parent_function == NULL) {
                    
                    // Check if we've already printed this variable
                    int hash = entry->id[0] % MAXIDS;  // Simple hash function
                    if (!printed_vars[hash]) {
                        //printf("DEBUG Found truly global variable '%s'\n", entry->id);
                        fprintf(fp, "var%s:\t.word 0\n", entry->id);
                        printed_vars[hash] = true;
                        hasGlobals = true;
                    }
                }
                
                // Clean up temporary node
                free(temp_node->name);
                free(temp_node);
                
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
    
    //printf("DEBUG === generateVarDecl for %s ===\n", 
            ////node->children[1]->name);
    
    // Mark this variable as local if it's in a function scope
    symEntry* entry = ST_lookup(node->children[1]->name);
    if (entry && current_scope != root) {
        // The parent_function should already be set by ST_insert
        //printf("DEBUG Variable '%s' is in function scope\n", entry->id);
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
        
        //printf("DEBUG Processing variable '%s'\n", var_name);
        
        if (parent_func && parent_func->children[0]) {
            //printf("DEBUG Found in function '%s'\n", parent_func->children[0]->name);
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
            //printf("DEBUG Variable '%s' is global\n", var_name);
            symEntry* entry = ST_lookup(var_name);
            if (entry) {
                entry->scope = GLOBAL_SCOPE;
                entry->parent_function = NULL;
            }
        }
    }
}