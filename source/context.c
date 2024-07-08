#include "context.h"

#define PROGRAM_CONTEXT_SIZE 64

/* Struct containing the context of the program's scope. Including 
   variable names and compile-time data known only to this scope. */
typedef struct pcontext {
    /* Name associated with the scope. Optional, NULL if unnamed. */
    uint8_t *name; 
    /* Scope that the current scope is defined within. NULL if the 
       this scope is the top-level/global scope. */
    struct pcontext *topic;

    /* Scopes that have been imported into the current scope. 
       TODO: implement. */
    struct pcontext **imports;

    /* True if the scope is to visible to external programs, false 
       otherwise. TODO: implement. */
    bool exported;
} program_context;

program_context *TheContext;

typedef struct {
    size_t           count;
    size_t           capacity;
    syntax_store *syntax_stack;
    size_t        syntax_count;
    size_t        syntax_capacity;
} program_context_info;

static program_context_info TheInfo = { 0 };

program_context *context_push(void) {

    size_t bytes;
    if (TheInfo.count == TheContext.capacity) {
        TheInfo.capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(program_context) * TheInfo.capacity;
        TheContext = (program_context *) realloc(context, bytes); 
    }

    TheInfo.count += 1;
    return TheContext + TheInfo.count;
}

syntax_store *syntax_push(syntax_store *s) {

    size_t bytes;
    if (TheInfo.syntax_count == TheContext.syntax_capacity) {
        TheInfo.syntax_capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(syntax_store) * TheInfo.syntax_capacity;
        TheContext = (syntax_store *) realloc(context, bytes); 
    }

    TheInfo.syntax_stack + TheInfo.syntax_count = s;

    TheInfo.syntax_count += 1;
    return TheInfo.syntax_stack + TheInfo.syntax_count;
}

syntax_store *syntax_pop(void) {

    size_t bytes;
    if (TheInfo.syntax_count == TheContext.syntax_capacity) {
        TheInfo.syntax_capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(syntax_store) * TheInfo.syntax_capacity;
        TheContext = (syntax_store *) realloc(context, bytes); 
    }

    TheInfo.syntax_stack + TheInfo.syntax_count = s;
    
    TheInfo.syntax_count += 1;
    return TheInfo.syntax_stack + TheInfo.syntax_count;
}

void _update_context_scope(syntax_store *store) {
    
    context_push();

    return;
}

void _update_context_alphabet_body(syntax_store *store) {

    return;
}

syntax_store *update_program_context(syntax_store *store) {
    switch (store->type) { 
    case ast_statements:
        return _update_context_scope(store);
    case ast_scope:
        return _update_context_scope(store);
    case ast_alphabet_body:
        return _update_context_alphabet_body(store);
    default: 
        return NULL; }
}

void validate_program_context (void) {

    syntax_store *s = Sytnax.tree;

    for (size_t i = 0; i < Syntax.info.count; ++i) {
        
        if (s != NULL) {
            s = update_program_context(s[i]);
        }
    }

    return;
}

const struct context Context = {
    .validate = validate_program_context
};
