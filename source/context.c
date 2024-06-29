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

typedef struct {
    size_t           count;
    size_t           capacity;
    program_context *context;
} program_context_info;

static program_context_info TheContext = { 0 };

program_context *context_push(void) {

    size_t bytes;
    program_context *context = TheContext.context;
    if (TheContext.count == TheContext.capacity) {
        TheContext.capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(program_context) * TheContext.capacity;
        context = (program_context *) realloc(context, bytes); 
    }

    TheContext.count += 1;
    return context + TheContext.count;
}

void _update_context_scope(syntax_store *store) {
    
    context_push();

    return;
}

void _update_context_alphabet_body(syntax_store *store) {

    return;
}

void update_program_context(syntax_store *store) {
    switch (store->type) { 
    case ast_scope:
        _update_context_scope(store);
    default: return; }
    switch (store->type) { 
    case ast_alphabet_body:
        _update_context_alphabet_body(store);
    default: return; }
    return;
}

void validate_program_context (void) {

}

const struct context Context = {
    .validate = validate_program_context
};
