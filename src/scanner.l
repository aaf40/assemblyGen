
/* definitions */

%{
#include<stdio.h>
#include"../obj/y.tab.h"

int scancol = 1;
int yycol = 1;
int scanlineno = 1;

char* yyerrormsg;

void updateCol();
void countLines();
int processChar();
%}

newline         \n
whitespace      [ \t]+
integer         0|[1-9][0-9]*
integerlead0    0[0-9]+
character       \'([^\']|\\[\\nt\'])\'

comment         \/\*([^\*]|\*[^\/])*\*\/
untermcomment   \/\*([^\*]|\*[^\/])*

identifier      [a-zA-Z][a-zA-Z0-9]*
illidentifier   [0-9]+[a-zA-Z][a-zA-Z0-9]*

yylineno = 1;

%%

 /* rules */

 /* Keywords */;
"if"            {updateCol(); return KWD_IF;}
"else"          {updateCol(); return KWD_ELSE;}
"while"         {updateCol(); return KWD_WHILE;}
"int"           {updateCol(); return KWD_INT;}
"char"          {updateCol(); return KWD_CHAR;}
"return"        {updateCol(); return KWD_RETURN;}
"void"          {updateCol(); return KWD_VOID;}

 /* Operators */;
\+              {updateCol(); return OPER_ADD;}
\-              {updateCol(); return OPER_SUB;}
\*              {updateCol(); return OPER_MUL;}
\/              {updateCol(); return OPER_DIV;}
\<=             {updateCol(); return OPER_LTE;}
\>=             {updateCol(); return OPER_GTE;}
\<              {updateCol(); return OPER_LT;}
\>              {updateCol(); return OPER_GT;}
==              {updateCol(); return OPER_EQ;}
!=              {updateCol(); return OPER_NEQ;}
=               {updateCol(); return OPER_ASGN;}

 /* Punctuation */;
\[              {updateCol(); return LSQ_BRKT;}
\]              {updateCol(); return RSQ_BRKT;}
\{              {updateCol(); return LCRLY_BRKT;}
\}              {updateCol(); return RCRLY_BRKT;}
\(              {updateCol(); return LPAREN;}
\)              {updateCol(); return RPAREN;}
,               {updateCol(); return COMMA;}
;               {updateCol(); return SEMICLN;}

 /* Identifiers */;
{identifier}    {updateCol();
                 yylval.strval = (char *) malloc(yyleng *sizeof(char));
                 strcpy(yylval.strval, yytext);
                 return ID;}
{illidentifier} {updateCol(); yyerrormsg = "Identifiers may not start with a digit"; return ERROR;}

 /* Constants */;
{integer}       {updateCol(); yylval.value = atoi(yytext); return INTCONST;}
{integerlead0}  {updateCol(); yyerrormsg = "Integers may not have leading zeros"; return ERROR;}
{character}     {updateCol(); return processChar();}

 /* Comments */;
{comment}       {updateCol(); /* skip comments */}
{untermcomment} {updateCol(); yyerrormsg = "Unterminated comment"; return ERROR;}

 /* Other */;
{newline}       {scancol = 1; scanlineno++; yylineno = scanlineno;}
{whitespace}    {updateCol(); /* skip whitespace */}
.               {return ILLEGAL_TOKEN;}

%%

/* user routines */

void updateCol(){
    yycol = scancol;
    scancol += yyleng;
}

void countLines(){
    yylineno = scanlineno;
    for (int i=0; i<yyleng; i++){
        if (yytext[i] == '\n')
            scanlineno++;
    }
}
int processChar(){
    // yytext[0] will be "'", so check yytext[1] for escap
    if (yytext[1] == '\\'){
        if (yytext[2] == '\''){
            yylval.value = '\'';
        }
        else if (yytext[2] == 'n'){
            yylval.value = '\n';
        }
        else if (yytext[2] == 't'){
            yylval.value = '\t';
        }
        else if (yytext[2] == '\\'){
            yylval.value = '\\';
        }
        else{
            yyerrormsg = "Unrecognized escape character in String";
            return ERROR;
        }
    }
    else{
        // If the character isn't escaped, add it to our temp string.
        yylval.value = yytext[1];
    }
    return CHARCONST;
}
