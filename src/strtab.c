#include "strtab.h"
#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Add function prototype before it's used
void print_entry(symEntry *entry);

// Global variables for symbol table management
table_node* root = NULL;
table_node* current_scope = NULL;
static param* working_list_head = NULL;
static param* working_list_tail = NULL;
tree* current_tree = NULL;

// Define the global variables (not the types, which are in the header)
SemanticError semantic_errors[MAX_ERRORS] = {0};
int error_count = 0;
static int errors_printed = 0;

// Hash function
static int hash(char *id) {
    unsigned int hash = 0;
    for (int i = 0; id[i] != '\0'; i++) {
        hash = 31 * hash + id[i];
    }
    return hash % MAXIDS;
}

// Count parameters in a parameter list
int count_params(param *params) {
    int count = 0;
    param *current = params;
    while (current) {
        count++;
        current = current->next;
    }
    return count;
}

void ST_set_function_info(symEntry *entry, enum dataType ret_type, param *params, int num_params) {
    if (!entry) return;
    
    //printf("DEBUG: Setting function info for %s\n", entry->id);
    //printf("DEBUG: Return type: %d, Num params: %d\n", ret_type, num_params);
    
    entry->return_type = ret_type;
    entry->params = params;
    entry->num_params = num_params;
    
    // Print parameter info
    param *p = params;
    int i = 0;
    while (p) {
        //printf("DEBUG: Parameter %d: name=%s, type=%d, symbol_type=%d\n",
        //       i++, p->name, p->data_type, p->symbol_type);
        p = p->next;
    }
}

symEntry* ST_insert(char* id, enum dataType d_type, enum symbolType s_type) {
    if (!current_scope) {
        fprintf(stderr, "Error: No current scope\n");
        return NULL;
    }
    
    // Only use root scope for functions and global variables
    table_node* target_scope = current_scope;
    bool will_be_global = false;

    // Check if we're in a function declaration context
    tree* current = current_tree;  // You'll need to add this as a global variable
    while (current) {
        if (current->nodeKind == FUNDECL) {
            // We're inside a function declaration
            will_be_global = false;
            target_scope = current_scope;
            break;
        }
        current = current->parent;
    }

    // If we're not in a function context, and it's a function declaration, use root scope
    if (!current && s_type == ST_FUNC) {
        will_be_global = true;
        target_scope = root;
    }
    
    //printf("DEBUG ST_insert - id=%s, current_scope=%p, root=%p\n", 
            //id, (void*)current_scope, (void*)root);
    //printf("DEBUG ST_insert - target_scope=%p, will_be_global=%d\n", 
            //(void*)target_scope, will_be_global);
    
    // Check if variable already exists in CURRENT scope only
    symEntry* existing = ST_lookup_in_scope(id, target_scope);
    if (existing) {
        //printf("DEBUG Symbol '%s' already exists in current scope\n", id);
        return existing;
    }
    
    // Create new entry
    symEntry* entry = malloc(sizeof(symEntry));
    if (!entry) return NULL;
    
    entry->id = strdup(id);
    entry->data_type = d_type;
    entry->sym_type = s_type;
    entry->scope = (target_scope == root) ? GLOBAL_SCOPE : LOCAL_SCOPE;
    
    // Set parent function for local variables
    if (target_scope != root) {
        // Find the enclosing function in the symbol table
        symEntry* main_entry = ST_lookup("main");  // Or current function
        if (main_entry) {
            entry->parent_function = main_entry;
            //printf("DEBUG Setting parent function for '%s' to '%s'\n", 
                    //id, main_entry->id);
        }
    } else {
        entry->parent_function = NULL;
    }
    
    // Add to scope's table
    int index = hash(id);
    entry->next = target_scope->strTable[index];
    target_scope->strTable[index] = entry;
    
    return entry;
}

param* get_param_list(void) {
    param* list = working_list_head;
    working_list_head = working_list_tail = NULL;
    return list;
}

