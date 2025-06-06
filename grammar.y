
%code requires { 
    #include <stdio.h> 
    #include <stdlib.h>
    #include "syntax.h"
    #include "lex.h"
    #include "token.h"
    //#include "context.h"
}


%{  
    #include <stdio.h> 
    #include <stdlib.h>
    #include "syntax.h"
    #include "lex.h"
    #include "token.h"
    #include "context.h"

    // TODO: The new...
    #include "grammar.h"

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
        printf("Parser error: %s \n", error);
        return 0;
    }
%}

%union {
    void *yuck;
}

%token IDENTIFIER

%token IN     
%token NOT 
%left EXTENDS  
%token DOUBLE_COLON 

%token EQUAL 
%token COMMA    
%token LCURL      
%token RCURL         
%token SEMICOLON   
%token PERIOD      
%token LANGLE
%token RANGLE
%token LBRACKET
%token RBRACKET
%token ATSIGN

%token EN_IN          
%token EN_NOT          
%left EN_EXTENDS      
%token EN_MODULE          
%token EN_IMPORT      
%token EN_EXPORT

%type<yuck> program
%type<yuck> statements
%type<yuck> scope
%type<yuck> scope_name
%type<yuck> scope_context
%type<yuck> u_inherited_scope_names
%type<yuck> function
%type<yuck> statementz
%type<yuck> statement
%type<yuck> assignment_statement
%type<yuck> import_statement
%type<yuck> l_expression
%type<yuck> variable
%type<yuck> r_expression
%type<yuck> extends_expression
%type<yuck> alphabet_body
%type<yuck> u_letters
%type<yuck> letters
%type<yuck> letter
%type<yuck> IDENTIFIER

%%

program 
    : statements { 
        $$ = program_statements($1); }
    ;
statements  
    : { 
        $$ = statements_root(); }
    | statements statementz { 
        $$ = statements_statements_statementz($1, $2); }
    | statements statementz PERIOD {
        $$ = statements_statements_statementz_PERIOD($1, $2); }
    | statements scope  {
        $$ = statements_statements_scope($1, $2); }
    ;
scope 
    : scope_export scope_module scope_name LCURL statements RCURL {
        $$ = scope_scope($5, $3); }
    | scope_context scope_name LCURL statements RCURL {
        $$ = scope_scope_context_scope($1, $4, $2); }
    | scope_export scope_context scope_name LCURL statements RCURL {
        $$ = scope_scope_context_scope($2, $5, $3); }
    ;
scope_export
    : EN_EXPORT {}
    ;
scope_module
    : EN_MODULE {}
    ;
scope_name 
    : { $$ = NULL; } 
    | IDENTIFIER {
        syntax_store *s = Syntax.push();
        s->type = ast_scope_name;
        s->token_index = TheIndex;
        $$ = s; }
    ;
scope_context
    : LANGLE u_inherited_scope_names RANGLE { 
        $$ = $2; }
    ;

function        
    : LBRACKET RBRACKET LCURL statements RCURL {
        syntax_store *s = Syntax.push();
        s->type = ast_function;
        s->size = 1;
        s->content = malloc(sizeof(syntax_store *));
        s->content[0] = (syntax_store *) $4;
        $$ = s; }
    ;
u_inherited_scope_names
    : { $$ = NULL; }
    | inherited_scope_names { $$ = NULL; }
    | EQUAL { 
        syntax_store *s = Syntax.push();
        s->type = ast_scope_context_names_literal;
        s->token_index = TheIndex;
        $$ = s; } 
    | ATSIGN { 
        syntax_store *s = Syntax.push();
        s->type = ast_scope_context_names_literal;
        s->token_index = TheIndex;
        $$ = s; } 
    ;
inherited_scope_names
    : inherited_scope_name {}
    | inherited_scope_names COMMA inherited_scope_name {}
    ;
inherited_scope_name
    : IDENTIFIER {}
    | inherited_scope_name DOUBLE_COLON IDENTIFIER {}
    ;
