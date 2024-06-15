#pragma once 

#include "lex.h"

typedef struct store {
    size_t token_index;
    lexical_store *token;
    size_t size;
    struct store *topic;
    struct store *content;
} syntax_store;

struct syntax {
    syntax_store *( *append) (void);
};

extern const struct syntax Syntax;