void clear_param_list(void) {
    param* current = working_list_head;
    while (current) {
        param* next = current->next;
        free(current);
        current = next;
    }
    working_list_head = working_list_tail = NULL;
}

int check_param_compatibility(symEntry *func, param *call_params) {
    param *def_params = func->params;
    param *curr_call = call_params;
    int count = 0;
    
    while (def_params && curr_call) {
        if (def_params->data_type != curr_call->data_type ||
            def_params->symbol_type != curr_call->symbol_type) {
            return 0;  // Type mismatch
        }
        def_params = def_params->next;
        curr_call = curr_call->next;
        count++;
    }
    
    return (count == func->num_params);
}

int has_global_scope(tree* node) {
    if (!node || !node->name) return 0;
    
    symEntry* entry = ST_lookup(node->name);
    if (!entry) return 0;
    
    return (entry->scope == GLOBAL_SCOPE);
}

void new_scope(void) {
    //printf("DEBUG: Creating new scope (current: %p, root: %p)\n", 
    //       (void*)current_scope, (void*)root);
    
    table_node *new_node = (table_node *)malloc(sizeof(table_node));
    if (!new_node) {
        fprintf(stderr, "Error: Memory allocation failed for new scope\n");
        exit(1);
    }
    
    // Initialize the new scope
    memset(new_node->strTable, 0, sizeof(new_node->strTable));
    new_node->numChildren = 0;
    new_node->parent = current_scope;
    new_node->first_child = NULL;
    new_node->last_child = NULL;
    new_node->next = NULL;
    
    // If this is the first scope (root)
    if (!root) {
        root = new_node;
        current_scope = root;
        return;
    }
    
    // Add as child to current scope
    if (!current_scope->first_child) {
        current_scope->first_child = new_node;
    } else {
        current_scope->last_child->next = new_node;
    }
    current_scope->last_child = new_node;
    current_scope->numChildren++;
    
    // Make this the current scope
    current_scope = new_node;
    
    //printf("DEBUG: New scope created at %p\n", (void*)current_scope);
}

void up_scope(void) {
    //printf("DEBUG: up_scope - Moving up from scope %p\n", (void*)current_scope);
    if (current_scope && current_scope->parent) {
        current_scope = current_scope->parent;
        //printf("DEBUG: up_scope - New current scope: %p\n", (void*)current_scope);
    }
}

void end_scope(void) {
    if (!current_scope) {
        fprintf(stderr, "Error: No current scope to end\n");
        return;
    }
    
    if (current_scope == root) {
        fprintf(stderr, "Warning: Attempting to end root scope\n");
        return;
    }
    
    // Move back to parent scope
    current_scope = current_scope->parent;
}

// Helper function to traverse scopes
void print_scope_entries(table_node* scope) {
    if (!scope) return;
    
    for (int i = 0; i < MAXIDS; i++) {
        symEntry *entry = scope->strTable[i];
        while (entry) {
            print_entry(entry);
            entry = entry->next;
        }
    }
}

// Helper function before print_sym_tab
static void print_scope_tree(table_node* scope) {
    if (!scope) return;
    
    // Print entries in current scope
    print_scope_entries(scope);
    
    // Print entries in first child
    if (scope->first_child) {
        print_scope_tree(scope->first_child);
    }
    
    // Print entries in next sibling
    if (scope->next) {
        print_scope_tree(scope->next);
    }
}

void print_sym_tab(void) {
    if (!root) {
        printf("Symbol table is empty\n");
        return;
    }
    
    printf("\nSymbol Table Contents:\n");
    printf("=====================\n");
    
    // Print global entries
    printf("Global Scope:\n");
    printf("-------------\n");
    for (int i = 0; i < MAXIDS; i++) {
        symEntry *entry = root->strTable[i];
        while (entry) {
            if (entry->scope == GLOBAL_SCOPE) {
                print_entry(entry);
            }
            entry = entry->next;
        }
    }
    
    // Print local entries from all scopes
    printf("\nLocal Scope:\n");
    printf("------------\n");
    
    // Start printing from root's first child
    if (root->first_child) {
        print_scope_tree(root->first_child);
    }
    
    printf("=====================\n\n");
}

