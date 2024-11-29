%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tree.h"
#include "strtab.h"

extern int yylineno;
extern tree* getCurrentFunction(void);
extern void setCurrentFunction(tree* func);
extern tree* ast;
extern SemanticError semantic_errors[];
extern int error_count;
extern struct table_node* root;
extern struct table_node* current_scope;

int yyerror(char *s);
int yywarning(char *s);
void yyfinish(void);
int yyparse(void);
int yylex(void);

char* scope = "";
int had_syntax_error = 0;
static int suppress_error = 0;

#ifdef DEBUG
#define DEBUG_PRINT(fmt, ...) \
    fprintf(stderr, "DEBUG: " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...)
#endif

#define YYERROR_VERBOSE 1

static int parse_complete = 0;
%}

%union {
    int value;                  // For integer constants
    char *strval;              // For string values and identifiers
    struct treenode *node;     // For AST nodes
    struct symEntry *entry;     
}

%type <node> program declList decl varDecl funDecl typeSpecifier
%type <node> formalDeclList formalDecl funBody localDeclList
%type <node> statementList statement compoundStmt assignStmt
%type <node> condStmt loopStmt returnStmt expression simpleExpression
%type <node> var relop addExpr addop term mulop factor funcCallExpr argList

%token <strval> ID
%token <value> INTCONST CHARCONST
%token <strval> STRCONST
%token KWD_INT KWD_CHAR KWD_VOID
%token KWD_IF KWD_ELSE KWD_WHILE KWD_RETURN
%token OPER_ADD OPER_SUB OPER_MUL OPER_DIV
%token OPER_LT OPER_LTE OPER_GT OPER_GTE OPER_EQ OPER_NEQ OPER_ASGN
%token LSQ_BRKT RSQ_BRKT LCRLY_BRKT RCRLY_BRKT LPAREN RPAREN
%token COMMA SEMICLN
%token ERROR ILLEGAL_TOKEN
%token INTEGER
%token <value> INTCONST CHARCONST

%left OPER_ADD OPER_SUB
%left OPER_MUL OPER_DIV
%nonassoc OPER_LT OPER_LTE OPER_GT OPER_GTE OPER_EQ OPER_NEQ
%nonassoc LOWER_THAN_ELSE
%nonassoc KWD_ELSE

%start program

%%

// The root of the program
// This rule creates the top-level node for the entire program
program         : declList
                {
                    $$ = maketree(PROGRAM);
                    addChild($$, $1);
                    ast = $$;
                    print_semantic_errors();
                }
                ;

// List of declarations (variables and functions)
// This rule handles multiple declarations in the program
declList        : decl
                {
                    // For a single declaration, create a new DECLLIST node
                    $$ = maketree(DECLLIST);
                    addChild($$, $1);
                }
                | declList decl
                {
                    // For multiple declarations, create a new DECLLIST node
                    // and add both the previous list and the new declaration
                    tree *newDeclList = maketree(DECLLIST);
                    addChild(newDeclList, $1);  // Add the previous declList as a child
                    addChild(newDeclList, $2);  // Add the new decl as a child
                    $$ = newDeclList;
                }
                ;

decl            : varDecl
                {
                    // Wrap variable declaration in a DECL node
                    $$ = maketree(DECL);
                    addChild($$, $1);
                }
                | funDecl
                {
                    // Wrap function declaration in a DECL node
                    $$ = maketree(DECL);
                    addChild($$, $1);
                }
                ;

// Variable declaration
// Handles both simple variables and array declarations
varDecl         : typeSpecifier ID SEMICLN
                {
                    //printf("DEBUG: varDecl - Processing scalar '%s'\n", $2);
                    //printf("DEBUG: varDecl - Current scope: %p (root: %p)\n", 
                    //       (void*)current_scope, (void*)root);
                    
                    $$ = maketree(VARDECL);
                    addChild($$, $1);
                    tree *id = maketree(IDENTIFIER);
                    setName(id, $2);
                    addChild($$, id);
                    
                    // Add variable to symbol table
                    symEntry* entry = ST_insert($2, $1->type, ST_SCALAR);
                    if (!entry) {
                        add_semantic_error(yylineno, "Symbol declared multiple times.");
                    }
                    //printf("DEBUG: varDecl - After insert for '%s'\n", $2);
                }
                | typeSpecifier ID LSQ_BRKT INTCONST RSQ_BRKT SEMICLN
                {
                    //printf("DEBUG: varDecl - Processing array '%s[%d]'\n", $2, $4);
                    //printf("DEBUG: varDecl - Current scope: %p (root: %p)\n", 
                    //       (void*)current_scope, (void*)root);
                    
                    $$ = maketree(VARDECL);
                    addChild($$, $1);
                    tree *id = maketree(ARRAYDECL);
                    setName(id, $2);
                    addChild($$, id);
                    
                    // Add array to symbol table
                    symEntry* entry = ST_insert($2, $1->type, ST_ARRAY);
                    if (!entry) {
                        add_semantic_error(yylineno, "Symbol declared multiple times.");
                    } else {
                        entry->array_size = $4;
                        validate_array_declaration($4, yylineno);
                    }
                    //printf("DEBUG: varDecl - After insert for array '%s'\n", $2);
                }
                ;

