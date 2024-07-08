#pragma once 

#include "string.h"
#include "stdbool.h"

#include "lex.h"
#include "bison.h"
#include "algorithm/memory_compare.h"


typedef enum {
    ast_program,
    ast_scope, 
    ast_statements,
    ast_statement,
    // statement types
    ast_l_expression,
    ast_r_expression,
    ast_variable, 
    ast_assignment_statement, 
    ast_alphabet_body,
    ast_letters,
    ast_letter
} syntax_store_type;

typedef struct {
    size_t count;
    size_t capacity;
} syntax_info;

typedef struct sstore {
    syntax_store_type type; 
    size_t token_index;
    size_t size;
    struct sstore **topic;
    struct sstore **content;
    bool prune;
} syntax_store;

struct syntax {
    syntax_store *   tree;
    syntax_info  *   info;
    int           ( *parse) (void);
    syntax_store *( *push)  (void);
    void          ( *check) (syntax_store *);
    void          ( *print) (void);
    void          ( *free)  (void);
};

extern const struct syntax Syntax;
