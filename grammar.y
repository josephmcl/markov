
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
%right BIND
%left IN EN_IN
%left EXTENDS EN_EXTENDS
%left UNION EN_UNION
%left INTERSECT EN_INTERSECT
%left BACKSLASH EN_DIFFERENCE

%token ARROW
%token TERMINAL
%token EMIT_ARROW
%token EMIT_TERMINAL
%token COLON
%token TILDE
%token BIND
%token RULE_EQ
%token APPROX
%token DOUBLE_TILDE
%token EXCLAIM
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
%type<yuck> range_literal
%type<yuck> range_function
%type<yuck> abstract_named
%type<yuck> abstract_name_list
%type<yuck> algorithm
%type<yuck> algorithm_name
%type<yuck> algorithm_rules
%type<yuck> algorithm_rule
%type<yuck> algorithm_call
%type<yuck> equivalence_statement
%type<yuck> bind_expression
%type<yuck> bind_rule
%type<yuck> bind_rules_list
%type<yuck> bind_rules_value
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
    | algorithm_call       { $$ = $1; }
    | equivalence_statement { $$ = $1; }
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
    | abstract_named { $$ = $1; }
    | range_literal { $$ = $1; }
    | range_function { $$ = $1; }
    | bind_expression { $$ = $1; }
    | bind_rules_value { $$ = $1; }
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
range_literal
    : NUMBER PERIOD PERIOD NUMBER {
        /* 0..5 — range of integers, step 1
           content[0] = start (ast_variable with NUMBER token)
           content[1] = end (ast_variable with NUMBER token) */
        syntax_store *s = Syntax.push();
        s->type = ast_range_literal;
        s->token_index = @1.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        syntax_store *start = Syntax.push();
        start->type = ast_variable; start->token_index = @1.first_column;
        start->size = 0; start->content = NULL;
        syntax_store *end = Syntax.push();
        end->type = ast_variable; end->token_index = @4.first_column;
        end->size = 0; end->content = NULL;
        s->content[0] = start; start->topic = s;
        s->content[1] = end; end->topic = s;
        $$ = s;
    }
    ;