// Helper function to print a single symbol table entry
void print_entry(symEntry *entry) {
    if (!entry) return;
    
    printf("%s: ", entry->id);
    
    // Print symbol type
    switch(entry->sym_type) {
        case ST_SCALAR:
            printf("scalar ");
            break;
        case ST_ARRAY:
            printf("array[%d] ", entry->array_size);
            break;
        case ST_FUNC:
            printf("function ");
            break;
    }
    
    // Print data type
    switch(entry->data_type) {
        case DT_INT:  printf("int"); break;
        case DT_CHAR: printf("char"); break;
        case DT_VOID: printf("void"); break;
        default:      printf("unknown"); break;
    }
    printf("\n");
    
    // Print function parameters if applicable
    if (entry->sym_type == ST_FUNC && entry->params) {
        printf("    Parameters: ");
        param *p = entry->params;
        while (p) {
            switch(p->data_type) {
                case DT_INT:  printf("int"); break;
                case DT_CHAR: printf("char"); break;
                case DT_VOID: printf("void"); break;
                default:      printf("unknown"); break;
            }
            if (p->symbol_type == ST_ARRAY) printf("[]");
            p = p->next;
            if (p) printf(", ");
        }
        printf("\n");
    }
}

int ST_get_info(char *id, enum dataType *type, enum symbolType *symbol_type, int *scope) {
    symEntry *entry = ST_lookup(id);
    if (!entry) return 0;
    
    if (type) *type = entry->data_type;
    if (symbol_type) *symbol_type = entry->sym_type;
    if (scope) *scope = entry->scope;
    return 1;
}

int get_param_count(char *func_id) {
    symEntry *entry = ST_lookup(func_id);
    if (!entry || entry->sym_type != ST_FUNC) return 0;
    return entry->num_params;
}

symEntry* ST_lookup(char* id) {
    //printf("DEBUG: ST_lookup called for '%s'\n", id);
    
    // Start from current scope
    table_node* scope = current_scope;
    while (scope != NULL) {
        // Get hash value for id
        int index = hash(id);
        
        // Look for entry in current scope
        symEntry* entry = scope->strTable[index];
        while (entry != NULL) {
            if (strcmp(entry->id, id) == 0) {
                //printf("DEBUG: ST_lookup found '%s' in scope %p\n", id, (void*)scope);
                return entry;
            }
            entry = entry->next;
        }
        
        // Move up to parent scope
        scope = scope->parent;
    }
    
    //printf("DEBUG: ST_lookup did not find '%s'\n", id);
    return NULL;
}

__attribute__((constructor))
void init_symbol_table(void) {
    if (!root) {
        root = (table_node*)malloc(sizeof(table_node));
        if (!root) {
            fprintf(stderr, "Failed to allocate root symbol table\n");
            exit(1);
        }
        memset(root, 0, sizeof(table_node));
        current_scope = root;
    }
    
    // Pre-declare the output library function
    symEntry* output_func = ST_insert("output", DT_VOID, ST_FUNC);
    
    // Set up its parameter info
    param* p = malloc(sizeof(param));
    p->name = "x";
    p->data_type = DT_INT;
    p->symbol_type = ST_SCALAR;
    p->next = NULL;
    
    ST_set_function_info(output_func, DT_VOID, p, 1);
}

// Adds a semantic error to the error array with the given line number and message.
void add_semantic_error(int line, const char* message) {
    if (error_count < MAX_ERRORS) {
        semantic_errors[error_count].line = line;
        strncpy(semantic_errors[error_count].message, message, MAX_ERROR_LENGTH - 1);
        semantic_errors[error_count].message[MAX_ERROR_LENGTH - 1] = '\0';
        error_count++;
    }
}