// Type specifier (int, char, void)
// Creates a node for the type of a variable or function
typeSpecifier   : KWD_INT
                {
                    $$ = maketree(TYPESPEC);
                    $$->type = DT_INT;
                    $$->val = DT_INT;
                }
                | KWD_CHAR
                {
                    $$ = maketree(TYPESPEC);
                    $$->type = DT_CHAR;
                    $$->val = DT_CHAR;
                }
                | KWD_VOID
                {
                    $$ = maketree(TYPESPEC);
                    $$->type = DT_VOID;
                    $$->val = DT_VOID;
                }
                ;

// Function declaration
funDecl         : typeSpecifier ID LPAREN formalDeclList RPAREN funBody
                {
                    $$ = maketree(FUNDECL);
                    addChild($$, $1);  // Add type specifier
                    
                    // Create and add identifier node
                    tree* id = maketree(IDENTIFIER);
                    setName(id, $2);
                    addChild($$, id);
                    
                    // Add parameter list and function body
                    if ($4) addChild($$, $4);  // formalDeclList
                    if ($6) addChild($$, $6);  // funBody
                    
                    // Symbol table handling
                    symEntry* entry = ST_insert($2, $1->type, ST_FUNC);
                    if (!entry) {
                        add_semantic_error(yylineno, "Function redefinition.");
                    }
                }
                | typeSpecifier ID LPAREN RPAREN funBody
                {
                    $$ = maketree(FUNDECL);
                    addChild($$, $1);  // Add type specifier
                    
                    // Create and add identifier node
                    tree* id = maketree(IDENTIFIER);
                    setName(id, $2);
                    addChild($$, id);
                    
                    // Add empty parameter list and function body
                    tree* emptyParams = maketree(FORMALDECLLIST);
                    addChild($$, emptyParams);
                    if ($5) addChild($$, $5);  // funBody
                    
                    // Symbol table handling
                    symEntry* entry = ST_insert($2, $1->type, ST_FUNC);
                    if (!entry) {
                        add_semantic_error(yylineno, "Function redefinition.");
                    }
                }
                ;

formalDeclList  : /* empty */
                {
                    $$ = maketree(FORMALDECLLIST);
                }
                | formalDecl
                {
                    $$ = maketree(FORMALDECLLIST);
                    addChild($$, $1);
                }
                | formalDeclList COMMA formalDecl
                {
                    $$ = $1;
                    addChild($$, $3);
                }
                ;

formalDecl      : typeSpecifier ID
                {
                    $$ = maketree(FORMALDECL);
                    addChild($$, $1);
                    tree *id = maketree(IDENTIFIER);
                    setName(id, $2);
                    addChild($$, id);
                    
                    // Add parameter to current (function) scope
                    symEntry* entry = ST_insert($2, $1->type, ST_SCALAR);
                    if (!entry) {
                        add_semantic_error(yylineno, "Parameter already declared.");
                    }
                    add_param($2, $1->type, ST_SCALAR);
                }
                | typeSpecifier ID LSQ_BRKT RSQ_BRKT
                {
                    $$ = maketree(FORMALDECL);
                    addChild($$, $1);
                    tree *id = maketree(ARRAYDECL);
                    setName(id, $2);
                    addChild($$, id);
                    
                    // Add array parameter to current (function) scope
                    symEntry* entry = ST_insert($2, $1->type, ST_ARRAY);
                    if (!entry) {
                        add_semantic_error(yylineno, "Parameter already declared.");
                    }
                    add_param($2, $1->type, ST_ARRAY);
                }
                ;

// Function body
// Contains local declarations and statements
funBody         : LCRLY_BRKT localDeclList statementList RCRLY_BRKT
                {
                    $$ = maketree(FUNBODY);
                    if ($2) addChild($$, $2);  // Always add localDeclList
                    if ($3) addChild($$, $3);  // Always add statementList
                }
                ;