range_function
    : IDENTIFIER LPAREN NUMBER COMMA NUMBER COMMA NUMBER RPAREN {
        /* range(0, 10, 2) — range with explicit step
           content[0] = start, content[1] = end, content[2] = step */
        syntax_store *s = Syntax.push();
        s->type = ast_range_function;
        s->token_index = @1.first_column;
        s->size = 3;
        s->content = malloc(sizeof(syntax_store *) * 3);
        syntax_store *start = Syntax.push();
        start->type = ast_variable; start->token_index = @3.first_column;
        start->size = 0; start->content = NULL;
        syntax_store *end = Syntax.push();
        end->type = ast_variable; end->token_index = @5.first_column;
        end->size = 0; end->content = NULL;
        syntax_store *step = Syntax.push();
        step->type = ast_variable; step->token_index = @7.first_column;
        step->size = 0; step->content = NULL;
        s->content[0] = start; start->topic = s;
        s->content[1] = end; end->topic = s;
        s->content[2] = step; step->topic = s;
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
abstract_named
    : LBRACKET abstract_name_list RBRACKET {
        /* [greater, lesser] — abstract alphabet with named positions */
        syntax_store *list = (syntax_store *) $2;
        syntax_store *s = Syntax.push();
        s->type = ast_abstract_named;
        s->token_index = @1.first_column;
        s->size = list->size;
        s->content = list->content;
        for (size_t i = 0; i < s->size; i++) {
            if (s->content[i]) s->content[i]->topic = s;
        }
        $$ = s;
    }
    ;
abstract_name_list
    : IDENTIFIER COMMA IDENTIFIER {
        /* First two names — need at least 2 for an abstract alphabet */
        syntax_store *s = Syntax.push();
        s->type = ast_abstract_named;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        syntax_store *n1 = Syntax.push();
        n1->type = ast_variable; n1->token_index = @1.first_column;
        n1->size = 0; n1->content = NULL;
        syntax_store *n2 = Syntax.push();
        n2->type = ast_variable; n2->token_index = @3.first_column;
        n2->size = 0; n2->content = NULL;
        s->content[0] = n1;
        s->content[1] = n2;
        $$ = s;
    }
    | abstract_name_list COMMA IDENTIFIER {
        syntax_store *list = (syntax_store *) $1;
        syntax_store *name = Syntax.push();
        name->type = ast_variable;
        name->token_index = @3.first_column;
        name->size = 0; name->content = NULL;
        list->size += 1;
        list->content = realloc(list->content, sizeof(syntax_store *) * list->size);
        list->content[list->size - 1] = name;
        $$ = list;
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
algorithm
    : algorithm_name DOUBLE_COLON variable LCURL algorithm_rules RCURL {
        /* A::B { rules } — concrete alphabet
           content[0] = algorithm name (ast_variable)
           content[1] = alphabet variable (ast_variable)
           content[2] = rules (ast_algorithm_rules) */
        syntax_store *s = Syntax.push();
        syntax_store *name = (syntax_store *) $1;
        syntax_store *alphabet_var = (syntax_store *) $3;
        syntax_store *rules = (syntax_store *) $5;

        s->type = ast_algorithm;
        s->token_index = @1.first_column;
        s->size = 3;
        s->content = malloc(sizeof(syntax_store *) * 3);
        s->content[0] = name;
        s->content[1] = alphabet_var;
        s->content[2] = rules;

        name->topic = s;
        if (alphabet_var != NULL) alphabet_var->topic = s;
        if (rules != NULL) rules->topic = s;

        $$ = s;
    }
    | algorithm_name DOUBLE_COLON abstract_size LCURL algorithm_rules RCURL {
        /* A::[N] { rules } — abstract alphabet
           content[0] = algorithm name (ast_variable)
           content[1] = abstract size (ast_abstract_size)
           content[2] = rules (ast_algorithm_rules) */
        syntax_store *s = Syntax.push();
        syntax_store *name = (syntax_store *) $1;
        syntax_store *abstract_alph = (syntax_store *) $3;
        syntax_store *rules = (syntax_store *) $5;

        s->type = ast_algorithm;
        s->token_index = @1.first_column;
        s->size = 3;
        s->content = malloc(sizeof(syntax_store *) * 3);
        s->content[0] = name;
        s->content[1] = abstract_alph;
        s->content[2] = rules;

        name->topic = s;
        abstract_alph->topic = s;
        if (rules != NULL) rules->topic = s;

        $$ = s;
    }
    | algorithm_name DOUBLE_COLON abstract_named LCURL algorithm_rules RCURL {
        /* A::[greater, lesser] { rules } — named abstract alphabet
           content[0] = algorithm name (ast_variable)
           content[1] = named abstract (ast_abstract_named)
           content[2] = rules (ast_algorithm_rules) */
        syntax_store *s = Syntax.push();
        syntax_store *name = (syntax_store *) $1;
        syntax_store *abstract_alph = (syntax_store *) $3;
        syntax_store *rules = (syntax_store *) $5;

        s->type = ast_algorithm;
        s->token_index = @1.first_column;
        s->size = 3;
        s->content = malloc(sizeof(syntax_store *) * 3);
        s->content[0] = name;
        s->content[1] = abstract_alph;
        s->content[2] = rules;

        name->topic = s;
        abstract_alph->topic = s;
        if (rules != NULL) rules->topic = s;

        $$ = s;
    }
    | algorithm_name DOUBLE_COLON LBRACKET r_expression RBRACKET TILDE IDENTIFIER {
        /* sort ::[0..5]~ bsort — bounded observational equivalence */
        syntax_store *s = Syntax.push();
        s->type = ast_equivalence;
        s->token_index = @6.first_column;
        s->size = 3;
        s->content = malloc(sizeof(syntax_store *) * 3);
        s->content[0] = (syntax_store *) $1; s->content[0]->topic = s;
        syntax_store *right = Syntax.push();
        right->type = ast_variable; right->token_index = @7.first_column;
        right->size = 0; right->content = NULL;
        s->content[1] = right; right->topic = s;
        s->content[2] = (syntax_store *) $4; s->content[2]->topic = s;
        $$ = s;
    }
    | algorithm_name DOUBLE_COLON LBRACKET r_expression RBRACKET APPROX IDENTIFIER {
        /* sort ::[0..5]≈ bsort — bounded bisimulation */
        syntax_store *s = Syntax.push();
        s->type = ast_equivalence;
        s->token_index = @6.first_column;
        s->size = 3;
        s->content = malloc(sizeof(syntax_store *) * 3);
        s->content[0] = (syntax_store *) $1; s->content[0]->topic = s;
        syntax_store *right = Syntax.push();
        right->type = ast_variable; right->token_index = @7.first_column;
        right->size = 0; right->content = NULL;
        s->content[1] = right; right->topic = s;
        s->content[2] = (syntax_store *) $4; s->content[2]->topic = s;
        $$ = s;
    }
    | algorithm_name DOUBLE_COLON LBRACKET r_expression RBRACKET DOUBLE_TILDE IDENTIFIER {
        /* sort ::[0..5]~~ bsort — ASCII bounded bisimulation */
        syntax_store *s = Syntax.push();
        s->type = ast_equivalence;
        s->token_index = @6.first_column;
        s->size = 3;
        s->content = malloc(sizeof(syntax_store *) * 3);
        s->content[0] = (syntax_store *) $1; s->content[0]->topic = s;
        syntax_store *right = Syntax.push();
        right->type = ast_variable; right->token_index = @7.first_column;
        right->size = 0; right->content = NULL;
        s->content[1] = right; right->topic = s;
        s->content[2] = (syntax_store *) $4; s->content[2]->topic = s;
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
    /* AST layout for all variants:
       content[0] = LHS pattern (always)
       content[1] = RHS replacement (NULL for terminal)
       content[2] = rule name (ast_rule_name, NULL if unnamed)
       content[3] = emit string (ast_emit_expression, NULL if silent/default)
       token_index = arrow token (ARROW/TERMINAL/EMIT_ARROW/EMIT_TERMINAL) */

    /* --- Unnamed rules --- */
    : pattern ARROW pattern {
        syntax_store *s = Syntax.push();
        s->type = ast_algorithm_rule;
        s->token_index = @2.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $1; s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $3; s->content[1]->topic = s;
        s->content[2] = NULL;
        s->content[3] = NULL;
        $$ = s;
    }
    | pattern TERMINAL {
        syntax_store *s = Syntax.push();
        s->type = ast_algorithm_rule;
        s->token_index = @2.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $1; s->content[0]->topic = s;
        s->content[1] = NULL;
        s->content[2] = NULL;
        s->content[3] = NULL;
        $$ = s;
    }
    | pattern EMIT_ARROW pattern COLON STRING_LITERAL {
        syntax_store *s = Syntax.push();
        syntax_store *emit = Syntax.push();
        emit->type = ast_emit_expression;
        emit->token_index = @5.first_column;
        emit->size = 0; emit->content = NULL;
        s->type = ast_algorithm_rule;
        s->token_index = @2.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $1; s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $3; s->content[1]->topic = s;
        s->content[2] = NULL;
        s->content[3] = emit; emit->topic = s;
        $$ = s;
    }
    | pattern EMIT_ARROW pattern {
        syntax_store *s = Syntax.push();
        s->type = ast_algorithm_rule;
        s->token_index = @2.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $1; s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $3; s->content[1]->topic = s;
        s->content[2] = NULL;
        s->content[3] = NULL;
        $$ = s;
    }
    | pattern EMIT_TERMINAL COLON STRING_LITERAL {
        syntax_store *s = Syntax.push();
        syntax_store *emit = Syntax.push();
        emit->type = ast_emit_expression;
        emit->token_index = @4.first_column;
        emit->size = 0; emit->content = NULL;
        s->type = ast_algorithm_rule;
        s->token_index = @2.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $1; s->content[0]->topic = s;
        s->content[1] = NULL;
        s->content[2] = NULL;
        s->content[3] = emit; emit->topic = s;
        $$ = s;
    }
    | pattern EMIT_TERMINAL {
        syntax_store *s = Syntax.push();
        s->type = ast_algorithm_rule;
        s->token_index = @2.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $1; s->content[0]->topic = s;
        s->content[1] = NULL;
        s->content[2] = NULL;
        s->content[3] = NULL;
        $$ = s;
    }

    /* --- Named rules --- */
    | IDENTIFIER COLON pattern ARROW pattern {
        syntax_store *s = Syntax.push();
        syntax_store *name = Syntax.push();
        name->type = ast_rule_name;
        name->token_index = @1.first_column;
        name->size = 0; name->content = NULL;
        s->type = ast_algorithm_rule;
        s->token_index = @4.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $3; s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $5; s->content[1]->topic = s;
        s->content[2] = name; name->topic = s;
        s->content[3] = NULL;
        $$ = s;
    }
    | IDENTIFIER COLON pattern TERMINAL {
        syntax_store *s = Syntax.push();
        syntax_store *name = Syntax.push();
        name->type = ast_rule_name;
        name->token_index = @1.first_column;
        name->size = 0; name->content = NULL;
        s->type = ast_algorithm_rule;
        s->token_index = @4.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $3; s->content[0]->topic = s;
        s->content[1] = NULL;
        s->content[2] = name; name->topic = s;
        s->content[3] = NULL;
        $$ = s;
    }
    | IDENTIFIER COLON pattern EMIT_ARROW pattern COLON STRING_LITERAL {
        syntax_store *s = Syntax.push();
        syntax_store *name = Syntax.push();
        name->type = ast_rule_name;
        name->token_index = @1.first_column;
        name->size = 0; name->content = NULL;
        syntax_store *emit = Syntax.push();
        emit->type = ast_emit_expression;
        emit->token_index = @7.first_column;
        emit->size = 0; emit->content = NULL;
        s->type = ast_algorithm_rule;
        s->token_index = @4.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $3; s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $5; s->content[1]->topic = s;
        s->content[2] = name; name->topic = s;
        s->content[3] = emit; emit->topic = s;
        $$ = s;
    }
    | IDENTIFIER COLON pattern EMIT_ARROW pattern {
        syntax_store *s = Syntax.push();
        syntax_store *name = Syntax.push();
        name->type = ast_rule_name;
        name->token_index = @1.first_column;
        name->size = 0; name->content = NULL;
        s->type = ast_algorithm_rule;
        s->token_index = @4.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $3; s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $5; s->content[1]->topic = s;
        s->content[2] = name; name->topic = s;
        s->content[3] = NULL;
        $$ = s;
    }
    | IDENTIFIER COLON pattern EMIT_TERMINAL COLON STRING_LITERAL {
        syntax_store *s = Syntax.push();
        syntax_store *name = Syntax.push();
        name->type = ast_rule_name;
        name->token_index = @1.first_column;
        name->size = 0; name->content = NULL;
        syntax_store *emit = Syntax.push();
        emit->type = ast_emit_expression;
        emit->token_index = @6.first_column;
        emit->size = 0; emit->content = NULL;
        s->type = ast_algorithm_rule;
        s->token_index = @4.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $3; s->content[0]->topic = s;
        s->content[1] = NULL;
        s->content[2] = name; name->topic = s;
        s->content[3] = emit; emit->topic = s;
        $$ = s;
    }
    | IDENTIFIER COLON pattern EMIT_TERMINAL {
        syntax_store *s = Syntax.push();
        syntax_store *name = Syntax.push();
        name->type = ast_rule_name;
        name->token_index = @1.first_column;
        name->size = 0; name->content = NULL;
        s->type = ast_algorithm_rule;
        s->token_index = @4.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $3; s->content[0]->topic = s;
        s->content[1] = NULL;
        s->content[2] = name; name->topic = s;
        s->content[3] = NULL;
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
algorithm_call
    : IDENTIFIER LPAREN STRING_LITERAL RPAREN {
        /* swap("□□■") — literal word input
           content[0] = algorithm name (ast_variable)
           content[1] = word literal (ast_word_literal) */
        syntax_store *s = Syntax.push();
        syntax_store *name = Syntax.push();
        name->type = ast_variable;
        name->token_index = @1.first_column;
        name->size = 0; name->content = NULL;
        syntax_store *arg = Syntax.push();
        arg->type = ast_word_literal;
        arg->token_index = @3.first_column;
        arg->size = 0; arg->content = NULL;
        s->type = ast_algorithm_call;
        s->token_index = @1.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = name; name->topic = s;
        s->content[1] = arg; arg->topic = s;
        $$ = s;
    }
    | IDENTIFIER LPAREN IDENTIFIER RPAREN {
        /* swap(w) — word variable reference
           content[0] = algorithm name (ast_variable)
           content[1] = variable reference (ast_variable) */
        syntax_store *s = Syntax.push();
        syntax_store *name = Syntax.push();
        name->type = ast_variable;
        name->token_index = @1.first_column;
        name->size = 0; name->content = NULL;
        syntax_store *arg = Syntax.push();
        arg->type = ast_variable;
        arg->token_index = @3.first_column;
        arg->size = 0; arg->content = NULL;
        s->type = ast_algorithm_call;
        s->token_index = @1.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = name; name->topic = s;
        s->content[1] = arg; arg->topic = s;
        $$ = s;
    }
    | IDENTIFIER LPAREN TILDE RPAREN {
        /* swap(~) — stdin input
           content[0] = algorithm name (ast_variable)
           content[1] = NULL (signals stdin) */
        syntax_store *s = Syntax.push();
        syntax_store *name = Syntax.push();
        name->type = ast_variable;
        name->token_index = @1.first_column;
        name->size = 0; name->content = NULL;
        s->type = ast_algorithm_call;
        s->token_index = @1.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = name; name->topic = s;
        s->content[1] = NULL;
        $$ = s;
    }
    | IDENTIFIER LPAREN algorithm_call RPAREN {
        /* sort(reverse("□□■")) — composed algorithm call
           content[0] = outer algorithm name (ast_variable)
           content[1] = inner algorithm call (ast_algorithm_call) */
        syntax_store *s = Syntax.push();
        syntax_store *name = Syntax.push();
        name->type = ast_variable;
        name->token_index = @1.first_column;
        name->size = 0; name->content = NULL;
        s->type = ast_algorithm_call;
        s->token_index = @1.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        s->content[0] = name; name->topic = s;
        s->content[1] = (syntax_store *) $3; s->content[1]->topic = s;
        $$ = s;
    }
    ;
bind_rules_value
    : LCURL bind_rules_list RCURL {
        /* {a:□, b:■} — bind rules as a standalone value */
        $$ = $2;
    }
    ;
bind_expression
    : r_expression BIND r_expression {
        /* [2] :> {□, ■} — universal bind
           content[0] = source alphabet (abstract or concrete)
           content[1] = target alphabet
           content[2] = NULL (universal, no rules) */
        syntax_store *s = Syntax.push();
        s->type = ast_bind_expression;
        s->token_index = @2.first_column;
        s->size = 3;
        s->content = malloc(sizeof(syntax_store *) * 3);
        s->content[0] = (syntax_store *) $1; s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $3; s->content[1]->topic = s;
        s->content[2] = NULL;
        $$ = s;
    }
    | r_expression COLON LBRACKET bind_rules_list RBRACKET RANGLE r_expression {
        /* [2] :[a:□, b:■]> {□, ■} — specified bind
           content[0] = source alphabet
           content[1] = target alphabet
           content[2] = bind rules list */
        syntax_store *s = Syntax.push();
        s->type = ast_bind_expression;
        s->token_index = @2.first_column;
        s->size = 3;
        s->content = malloc(sizeof(syntax_store *) * 3);
        s->content[0] = (syntax_store *) $1; s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $7; s->content[1]->topic = s;
        s->content[2] = (syntax_store *) $4; s->content[2]->topic = s;
        $$ = s;
    }
    ;
bind_rules_list
    : bind_rule {
        syntax_store *s = Syntax.push();
        s->type = ast_bind_rules_list;
        s->size = 1;
        s->content = malloc(sizeof(syntax_store *));
        s->content[0] = (syntax_store *) $1;
        s->content[0]->topic = s;
        $$ = s;
    }
    | IDENTIFIER {
        /* r — named bind rules variable reference.
           Wraps IDENTIFIER as a single-entry bind_rules_list with ast_variable.
           Context layer resolves this to the named bind rules. */
        syntax_store *s = Syntax.push();
        s->type = ast_bind_rules_list;
        s->size = 1;
        s->content = malloc(sizeof(syntax_store *));
        syntax_store *var = Syntax.push();
        var->type = ast_variable;
        var->token_index = @1.first_column;
        var->size = 0; var->content = NULL;
        s->content[0] = var; var->topic = s;
        $$ = s;
    }
    | bind_rules_list COMMA bind_rule {
        syntax_store *list = (syntax_store *) $1;
        syntax_store *rule = (syntax_store *) $3;
        list->size += 1;
        list->content = realloc(list->content, sizeof(syntax_store *) * list->size);
        list->content[list->size - 1] = rule;
        rule->topic = list;
        $$ = list;
    }
    ;
bind_rule
    : IDENTIFIER COLON IDENTIFIER {
        /* a:□ — map abstract position to concrete letter */
        syntax_store *s = Syntax.push();
        s->type = ast_bind_rule;
        s->token_index = @1.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        syntax_store *src = Syntax.push();
        src->type = ast_variable; src->token_index = @1.first_column;
        src->size = 0; src->content = NULL;
        syntax_store *tgt = Syntax.push();
        tgt->type = ast_variable; tgt->token_index = @3.first_column;
        tgt->size = 0; tgt->content = NULL;
        s->content[0] = src; src->topic = s;
        s->content[1] = tgt; tgt->topic = s;
        $$ = s;
    }
    | IDENTIFIER COLON {
        /* △: — inert (letter passes through unchanged) */
        syntax_store *s = Syntax.push();
        s->type = ast_bind_rule;
        s->token_index = @1.first_column;
        s->size = 1;
        s->content = malloc(sizeof(syntax_store *));
        syntax_store *src = Syntax.push();
        src->type = ast_variable; src->token_index = @1.first_column;
        src->size = 0; src->content = NULL;
        s->content[0] = src; src->topic = s;
        $$ = s;
    }
    | IDENTIFIER PERIOD IDENTIFIER {
        /* c.b — collapse (c maps to same as b) */
        syntax_store *s = Syntax.push();
        s->type = ast_bind_rule;
        s->token_index = @2.first_column;
        s->size = 2;
        s->content = malloc(sizeof(syntax_store *) * 2);
        syntax_store *src = Syntax.push();
        src->type = ast_variable; src->token_index = @1.first_column;
        src->size = 0; src->content = NULL;
        syntax_store *tgt = Syntax.push();
        tgt->type = ast_variable; tgt->token_index = @3.first_column;
        tgt->size = 0; tgt->content = NULL;
        s->content[0] = src; src->topic = s;
        s->content[1] = tgt; tgt->topic = s;
        $$ = s;
    }
    | EXCLAIM IDENTIFIER {
        /* !d — error (halt if encountered) */
        syntax_store *s = Syntax.push();
        s->type = ast_bind_rule;
        s->token_index = @1.first_column;
        s->size = 1;
        s->content = malloc(sizeof(syntax_store *));
        syntax_store *src = Syntax.push();
        src->type = ast_variable; src->token_index = @2.first_column;
        src->size = 0; src->content = NULL;
        s->content[0] = src; src->topic = s;
        $$ = s;
    }
    ;
equivalence_statement
    : algorithm_name RULE_EQ algorithm_name {
        /* sort ::= bsort — rule equivalence
           content[0] = left, content[1] = right, content[2] = range, content[3] = emit */
        syntax_store *s = Syntax.push();
        s->type = ast_equivalence;
        s->token_index = @2.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $1; s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $3; s->content[1]->topic = s;
        s->content[2] = NULL;
        s->content[3] = NULL;
        $$ = s;
    }
    | algorithm_name RULE_EQ algorithm_name COLON STRING_LITERAL {
        /* sort ::= bsort : "~result" — with emit */
        syntax_store *s = Syntax.push();
        syntax_store *emit = Syntax.push();
        emit->type = ast_emit_expression;
        emit->token_index = @5.first_column;
        emit->size = 0; emit->content = NULL;
        s->type = ast_equivalence;
        s->token_index = @2.first_column;
        s->size = 4;
        s->content = malloc(sizeof(syntax_store *) * 4);
        s->content[0] = (syntax_store *) $1; s->content[0]->topic = s;
        s->content[1] = (syntax_store *) $3; s->content[1]->topic = s;
        s->content[2] = NULL;
        s->content[3] = emit; emit->topic = s;
        $$ = s;
    }
    ;

/* Bounded equivalence forms (sort ::[r]~ bsort) are in the
   algorithm production since they share the algorithm_name DOUBLE_COLON
   prefix. See the algorithm production above. */