// Validates array access by checking if the index expression is valid and within bounds
void print_semantic_errors(void) {
    // Sort errors by line number
    for (int i = 0; i < error_count - 1; i++) {
        for (int j = 0; j < error_count - i - 1; j++) {
            if (semantic_errors[j].line > semantic_errors[j + 1].line) {
                SemanticError temp = semantic_errors[j];
                semantic_errors[j] = semantic_errors[j + 1];
                semantic_errors[j + 1] = temp;
            }
        }
    }
    
    // Print sorted errors
    for (int i = 0; i < error_count; i++) {
        printf("error: line %d: %s\n", 
               semantic_errors[i].line, 
               semantic_errors[i].message);
    }
}

// Helper function to be called before yyparse returns
void init_error_handling(void) {
    atexit(print_semantic_errors);
}

static void check_expression_index(tree* node, symEntry* entry, int line) {
    // Check for non-integer operands first
    for (int i = 0; i < node->numChildren; i++) {
        tree* child = node->children[i];
        if (child->nodeKind == IDENTIFIER) {
            symEntry* id_entry = ST_lookup(child->name);
            if (id_entry && (id_entry->data_type == DT_CHAR || id_entry->data_type == DT_VOID)) {
                add_semantic_error(line, "Array indexed using non-integer expression.");
                return;
            }
        } else if (child->nodeKind == CHAR) {
            add_semantic_error(line, "Array indexed using non-integer expression.");
            return;
        }
    }
    
    // Check for constant expression bounds
    int is_constant = 1;
    int total = 0;
    
    for (int i = 0; i < node->numChildren; i++) {
        if (node->children[i]->nodeKind == INTEGER) {
            total += node->children[i]->val;
        } else {
            is_constant = 0;
            break;
        }
    }
    
    if (is_constant && total >= entry->array_size) {
        add_semantic_error(line, "Statically sized array indexed with constant, out-of-bounds expression.");
    }
}

// Helper function to check if an expression is an integer type
static int is_integer_expr(tree* node) {
    if (!node) return 0;
    
    switch (node->nodeKind) {
        case INTEGER:
        case 289:
            return 1;
            
        case IDENTIFIER: {
            symEntry* entry = ST_lookup(node->name);
            return (entry && entry->data_type == DT_INT);
        }
            
        case EXPRESSION:
        case ADDEXPR:
        case TERM:
        case FACTOR:
        case ADDOP:
        case MULOP:
            if (node->numChildren == 2) {
                return is_integer_expr(node->children[0]) && 
                       is_integer_expr(node->children[1]);
            } else if (node->numChildren == 1) {
                return is_integer_expr(node->children[0]);
            }
            return 0;
            
        case CHAR:
            return 0;
            
        default:
            return 0;
    }
}

// Simple constant expression evaluator - only called after type checking
static int evaluate_constant(tree* node) {
    if (!node) return 0;
    
    switch (node->nodeKind) {
        case INTEGER:
        case 289:
            return node->val;
            
        case EXPRESSION:
        case ADDEXPR:
        case TERM:
            if (node->numChildren == 2) {
                if (node->nodeKind == ADDEXPR) {
                    return evaluate_constant(node->children[0]) + evaluate_constant(node->children[1]);
                }
                if (node->nodeKind == TERM) {
                    return evaluate_constant(node->children[0]) * evaluate_constant(node->children[1]);
                }
            }
            if (node->numChildren > 0) {
                return evaluate_constant(node->children[0]);
            }
            return 0;
            
        case ADDOP:
            if (node->val == 0) // Addition
                return evaluate_constant(node->children[0]) + evaluate_constant(node->children[1]);
            else // Subtraction
                return evaluate_constant(node->children[0]) - evaluate_constant(node->children[1]);
            
        case MULOP:
            if (node->val == 0) // Multiplication
                return evaluate_constant(node->children[0]) * evaluate_constant(node->children[1]);
            else // Division
                return evaluate_constant(node->children[0]) / evaluate_constant(node->children[1]);
            
        default:
            return 0;
    }
}