localDeclList   : /* empty */
                {
                    //printf("DEBUG: Creating empty localDeclList\n");
                    $$ = maketree(LOCALDECLLIST);
                }
                | localDeclList varDecl
                {
                    //printf("DEBUG: Adding varDecl to localDeclList\n");
                    if ($1 == NULL) {
                        $$ = maketree(LOCALDECLLIST);
                    } else {
                        $$ = $1;
                    }
                    addChild($$, $2);
                }
                ;

// List of statements
// Handles multiple statements in a block
statementList   : statement
                {
                    $$ = maketree(STATEMENTLIST);
                    addChild($$, $1);
                }
                | statementList statement
                {
                    $$ = $1;  // Use existing STATEMENTLIST node
                    addChild($$, $2);  // Add new statement
                }
                ;

// Different types of statements
// This rule categorizes various statement types
statement       : assignStmt
                | compoundStmt
                | condStmt
                | loopStmt
                | returnStmt
                ;

compoundStmt    : LCRLY_BRKT statementList RCRLY_BRKT
                {
                    $$ = $2;
                }
                ;

// Assignment statement
// Handles both variable assignment and expression statements
assignStmt      : var OPER_ASGN expression SEMICLN
                {
                    $$ = maketree(ASSIGNSTMT);
                    addChild($$, $1);
                    addChild($$, $3);
                    
                    enum dataType lhs_type = getExpressionType($1);
                    enum dataType rhs_type = getExpressionType($3);
                    
                    // Case 1: void assignments
                    if (lhs_type == DT_VOID) {
                        // void variables can only be assigned void expressions
                        if (rhs_type != DT_VOID) {
                            add_semantic_error(yylineno, "Type mismatch in assignment.");
                        }
                    }
                    // Case 2: char assignments
                    else if (lhs_type == DT_CHAR) {
                        // char variables can only be assigned char expressions
                        if (rhs_type != DT_CHAR) {
                            add_semantic_error(yylineno, "Type mismatch in assignment.");
                        }
                    }
                    // Case 3: int assignments
                    else if (lhs_type == DT_INT) {
                        // int variables can be assigned int or char (implicit promotion)
                        if (rhs_type != DT_INT && rhs_type != DT_CHAR) {
                            add_semantic_error(yylineno, "Type mismatch in assignment.");
                        }
                    }
                }
                | expression SEMICLN
                {
                    $$ = maketree(STATEMENT);
                    addChild($$, $1);
                    getExpressionType($1);
                }
                ;

// Conditional statement (if-else)
// Handles both if and if-else constructs
condStmt        : KWD_IF LPAREN expression RPAREN statement %prec LOWER_THAN_ELSE
                {
                    // If statement without else
                    $$ = maketree(CONDSTMT);
                    addChild($$, $3);  // Condition
                    addChild($$, $5);  // If-body
                }
                | KWD_IF LPAREN expression RPAREN statement KWD_ELSE statement
                {
                    // If-else statement
                    $$ = maketree(CONDSTMT);
                    addChild($$, $3);  // Condition
                    addChild($$, $5);  // If-body
                    addChild($$, $7);  // Else-body
                }
                ;

// Loop statement (while)
loopStmt        : KWD_WHILE LPAREN expression RPAREN statement
                {
                    $$ = maketree(LOOPSTMT);
                    addChild($$, $3);  // Loop condition
                    addChild($$, $5);  // Loop body
                }
                ;

// Return statement
returnStmt      : KWD_RETURN expression SEMICLN
                {
                    $$ = maketree(RETURNSTMT);
                    addChild($$, $2);
                    
                    // Get current function's return type
                    tree* current_func = getCurrentFunction();
                    if (current_func) {
                        if (current_func->type == DT_VOID) {
                            yyerror("Void function cannot return a value");
                        } else if ($2->type != current_func->type) {
                            yyerror("Return type mismatch");
                        }
                    }
                }
                | KWD_RETURN SEMICLN
                {
                    $$ = maketree(RETURNSTMT);
                    
                    // Check if void return is allowed
                    tree* current_func = getCurrentFunction();
                    if (current_func && current_func->type != DT_VOID) {
                        yyerror("Non-void function must return a value");
                    }
                }
                ;

