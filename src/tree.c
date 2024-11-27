#include "tree.h"
#include "strtab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* extern declarations */
extern int yylineno;
extern tree *ast;

/* Global variables */
tree *ast = NULL;

/* string values for ast node types, makes tree output more readable */
char *nodeNames[33] = {"program", "declList", "decl", "varDecl", "typeSpecifier",
                       "funDecl", "formalDeclList", "formalDecl", "funBody",
                       "localDeclList", "statementList", "statement", "compoundStmt",
                       "assignStmt", "condStmt", "loopStmt", "returnStmt","expression",
                       "relop", "addExpr", "addop", "term", "mulop", "factor",
                       "funcCallExpr", "argList", "integer", "identifier", "var",
                       "arrayDecl", "char", "funcTypeName"};

char *typeNames[3] = {"int", "char", "void"};
char *ops[10] = {"+", "-", "*", "/", "<", "<=", "==", ">=", ">", "!="};

static tree *current_function = NULL;

void setCurrentFunction(tree *func) {
    current_function = func;
}

tree *getCurrentFunction() {
    return current_function;
}

tree *maketree(int kind) {
      tree *this = (tree *) malloc(sizeof(struct treenode));
      this->nodeKind = kind;
      this->numChildren = 0;
      this->name = NULL;
      this->type = DT_VOID;
      return this;
}

tree* maketreeWithVal(int kind, int val) {
    tree* this = (tree*)malloc(sizeof(struct treenode));
    if (!this) return NULL;
    
    // Initialize the node
    this->numChildren = 0;
    this->val = val;
    this->name = NULL;
    this->type = DT_VOID;

    // Map token values to node kinds directly
    switch(kind) {
        case 289:  // Actual value of INTCONST
            this->nodeKind = INTEGER;
            break;
        case 290:  // Actual value of value of CHARCONST
            this->nodeKind = CHAR;
            break;
        default:
            this->nodeKind = kind;
    }
    
    return this;
}

void addChild(tree *parent, tree *child) {
      if (parent->numChildren == MAXCHILDREN) {
          printf("Cannot add child to parent node\n");
          exit(1);
      }
      nextAvailChild(parent) = child;
      parent->numChildren++;
}

void printAst(tree *node, int nestLevel) {
      char* nodeName = nodeNames[node->nodeKind];
      if(strcmp(nodeName,"identifier") == 0){
          if(node->val == -1)
              printf("%s,%s\n", nodeName,"undeclared variable");
          else
              printf("%s,%s\n", nodeName,node->name);
      }
      else if(strcmp(nodeName,"integer") == 0){
          printf("%s,%d\n", nodeName,node->val);
      }
      else if(strcmp(nodeName,"char") == 0){
          printf("%s,%c\n", nodeName,node->val);
      }
      else if(strcmp(nodeName,"typeSpecifier") == 0){
          printf("%s,%s\n", nodeName,typeNames[node->val]);
      }
      else if(strcmp(nodeName,"relop") == 0 || strcmp(nodeName,"mulop") == 0 || strcmp(nodeName,"addop") == 0){
          printf("%s,%s\n", nodeName,ops[node->val]);
      }
      else{
          printf("%s\n", nodeName);
      }

      int i, j;

      for (i = 0; i < node->numChildren; i++)  {
          for (j = 0; j < nestLevel; j++)
              printf("    ");
          printAst(getChild(node, i), nestLevel + 1);
      }

}

void analyzeProgram(tree *root) {
    if (!root) return;
    
    // Analyze each child (should be function declarations and global variables)
    for (int i = 0; i < root->numChildren; i++) {
        tree *child = root->children[i];
        switch (child->nodeKind) {
            case FUNDECL:
                analyzeFunctionDecl(child);
                break;
            case VARDECL:
                analyzeVarDecl(child);
                break;
            default:
                fprintf(stderr, "Error: Unexpected node kind at global scope\n");
        }
    }
}

void analyzeFunctionDecl(tree *node) {
    if (!node || node->nodeKind != FUNDECL) return;
    
    // Get function name and type from the FUNCTYPENAME child
    tree *funcTypeName = node->children[0];
    tree *typeSpec = funcTypeName->children[0];
    tree *id = funcTypeName->children[1];
    
    // Check if this is a function definition (has a function body)
    if (node->numChildren >= 3 && node->children[2] && node->children[2]->nodeKind == FUNBODY) {
        //printf("DEBUG: Found function definition for '%s'\n", id->name);
        
        // Only add to symbol table if not already there
        symEntry* existing = ST_lookup(id->name);
        if (!existing) {
            ST_insert(id->name, typeSpec->val, ST_FUNC);
        } else {
            // Function already exists
            semanticError("Function already defined", yylineno);
            return;
        }
    }
    
    // Create new scope for parameters and body
    new_scope();
    
    // Analyze parameters (formalDeclList is second child)
    tree *params = node->children[1];
    for (int i = 0; i < params->numChildren; i++) {
        analyzeNode(params->children[i]);
    }
    
    // Analyze function body (third child)
    if (node->numChildren >= 3) {
        analyzeNode(node->children[2]);
    }
    
    // Return to parent scope
    up_scope();
}

