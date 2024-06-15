#include "syntax.h"

extern int yyparse (void);

#define NODES_SIZE 64

static syntax_info TheInfo = {0};
static syntax_store *TheTree;

/* Invoke bison LALR1 parser. */
int syntax_parse(void) {
    return yyparse();
}

/* Allocate a new node in the ast. Return a pointer to the location. 
   If memory has already been allocated, do nothing. Otherwise, add 
   storage for more nodes. */
syntax_store *syntax_push(void) {

    size_t bytes;
    if (TheInfo.count == TheInfo.capacity) {
        TheInfo.capacity += NODES_SIZE;

        bytes = sizeof(syntax_store) * TheInfo.capacity;
        TheTree = (syntax_store *) realloc(TheTree, bytes); 
    }

    TheInfo.count += 1;
    return TheTree + TheInfo.count;
}

void syntax_free(void) {
    free(TheTree);
}

const struct syntax Syntax = {
    .parse = syntax_parse,
    .push = syntax_push,
    .free = syntax_free
};