var             : ID LSQ_BRKT expression RSQ_BRKT
                {
                    $$ = maketree(VAR);
                    tree *id = maketree(IDENTIFIER);
                    setName(id, $1);
                    addChild($$, id);
                    addChild($$, $3);
                    
                    // Add comprehensive array access validation
                    symEntry* entry = ST_lookup($1);
                    if (entry) {
                        check_array_access(entry, $3, yylineno);
                    } else {
                        add_semantic_error(yylineno, "Undeclared array variable");
                    }
                }
                | ID
                {
                    $$ = maketree(VAR);
                    tree *id = maketree(IDENTIFIER);
                    setName(id, $1);
                    addChild($$, id);
                }
                ;

// Expression (including relational operations)
// Handles both simple expressions and relational expressions
expression      : simpleExpression
                {
                    $<node>$ = maketree(EXPRESSION);
                    addChild($<node>$, $<node>1);
                }
                ;

simpleExpression: addExpr
                {
                    $<node>$ = $<node>1;
                }
                | addExpr relop addExpr
                {
                    $<node>$ = $<node>2;
                    addChild($<node>$, $<node>1);
                    addChild($<node>$, $<node>3);
                }
                ;

addExpr         : term
                {
                    $$ = $1;
                }
                | addExpr addop term
                {
                    $$ = $2;  // Use addop as the root
                    addChild($$, $1);
                    addChild($$, $3);
                }
                ;

term            : factor
                {
                    $$ = $1;
                }
                | term mulop factor
                {
                    $$ = $2;  // Use mulop as the root
                    addChild($$, $1);
                    addChild($$, $3);
                }
                ;

factor          : LPAREN expression RPAREN
                {
                    $$ = $2;  // Just pass through the expression
                }
                | var
                {
                    $$ = maketree(FACTOR);
                    addChild($$, $1);
                }
                | funcCallExpr
                {
                    $$ = maketree(FACTOR);
                    addChild($$, $1);
                }
                | INTCONST
                {
                    $$ = maketreeWithVal(INTEGER, $1);
                }
                | CHARCONST
                {
                    $$ = maketreeWithVal(CHAR, $1);
                }
                ;

relop           : OPER_LTE
                {
                    $<node>$ = maketree(RELOP);
                    $<node>$->val = 0;
                }
                | OPER_LT
                {
                    $$ = maketree(RELOP);
                    $$->val = 1;  // Add operator value
                }
                | OPER_GT
                {
                    $$ = maketree(RELOP);
                    $$->val = 2;  // Add operator value
                }
                | OPER_GTE
                {
                    $$ = maketree(RELOP);
                    $$->val = 3;  // Add operator value
                }
                | OPER_EQ
                {
                    $$ = maketree(RELOP);
                    $$->val = 4;  // Add operator value
                }
                | OPER_NEQ
                {
                    $$ = maketree(RELOP);
                    $$->val = 5;  // Add operator value
                }
                ;

addop           : OPER_ADD
                {
                    $$ = maketree(ADDOP);
                }
                | OPER_SUB
                {
                    $$ = maketree(ADDOP);
                }
                ;

mulop           : OPER_MUL
                {
                    $$ = maketree(MULOP);
                }
                | OPER_DIV
                {
                    $$ = maketree(MULOP);
                }
                ;

funcCallExpr    : ID LPAREN argList RPAREN
                {
                    $$ = maketree(FUNCCALLEXPR);
                    tree *id = maketree(IDENTIFIER);
                    setName(id, $1);
                    addChild($$, id);
                    addChild($$, $3);
                    
                    // Add comprehensive function call validation
                    check_function_call($1, $3, yylineno);
                }
                | ID LPAREN RPAREN
                {
                    $$ = maketree(FUNCCALLEXPR);
                    tree *id = maketree(IDENTIFIER);
                    setName(id, $1);
                    addChild($$, id);
                    tree* empty_args = maketree(ARGLIST);
                    addChild($$, empty_args);
                    check_function_call($1, empty_args, yylineno);
                }
                ;

argList         : expression
                {
                    $$ = maketree(ARGLIST);
                    addChild($$, $1);
                }
                | argList COMMA expression
                {
                    $$ = $1;  // Use the existing ARGLIST node
                    addChild($$, $3);  // Add the new expression to it
                }
                ;

%%

int yyerror(char * msg) {
    //printf("DEBUG: yyerror called with message: %s at line %d\n", msg, yylineno);
    static int last_error_line = -1;
    if (yylineno != last_error_line && strstr(msg, "syntax error")) {
        int has_semantic_error = 0;
        for (int i = 0; i < error_count; i++) {
            if (semantic_errors[i].line == yylineno) {
                has_semantic_error = 1;
                break;
            }
        }
        if (!has_semantic_error) {
            printf("error: line %d: %s\n", yylineno, msg);
        }
        last_error_line = yylineno;
    }
    return 1;
}