statementz 
    : statement { 
        syntax_store *s = Syntax.push();
        s->type = ast_statements;
        s->size = 1;
        s->content = malloc(sizeof(syntax_store *));
        s->content[0] = (syntax_store *) $1;
        s->content[0]->topic = s;
        $$ = s; }

    | statementz SEMICOLON statement { 
        syntax_store *statementz = (syntax_store *) $1;
        syntax_store *statement  = (syntax_store *) $3;

        statementz->size += 1;
        size_t bytes = statementz->size * sizeof(syntax_store *);
        statementz->content = (syntax_store **) realloc(
            statementz->content, bytes);
        statementz->content[statementz->size - 1] = statement;
        statementz->content[statementz->size - 1]->topic = statementz;
        $$ = statementz; }

    | statementz COMMA statement {
        syntax_store *statementz = (syntax_store *) $1;
        syntax_store *statement  = (syntax_store *) $3;
        statementz->size += 1;
        size_t bytes = statementz->size * sizeof(syntax_store *);
        statementz->content = (syntax_store **) realloc(
            statementz->content, bytes);
        statementz->content[statementz->size - 1] = statement;
        statementz->content[statementz->size - 1]->topic = statementz;
        $$ = statementz; }
    ;
statement 
    : assignment_statement { $$ = $1; }
    | import_statement     { $$ = $1; }
    | r_expression         { $$ = $1; }
    | function             { $$ = $1; }
    ;
assignment_statement 
    : l_expression EQUAL r_expression {
        syntax_store *s = Syntax.push(); 
        syntax_store *l_expression = (syntax_store *) $1;
        syntax_store *r_expression = (syntax_store *) $3;
        s->type = ast_assignment_statement;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = (syntax_store *) l_expression;
        s->content[1] = (syntax_store *) r_expression;
        s->content[0]->topic = s;
        s->content[1]->topic = s;
        $$ = s;
    }
    ;
import_statement 
    : EN_IMPORT EN_MODULE IDENTIFIER { 
        syntax_store *s = Syntax.push();
        s->type = ast_import_statement;
        s->size = 1;
        s->content = malloc(sizeof(syntax_store *));
        s->content[0] = (syntax_store *) $3;
        $$ = s; 
    }
    ;
l_expression 
    : variable { $$ = $1; }
    ;
variable 
    : IDENTIFIER {
        syntax_store *s = Syntax.push();
        s->type = ast_variable;
        s->token_index = TheIndex;
        $$ = s;
    }
    ;
r_expression
    : alphabet_body { $$ = $1; }
    | extends_expression { $$ = $1; }
    | variable { $$ = $1; }
    ;
extends_expression
    : r_expression EXTENDS r_expression {
        syntax_store *s = Syntax.push();
        syntax_store *r_expression_l = (syntax_store *) $1;
        syntax_store *r_expression_r = (syntax_store *) $3;
        s->type = ast_extends_expression;
        s->token_index = TheIndex;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = (syntax_store *) r_expression_l;
        s->content[0]->topic = s;
        s->content[1] = (syntax_store *) r_expression_r;
        s->content[1]->topic = s;
        $$ = s;
    }
    | r_expression EN_EXTENDS r_expression { $$ = $1; }
    ;
alphabet_body
    : LCURL u_letters RCURL { 
        syntax_store *s = Syntax.push(); 
        syntax_store *letters = (syntax_store *) $2;
        s->type = ast_alphabet_body;
        s->size = 1;
        s->content = malloc(sizeof(syntax_store *));
        s->content[0] = (syntax_store *) letters;
        s->content[0]->topic = s;
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
        /* On the terminus node, create the letters node and push the
           letter node onto it. */ 
        syntax_store *s = Syntax.push();
        s->type = ast_letters;
        s->size = 1;
        s->content = malloc(sizeof(syntax_store *));
        s->content[0] = (syntax_store *) $1;
        s->content[0]->topic = s;
        $$ = s;
    }

    | letters COMMA letter { 
        /* On the inner node, inherit the letters node and push the 
           letter node onto it. */ 
        syntax_store *letters = (syntax_store *) $1;
        syntax_store *letter  = (syntax_store *) $3;
        letters->size += 1;
        letters->content = (syntax_store **) realloc(
            letters->content, letters->size * sizeof(syntax_store *));
        letters->content[letters->size - 1] = letter;
        letters->content[letters->size - 1]->topic = letters;
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
