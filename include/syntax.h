#pragma once 

#include "lex.h"
#include "bison.h"

typedef struct {
    int count;
    int capacity;
} syntax_info;

typedef struct store {
    size_t token_index;
    lexical_store *token;
    size_t size;
    struct store *topic;
    struct store *content;
} syntax_store;

struct syntax {
    int           ( *parse) (void);
    syntax_store *( *push)  (void);
    void          ( *free)  (void);
};

extern const struct syntax Syntax;
