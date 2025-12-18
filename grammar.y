
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

        /* Use yylloc.first_column to store the token index */
        yylloc.first_column = (int) TheIndex;

        if (index >= Lex.info->count) {
            index = 0;
            return EOF;
        }
        else {
            return token;
        }
    }
    int yyerror(char *error) {
        printf("Parser error: %s at token index %lu\n", error, TheIndex);
        return 0;
    }
%}

%locations

%union {
    void *yuck;
    size_t index;
}

%token IDENTIFIER
%token STRING_LITERAL
%token NUMBER

%token PLUS
%token NOT
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

%token EN_NOT
%token EN_MODULE
%token EN_IMPORT
%token EN_EXPORT

/* Precedence: lowest to highest (later = higher precedence) */
/* IN/EN_IN have lowest precedence so "x in A extends B" = "x in (A extends B)" */
%left IN EN_IN
%left EXTENDS EN_EXTENDS
%left UNION EN_UNION
%left INTERSECT EN_INTERSECT
%left BACKSLASH EN_DIFFERENCE

%token ARROW
%token TERMINAL
%token LPAREN
%token RPAREN

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
%type<yuck> union_expression
%type<yuck> intersect_expression
%type<yuck> difference_expression
%type<yuck> alphabet_body
%type<yuck> u_letters
%type<yuck> letters
%type<yuck> letter
%type<yuck> word_literal
%type<yuck> word_in_expression
%type<yuck> abstract_size
%type<yuck> abstract_alphabet
%type<yuck> algorithm
%type<yuck> algorithm_name
%type<yuck> algorithm_word_param
%type<yuck> algorithm_rules
%type<yuck> algorithm_rule
%type<yuck> pattern
%type<yuck> IDENTIFIER
%type<yuck> STRING_LITERAL
%type<yuck> NUMBER

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
        s->token_index = @1.first_column;
        $$ = s; }
    | ATSIGN {
        syntax_store *s = Syntax.push();
        s->type = ast_scope_context_names_literal;
        s->token_index = @1.first_column;
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
    | scope                { $$ = $1; }
    | algorithm            { $$ = $1; }
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
        s->token_index = @1.first_column;
        $$ = s; }
    ;
scope_context
    : LANGLE u_inherited_scope_names RANGLE { 
        $$ = $2; }
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
        s->token_index = @1.first_column;
        $$ = s;
    }
    ;
r_expression
    : alphabet_body { $$ = $1; }
    | extends_expression { $$ = $1; }
    | union_expression { $$ = $1; }
    | intersect_expression { $$ = $1; }
    | difference_expression { $$ = $1; }
    | word_literal { $$ = $1; }
    | word_in_expression { $$ = $1; }
    | abstract_size { $$ = $1; }
    | abstract_alphabet { $$ = $1; }
    | variable { $$ = $1; }
    ;
extends_expression
    : r_expression EXTENDS r_expression {
        syntax_store *s = Syntax.push();
        syntax_store *r_expression_l = (syntax_store *) $1;
        syntax_store *r_expression_r = (syntax_store *) $3;
        s->type = ast_extends_expression;
        s->token_index = @2.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = (syntax_store *) r_expression_l;
        s->content[0]->topic = s;
        s->content[1] = (syntax_store *) r_expression_r;
        s->content[1]->topic = s;
        $$ = s;
    }
    | r_expression EN_EXTENDS r_expression {
        syntax_store *s = Syntax.push();
        syntax_store *r_expression_l = (syntax_store *) $1;
        syntax_store *r_expression_r = (syntax_store *) $3;
        s->type = ast_extends_expression;
        s->token_index = @2.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = (syntax_store *) r_expression_l;
        s->content[0]->topic = s;
        s->content[1] = (syntax_store *) r_expression_r;
        s->content[1]->topic = s;
        $$ = s;
    }
    ;
union_expression
    : r_expression UNION r_expression {
        syntax_store *s = Syntax.push();
        s->type = ast_union_expression;
        s->token_index = @2.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = (syntax_store *) $1;
        s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $3;
        s->content[1]->topic = s;
        $$ = s;
    }
    | r_expression EN_UNION r_expression {
        syntax_store *s = Syntax.push();
        s->type = ast_union_expression;
        s->token_index = @2.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = (syntax_store *) $1;
        s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $3;
        s->content[1]->topic = s;
        $$ = s;
    }
    ;
intersect_expression
    : r_expression INTERSECT r_expression {
        syntax_store *s = Syntax.push();
        s->type = ast_intersect_expression;
        s->token_index = @2.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = (syntax_store *) $1;
        s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $3;
        s->content[1]->topic = s;
        $$ = s;
    }
    | r_expression EN_INTERSECT r_expression {
        syntax_store *s = Syntax.push();
        s->type = ast_intersect_expression;
        s->token_index = @2.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = (syntax_store *) $1;
        s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $3;
        s->content[1]->topic = s;
        $$ = s;
    }
    ;
difference_expression
    : r_expression BACKSLASH r_expression {
        syntax_store *s = Syntax.push();
        s->type = ast_difference_expression;
        s->token_index = @2.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = (syntax_store *) $1;
        s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $3;
        s->content[1]->topic = s;
        $$ = s;
    }
    | r_expression EN_DIFFERENCE r_expression {
        syntax_store *s = Syntax.push();
        s->type = ast_difference_expression;
        s->token_index = @2.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = (syntax_store *) $1;
        s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $3;
        s->content[1]->topic = s;
        $$ = s;
    }
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
        s->token_index = @1.first_column;
        $$ = s;
    }
    ;