void analyzeVarDecl(tree *node) {
    if (!node || node->nodeKind != VARDECL) return;
    
    // Get type and identifier nodes
    tree *typeSpec = node->children[0];  // Type node
    tree *id = node->children[1];        // Identifier node
    
    int scope;
    // For regular variables
    if (id->nodeKind == IDENTIFIER) {
        if (ST_lookup(id->name)) {  // Correct, already using name
            semanticError("Variable already declared", -1);
            return;
        }
        ST_insert(id->name,      // Correct, already using name
                 typeSpec->val,     // type from typeSpec node's val
                 ST_SCALAR);      // Using our new enum name for scalar type
    }
    // For arrays
    else if (id->nodeKind == ARRAYDECL) {
        if (ST_lookup(id->name)) {
            semanticError("Array already declared", -1);
            return;
        }
        ST_insert(id->name,      // array name from strval
                 typeSpec->val,     // type from typeSpec node's val
                 ST_ARRAY);        // Using enum name for array type
    }
}

void semanticError(const char *message, int lineNo) {
    if (lineNo > 0) {
        fprintf(stderr, "Semantic error at line %d: %s\n", lineNo, message);
    } else {
        fprintf(stderr, "Semantic error: %s\n", message);
    }
}

void analyzeNode(tree *node) {
    if (!node) return;
    
    switch (node->nodeKind) {
        case VARDECL:
            analyzeVarDecl(node);
            break;
        case FUNDECL:
            analyzeFunctionDecl(node);
            break;
        default:
            // Recursively analyze children
            for (int i = 0; i < node->numChildren; i++) {
                analyzeNode(node->children[i]);
            }
    }
}

enum dataType getExpressionType(tree* node) {
    if (!node) return DT_VOID;
    
    switch (node->nodeKind) {
        case INTEGER:
            return DT_INT;
            
        case CHAR:
            return DT_CHAR;
            
        case IDENTIFIER: {
            symEntry* entry = ST_lookup(node->name);
            if (!entry) {
                add_semantic_error(yylineno, "Undeclared variable");
                return DT_VOID;
            }
            return entry->data_type;
        }
            
        case VAR: {
            if (node->numChildren > 0) {
                tree* id_node = node->children[0];
                if (id_node && id_node->nodeKind == IDENTIFIER) {
                    symEntry* entry = ST_lookup(id_node->name);
                    if (!entry) {
                        add_semantic_error(yylineno, "Undeclared variable");
                        return DT_VOID;
                    }
                    return entry->data_type;
                }
            }
            return DT_VOID;
        }
            
        case ADDOP:
        case MULOP: {
            enum dataType left = getExpressionType(node->children[0]);
            enum dataType right = getExpressionType(node->children[1]);
            
            // If either operand is void, operation is invalid
            if (left == DT_VOID || right == DT_VOID) {
                return DT_VOID;
            }
            
            // If either operand is int, result is int
            if (left == DT_INT || right == DT_INT) {
                return DT_INT;
            }
            
            // Both must be char at this point
            return DT_CHAR;
        }
            
        case EXPRESSION:
        case FACTOR:
        case TERM:
        case ADDEXPR: {
            // Pass through the type of the first child
            if (node->numChildren > 0) {
                return getExpressionType(node->children[0]);
            }
            return DT_VOID;
        }
            
        case FUNCCALLEXPR: {
            if (node->numChildren > 0) {
                tree* func_id = node->children[0];
                if (func_id && func_id->nodeKind == IDENTIFIER) {
                    // Special case for main
                    if (strcmp(func_id->name, "main") == 0) {
                        return DT_INT;  // main always returns int
                    }
                    // Regular function lookup
                    symEntry* entry = ST_lookup(func_id->name);
                    return entry ? entry->return_type : DT_VOID;
                }
            }
            return DT_VOID;
        }
            
        default:
            return DT_VOID;
    }
}

void setName(tree *node, char *name) {
    if (node->name) {
        free(node->name);  // Free any existing name
    }
    node->name = strdup(name);  // Make a copy of the name
}

// Helper function to handle binary operation type checking
enum dataType getBinaryOpType(tree *left, tree *right, int nodeKind) {
    enum dataType left_type = getExpressionType(left);
    enum dataType right_type = getExpressionType(right);
    
    // Both operands must be numeric (int or char)
    if (left_type == DT_VOID || right_type == DT_VOID) {
        return DT_VOID;  // Use as error indicator
    }
    
    // For arithmetic operations
    if (nodeKind == ADDOP || nodeKind == MULOP) {
        if ((left_type == DT_INT || left_type == DT_CHAR) && 
            (right_type == DT_INT || right_type == DT_CHAR)) {
            return DT_INT;  // Promote char to int in arithmetic
        }
        return DT_VOID;  // Error indicator
    }
    
    // For relational operations
    if (nodeKind == RELOP) {
        if ((left_type == DT_INT || left_type == DT_CHAR) && 
            (right_type == DT_INT || right_type == DT_CHAR)) {
            return DT_INT;  // Relational ops return int (0 or 1)
        }
        return DT_VOID;  // Error indicator
    }
    
    return DT_VOID;  // Error indicator
}
