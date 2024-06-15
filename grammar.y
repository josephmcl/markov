
%{  
    #include <stdio.h> 
    #include <stdlib.h>
    #include "syntax.h"
    #include "lex.h"
    #include "token.h"
    size_t TheIndex = 0;
    int yylex (void) {
        static size_t index = 0;
        int token;
        do {
            token = Lex.bison_token(index);
            index += 1;
        } while (token == TOKEN_UNUSED_BY_PARSER);

        TheIndex = index - 1;
        
        if (index >= Lex.info->count) {
            index = 0; 
            return EOF;
        }
        else {
            return token;
        }
    }
    int yyerror(char *error) {
        printf("Parser error: ");
        printf(error);
        printf("\n");
        return 0;
    }
%}

%token IDENTIFIER

%token IN     
%token NOT 
%token EXTENDS  

%token EQUAL 
%token COMMA    
%token LCURL      
%token RCURL         
%token SEMICOLON   
%token PERIOD      

%token EN_IN          
%token EN_NOT          
%token EN_EXTENDS      

%%

program 
    : statements { syntax_store *s = Syntax.append(); }
    ;
statements  
    : {}
    | statements statementz PERIOD { syntax_store *s = Syntax.append(); }
    ;
statementz 
    : statement { syntax_store *s = Syntax.append(); }
    | statementz SEMICOLON statement { syntax_store *s = Syntax.append(); }
statement 
    : l_expression EQUAL r_expression { syntax_store *s = Syntax.append(); }
    ;
l_expression 
    : IDENTIFIER { syntax_store *s = Syntax.append(); }
    ;
r_expression
    : alphabet_body { syntax_store *s = Syntax.append(); }
    ;
alphabet_body
    : LCURL u_letters RCURL { syntax_store *s = Syntax.append(); }
    ;
u_letters
    :  {}
    | letters { syntax_store *s = Syntax.append(); }
    ;
letters
    : letter { syntax_store *s = Syntax.append(); }
    | letters COMMA letter { syntax_store *s = Syntax.append(); }
    ;
letter
    : IDENTIFIER { 
        syntax_store *s = Syntax.append();
        lexical_store *l = Lex.store(TheIndex);
        //printf("%lu\n", TheIndex); 
        //printf("\'%.*s\'\n", l->end - l->begin, l->begin);
    }
    ;
