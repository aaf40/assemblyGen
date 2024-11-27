#ifndef TREE_H
#define TREE_H

#include "strtab.h"

#define MAXCHILDREN 100

// Forward declaration
typedef struct treenode tree;

// Node kinds for AST
typedef enum {
    PROGRAM,
    DECLLIST,
    DECL,
    VARDECL,
    TYPESPEC,
    FUNDECL,
    FORMALDECLLIST,
    FORMALDECL,
    FUNBODY,
    LOCALDECLLIST,
    STATEMENTLIST,
    STATEMENT,
    COMPOUNDSTMT,
    ASSIGNSTMT,
    CONDSTMT,
    LOOPSTMT,
    RETURNSTMT,
    EXPRESSION,
    RELOP,
    ADDEXPR,
    ADDOP,
    TERM,
    MULOP,
    FACTOR,
    FUNCCALLEXPR,
    ARGLIST,
    INTEGER,
    IDENTIFIER,
    VAR,
    ARRAYDECL,
    CHAR,
    FUNCTYPENAME
} NodeKind;

// Tree node structure
struct treenode {
    NodeKind nodeKind;
    int numChildren;
    struct treenode *children[MAXCHILDREN];
    int val;
    char *name;
    dataType type;
};

// Function declarations
tree* maketree(int kind);
tree* maketreeWithVal(int kind, int val);
void addChild(tree* parent, tree* child);
void printAst(tree* node, int nestLevel);
void setName(tree* node, char* name);
enum dataType getExpressionType(tree* node);
void analyzeProgram(tree* node);
void analyzeFunctionDecl(tree* node);
void analyzeVarDecl(tree* node);
void analyzeNode(tree* node);
void semanticError(const char* message, int lineNo);
tree* getCurrentFunction(void);
void setCurrentFunction(tree* func);

#define nextAvailChild(node) node->children[node->numChildren]
#define getChild(node, index) node->children[index]

extern tree* ast;

#endif