word_literal
    : STRING_LITERAL {
        syntax_store *s = Syntax.push();
        s->type = ast_word_literal;
        s->token_index = @1.first_column;
        $$ = s;
    }
    ;
word_in_expression
    : word_literal IN r_expression {
        syntax_store *s = Syntax.push();
        syntax_store *word = (syntax_store *) $1;
        syntax_store *alphabet = (syntax_store *) $3;
        s->type = ast_word_in_expression;
        s->token_index = @2.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = word;
        s->content[0]->topic = s;
        s->content[1] = alphabet;
        s->content[1]->topic = s;
        $$ = s;
    }
    | word_literal EN_IN r_expression {
        syntax_store *s = Syntax.push();
        syntax_store *word = (syntax_store *) $1;
        syntax_store *alphabet = (syntax_store *) $3;
        s->type = ast_word_in_expression;
        s->token_index = @2.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = word;
        s->content[0]->topic = s;
        s->content[1] = alphabet;
        s->content[1]->topic = s;
        $$ = s;
    }
    ;
abstract_size
    : LBRACKET NUMBER RBRACKET {
        syntax_store *s = Syntax.push();
        s->type = ast_abstract_size;
        s->token_index = @2.first_column;  /* token index of NUMBER */
        $$ = s;
    }
    ;
abstract_alphabet
    : abstract_size PLUS alphabet_body {
        syntax_store *s = Syntax.push();
        syntax_store *abstract_part = (syntax_store *) $1;
        syntax_store *concrete_part = (syntax_store *) $3;
        s->type = ast_abstract_alphabet;
        s->token_index = @2.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = abstract_part;
        s->content[0]->topic = s;
        s->content[1] = concrete_part;
        s->content[1]->topic = s;
        $$ = s;
    }
    ;
algorithm_name
    : IDENTIFIER {
        syntax_store *s = Syntax.push();
        s->type = ast_variable;
        s->token_index = (size_t) @1.first_column;
        s->size = 0;
        s->content = NULL;
        $$ = s;
    }
    ;
algorithm_word_param
    : IDENTIFIER {
        syntax_store *s = Syntax.push();
        s->type = ast_variable;
        s->token_index = (size_t) @1.first_column;
        s->size = 0;
        s->content = NULL;
        $$ = s;
    }
    ;
algorithm
    : algorithm_name DOUBLE_COLON variable LPAREN algorithm_word_param RPAREN LCURL algorithm_rules RCURL {
        /* A::B (C) { rules }
           content[0] = algorithm name (ast_variable)
           content[1] = alphabet variable (ast_variable)
           content[2] = word parameter name (ast_variable)
           content[3] = rules (ast_algorithm_rules) */
        syntax_store *s = Syntax.push();
        syntax_store *name = (syntax_store *) $1;
        syntax_store *alphabet_var = (syntax_store *) $3;
        syntax_store *word_param = (syntax_store *) $5;
        syntax_store *rules = (syntax_store *) $8;

        s->type = ast_algorithm;
        s->token_index = @1.first_column;  /* Use algorithm name's token */
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = name;
        s->content[1] = alphabet_var;
        s->content[2] = word_param;
        s->content[3] = rules;

        name->topic = s;
        if (alphabet_var != NULL) alphabet_var->topic = s;
        word_param->topic = s;
        if (rules != NULL) rules->topic = s;

        $$ = s;
    }
    ;
algorithm_rules
    : { $$ = NULL; }
    | algorithm_rule {
        syntax_store *s = Syntax.push();
        s->type = ast_algorithm_rules;
        s->size = 1;
        s->content = malloc(sizeof(syntax_store *));
        s->content[0] = (syntax_store *) $1;
        s->content[0]->topic = s;
        $$ = s;
    }
    | algorithm_rules SEMICOLON algorithm_rule {
        syntax_store *rules = (syntax_store *) $1;
        syntax_store *rule = (syntax_store *) $3;
        rules->size += 1;
        size_t bytes = rules->size * sizeof(syntax_store *);
        rules->content = (syntax_store **) realloc(rules->content, bytes);
        rules->content[rules->size - 1] = rule;
        rules->content[rules->size - 1]->topic = rules;
        $$ = rules;
    }
    ;
algorithm_rule
    : pattern ARROW pattern {
        /* P -> Q (substitution rule) */
        syntax_store *s = Syntax.push();
        syntax_store *left = (syntax_store *) $1;
        syntax_store *right = (syntax_store *) $3;
        s->type = ast_algorithm_rule;
        s->token_index = @2.first_column;  /* ARROW token */
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = left;
        s->content[0]->topic = s;
        s->content[1] = right;
        s->content[1]->topic = s;
        $$ = s;
    }
    | pattern TERMINAL {
        /* P -. (terminal rule) */
        syntax_store *s = Syntax.push();
        syntax_store *left = (syntax_store *) $1;
        s->type = ast_algorithm_rule;
        s->token_index = @2.first_column;  /* TERMINAL token */
        s->size = 1;
        s->content = malloc(sizeof(syntax_store *));
        s->content[0] = left;
        s->content[0]->topic = s;
        $$ = s;
    }
    ;
pattern
    : IDENTIFIER {
        /* Single letter/identifier pattern */
        syntax_store *s = Syntax.push();
        s->type = ast_pattern;
        s->token_index = @1.first_column;
        s->size = 0;
        s->content = NULL;
        $$ = s;
    }
    | pattern IDENTIFIER {
        /* Concatenated pattern: abc becomes pattern of a, b, c
           NOTE: For now we just track the count. The pattern letters
           can be reconstructed by walking backwards from token_index. */
        syntax_store *pat = (syntax_store *) $1;
        pat->size += 1;
        $$ = pat;
    }
    ;