// Helper to check if expression is a compile-time constant
static int is_constant_expr(tree* node) {
    if (!node) return 0;
    
    switch (node->nodeKind) {
        case INTEGER:
            return 1;
            
        case EXPRESSION:
        case ADDEXPR:
        case TERM:
        case FACTOR:
        case ADDOP:
        case MULOP:
            if (node->numChildren == 2) {
                return is_constant_expr(node->children[0]) && 
                       is_constant_expr(node->children[1]);
            } else if (node->numChildren == 1) {
                return is_constant_expr(node->children[0]);
            }
            return 0;
            
        default:
            return 0;
    }
}

// Define debug_print_tree first
void debug_print_tree(tree* node, int depth) {
    if (!node) return;
    
    for (int i = 0; i < depth; i++) printf("  ");
    printf("Node kind: %d", node->nodeKind);
    if (node->nodeKind == IDENTIFIER) printf(", name: %s", node->name);
    if (node->nodeKind == INTEGER) printf(", value: %d", node->val);
    printf("\n");
    
    for (int i = 0; i < node->numChildren; i++) {
        debug_print_tree(node->children[i], depth + 1);
    }
}

void check_array_access(symEntry* entry, struct treenode* index_expr, int line) {
    //printf("DEBUG: Checking array access on line %d\n", line);
    
    if (!entry || entry->sym_type != ST_ARRAY) {
        add_semantic_error(line, "Non-array identifier used as an array.");
        return;
    }

    if (!is_integer_expr(index_expr)) {
        add_semantic_error(line, "Array indexed using non-integer expression.");
        return;
    }

    //printf("DEBUG: Array size: %d\n", entry->array_size);
    if (entry->array_size > 0) {
        //printf("DEBUG: Checking if constant expression...\n");
        if (is_constant_expr(index_expr)) {
            int value = evaluate_constant(index_expr);
            //printf("DEBUG: Evaluated constant index: %d\n", value);
            if (value >= entry->array_size) {
                add_semantic_error(line, "Statically sized array indexed with constant, out-of-bounds expression.");
                return;
            }
        }
    }
}

// Update array declaration validation
void validate_array_declaration(int size, int line) {
    if (size == 0) {
        add_semantic_error(line, "Array variable declared with size of zero.");
    }
}

void ST_install_func(char* name, enum dataType type, param* params, int num_params, int line) {
    symEntry* entry = ST_lookup(name);
    //printf("DEBUG: ST_install_func - Processing '%s' (exists: %s)\n", 
    //       name, entry ? "yes" : "no");
    
    if (entry) {
        // Function already exists - error
        add_semantic_error(line, "Symbol declared multiple times.");
        return;
    }
    
    entry = ST_insert(name, type, ST_FUNC);
    if (entry) {
        entry->return_type = type;
        entry->params = params;
        entry->num_params = num_params;
    }
}

