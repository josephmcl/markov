#pragma once 

#include "string.h"

#include "lex.h"
#include "bison.h"


typedef enum {
    ast_program,
    ast_statement,
    // statement types
    ast_assignment,
    ast_l_expression,
    ast_r_expression,
    ast_alphabet_body,
    ast_letters,
    ast_letter
} syntax_store_type;

typedef struct {
    int count;
    int capacity;
} syntax_info;

typedef struct sstore {
    syntax_store_type type; 
    size_t token_index;
    size_t size;
    struct sstore **topic;
    struct sstore **content;
    uint8_t prune;
} syntax_store;

struct syntax {
    int           ( *parse) (void);
    syntax_store *( *push)  (void);
    void          ( *check) (syntax_store *);
    void          ( *free)  (void);
};

extern const struct syntax Syntax;
