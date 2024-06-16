
%code requires { 
    #include <stdio.h> 
    #include <stdlib.h>
    #include "syntax.h"
    #include "lex.h"
    #include "token.h"
}


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

%union {
    void *yuck;
}

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

%type<yuck> l_expression
%type<yuck> r_expression
%type<yuck> alphabet_body
%type<yuck> u_letters
%type<yuck> letters
%type<yuck> letter
%type<yuck> IDENTIFIER

%%

program 
    : statements { syntax_store *s = Syntax.push(); }
    ;
statements  
    : {}
    | statements statementz PERIOD { }
    ;
statementz 
    : statement { }
    | statementz SEMICOLON statement { }
statement 
    : l_expression EQUAL r_expression { syntax_store *s = Syntax.push(); }
    ;
l_expression 
    : IDENTIFIER { syntax_store *s = Syntax.push(); }
    ;
r_expression
    : alphabet_body { $$ = $1; }
    ;
alphabet_body
    : LCURL u_letters RCURL { 
        syntax_store *s = Syntax.push(); 
        syntax_store *letters = (syntax_store *) $2;
        s->type = ast_alphabet_body;
        s->size = 1;
        s->content = malloc(sizeof(syntax_store *));
        s->content[0] = (syntax_store *) letters;
        Syntax.check(s);
        $$ = s;
    }
    ;
u_letters
    :  {}
    | letters { $$ = $1; }
    ;
letters
    : letter { 
        syntax_store *s = Syntax.push();
        s->type = ast_letters;
        s->size = 1;
        s->content = malloc(sizeof(syntax_store *));
        s->content[0] = (syntax_store *) $1;
        $$ = s;
    }
    | letters COMMA letter { 

        syntax_store *letters = (syntax_store *) $1;
        syntax_store *letter  = (syntax_store *) $3;

        letters->size += 1;
        letters->content = (syntax_store **) realloc(
            letters->content, letters->size * sizeof(syntax_store *));
        letters->content[letters->size - 1] = letter;
        $$ = letters;
    }
    ;
letter
    : IDENTIFIER { 
        syntax_store *s = Syntax.push();
        s->type = ast_letter;
        s->token_index = TheIndex;
        $$ = s;
    }
    ;
