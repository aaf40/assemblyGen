#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<../src/tree.h>
#include<../src/strtab.h>

extern int yyparse(void);
extern FILE* yyin;

void printhelp(){
    printf("Usage: mcc [--ast] [--sym] [-h|--help] FILE\n");
    printf("\t--ast:\t\tPrint a textual representation of the constructed abstract syntax tree.\n");
    printf("\t--sym:\t\tPrint a textual representation of the constructed symbol table.\n");
    printf("\t-h,--help:\tPrint this help information and exit.\n\n");
}

int main(int argc, char *argv[]) {
    int p_ast = 0;
    int p_symtab = 0;

    // Skip first arg (program name), then check all but last for options.
    for(int i=1; i < argc - 1; i++){
        if(strcmp(argv[i],"-h")==0 || strcmp(argv[i],"--help")==0){
            printhelp();
            return 0;
        }
        else if(strcmp(argv[i],"--ast")==0){
            p_ast = 1;
        }
        else if(strcmp(argv[i],"--sym")==0){
            p_symtab = 1;
        }
        else{
            printhelp();
            return 0;
        }

    }

    yyin = fopen(argv[argc - 1],"r");
    if(!yyin){
        printf("error: unable to read source file %s\n",argv[argc-1]);
        return -1;
    }

    if (!yyparse()){
        printf("Compilation finished.\n\n");
        if(p_ast)
            printAst(ast, 1);
        if(p_symtab)
            print_sym_tab();
    }
    return 0;
}
