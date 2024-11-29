#ifndef STRTAB_H
#define STRTAB_H

#include <stdbool.h>

// Forward declarations
struct treenode;
typedef struct treenode tree;

// Data types for variables and functions
typedef enum dataType {
    DT_INT,     // Instead of INT_TYPE
    DT_CHAR,    // Instead of CHAR_TYPE
    DT_VOID,    // Instead of VOID_TYPE
    DT_ARRAY,   // Instead of ARRAY_TYPE
    DT_FUNC     // Instead of FUNCTION_TYPE
} dataType;

#define MAXIDS 1000
#define GLOBAL_SCOPE 0
#define LOCAL_SCOPE 1
#define MAX_ERRORS 100
#define MAX_ERROR_LENGTH 256

// Symbol types for symbol table entries
typedef enum symbolType {
    ST_SCALAR,  // Instead of SCALAR_TYPE
    ST_ARRAY,   // Instead of ARRAY_TYPE
    ST_FUNC     // Instead of FUNCTION_TYPE
} symbolType;

// Parameter list structure
typedef struct param {
    char* name;
    dataType data_type;
    enum symbolType symbol_type;
    struct param *next;
} param;

// Symbol table entry structure
typedef struct symEntry {
    char *id;                  // Identifier name
    dataType data_type;
    enum symbolType sym_type;
    int scope;                 // GLOBAL_SCOPE or LOCAL_SCOPE
    
    // For arrays
    int array_size;           // Size if array type
    
    // For functions
    dataType return_type;
    int num_params;            // Number of parameters
    param *params;             // List of parameter types
    
    // For scope tracking
    struct symEntry *parent_function;  // Function that contains this symbol (NULL if global)
    
    struct symEntry *next;     // For hash table collision handling
} symEntry;

// Symbol table node (for scope management)
typedef struct table_node {
    symEntry *strTable[MAXIDS];
    int numChildren;
    struct table_node *parent;
    struct table_node *first_child;
    struct table_node *last_child;
    struct table_node *next;
} table_node;

// Function declarations
symEntry* ST_insert(char *id, dataType d_type, enum symbolType s_type);
symEntry* ST_lookup(char *id);
symEntry* ST_lookup_in_scope(char* id, table_node* scope);
void ST_set_function_info(symEntry *entry, dataType ret_type, param *params, int num_params);
void add_param(char* name, dataType type, enum symbolType sym_type);
param* get_param_list(void);
void clear_param_list(void);
void new_scope(void);
void up_scope(void);
void print_sym_tab(void);
int ST_get_info(char *id, dataType *type, enum symbolType *symbol_type, int *scope);
int get_param_count(char *func_id);
void init_symbol_table(void);
void add_semantic_error(int line, const char* message);
void print_semantic_errors(void);
void check_array_access(symEntry* entry, tree* index_expr, int line);
void check_function_call(char* func_name, tree* args, int line);
void validate_array_index(tree* index_expr, int line);
void validate_array_declaration(int size, int line);
param* get_param_list(void);
int count_params(param* params);
void ST_install_func(char* name, enum dataType type, param* params, int num_params, int line);
void end_scope(void);

// Define SemanticError type
typedef struct {
    int line;
    char message[MAX_ERROR_LENGTH];
} SemanticError;

// Declare externals
extern table_node* root;
extern table_node* current_scope;
extern SemanticError semantic_errors[MAX_ERRORS];
extern int error_count;

#endif