void check_function_call(char* func_name, tree* args, int line) {
    // Special case for main - always returns int and takes no arguments
    if (strcmp(func_name, "main") == 0) {
        if (args && args->numChildren > 0) {
            add_semantic_error(line, "Too many arguments provided in function call.");
        }
        return;  // Return immediately for main
    }

    symEntry* func_entry = ST_lookup(func_name);
    if (!func_entry) {
        
        add_semantic_error(line, "Undefined function");
        return;
    }

    int provided_args = args ? args->numChildren : 0;
    
    // Check argument counts
    if (provided_args < func_entry->num_params) {
        add_semantic_error(line, "Too few arguments provided in function call.");
        return;
    }
    if (provided_args > func_entry->num_params) {
        add_semantic_error(line, "Too many arguments provided in function call.");
        return;
    }

    // Check argument types
    param* param_ptr = func_entry->params;
    for (int i = 0; i < provided_args && param_ptr != NULL; i++) {
        tree* arg = args->children[i];
        //printf("DEBUG: Checking argument %d:\n", i);
        //printf("DEBUG: Full argument node path:\n");
        
        // Trace the full path to the variable
        tree* current = arg;
        while (current && current->numChildren > 0) {
            //printf("DEBUG: Node kind=%d\n", current->nodeKind);
            current = current->children[0];
        }
        
        // Get the symbol table entry for this argument
        symEntry* arg_entry = NULL;
        if (arg->nodeKind == 17) { // EXPRESSION
            if (arg->numChildren > 0 && arg->children[0]->nodeKind == 23) { // FACTOR
                if (arg->children[0]->numChildren > 0 && 
                    arg->children[0]->children[0]->nodeKind == 28) { // VAR
                    tree* var_node = arg->children[0]->children[0];
                    if (var_node->numChildren > 0) {
                        //printf("DEBUG: Looking up identifier: %s\n", var_node->children[0]->name);
                        arg_entry = ST_lookup(var_node->children[0]->name);
                    }
                }
            }
        }

        //if (arg_entry) {
            //printf("DEBUG: Found symbol entry: id=%s, type=%d, sym_type=%d\n", 
            //       arg_entry->id, arg_entry->data_type, arg_entry->sym_type);
        //}

        //printf("DEBUG: Parameter expects: type=%d, sym_type=%d\n", 
        //       param_ptr->data_type, param_ptr->symbol_type);
        
        // For array parameters
        if (param_ptr->symbol_type == ST_ARRAY) {
            // Must have a symbol table entry for arrays
            if (!arg_entry || arg_entry->sym_type != ST_ARRAY) {
                add_semantic_error(line, "Argument type mismatch in function call.");
                return;
            }
            // Check array element type matches
            if (param_ptr->data_type != arg_entry->data_type) {
                add_semantic_error(line, "Argument type mismatch in function call.");
                return;
            }
        }
        // For non-array parameters
        else {
            // If argument is an array but parameter isn't
            if (arg_entry && arg_entry->sym_type == ST_ARRAY) {
                add_semantic_error(line, "Argument type mismatch in function call.");
                return;
            }
            // Check types match (including void)
            dataType arg_type = arg_entry ? arg_entry->data_type : getExpressionType(arg);
            if (param_ptr->data_type != arg_type) {
                add_semantic_error(line, "Argument type mismatch in function call.");
                return;
            }
        }
        
        param_ptr = param_ptr->next;
    }
}

void validate_array_index(tree* index_expr, int line) {
    if (!is_integer_expr(index_expr)) {
        add_semantic_error(line, "Array index must be an integer expression");
        return;
    }
    
    if (is_constant_expr(index_expr)) {
        int value = evaluate_constant(index_expr);
        if (value < 0) {
            add_semantic_error(line, "Array index cannot be negative");
        }
    }
}

// Adds a parameter with the given name, data type and symbol type to the working parameter list
void add_param(char* name, enum dataType type, enum symbolType sym_type) {
    param* new_param = (param*)malloc(sizeof(param));
    if (!new_param) {
        fprintf(stderr, "Error: Memory allocation failed for parameter\n");
        return;
    }
    
    new_param->name = strdup(name);
    new_param->data_type = type;
    new_param->symbol_type = sym_type;
    new_param->next = NULL;
    
    // Add to working list
    if (!working_list_head) {
        working_list_head = new_param;
        working_list_tail = new_param;
    } else {
        working_list_tail->next = new_param;
        working_list_tail = new_param;
    }
}

// Helper function to verify scope state
void verify_scope_state(void) {
    if (!current_scope) {
        fprintf(stderr, "Error: No current scope\n");
        return;
    }
    
    if (!root) {
        fprintf(stderr, "Error: No root scope\n");
        return;
    }
    
    // Verify we can traverse up to root from current scope
    table_node *temp = current_scope;
    while (temp->parent) {
        temp = temp->parent;
    }
    
    if (temp != root) {
        fprintf(stderr, "Error: Current scope not properly connected to root\n");
    }
}

symEntry* ST_lookup_in_scope(char* id, table_node* scope) {
    if (!scope) return NULL;
    
    // Look for the entry in this specific scope
    for (int i = 0; i < MAXIDS; i++) {
        symEntry* entry = scope->strTable[i];
        while (entry) {
            if (strcmp(entry->id, id) == 0) {
                return entry;
            }
            entry = entry->next;
        }
    }
    return NULL;
}

