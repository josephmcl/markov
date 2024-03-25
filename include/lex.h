#pragma once 

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "codepoint.h"
#include "token.h"

typedef lexical_store *lexical_stores;
typedef struct {
    int count;
    int capacity;
    unsigned int rows;
    unsigned int columns;
    unsigned int line_length;
    int index; // TODO remove ? 
    uint8_t *current; 
    int state;
    uint8_t *prior_newline;
} lexical_info;

struct lex {
    file_info *file;
    lexical_info *info;
    lexical_token ( *token) (size_t index);
    lexical_store *( *store) (size_t index);
    int ( *read)(const char *path);
    void ( *free)();
    int ( *analyze)();
    void ( *print)();
};

extern const struct lex Lex;
