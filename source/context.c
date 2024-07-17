#include "context.h"

#define PROGRAM_CONTEXT_SIZE 64

/* Struct containing the context of the program's scope. Including 
   variable names and compile-time data known only to this scope. */
typedef struct pcontext {
    /* Name associated with the scope. Optional, NULL if unnamed. */
    uint8_t *name; 
    /* Syntax tree node that the scope is associated with. */
    syntax_store *syntax;
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
    size_t         count;
    size_t         capacity;
    syntax_store **syntax_stack;
    size_t         syntax_count;
    size_t         syntax_capacity;
} program_context_info;

static program_context_info TheInfo = { 0 };

program_context *context_push(void) {

    size_t bytes;
    if (TheInfo.count == TheInfo.capacity) {
        TheInfo.capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(program_context) * TheInfo.capacity;
        TheContext = (program_context *) realloc(TheContext, bytes); 
    }

    TheInfo.count += 1;
    return TheContext + TheInfo.count - 1;
}

program_context *context_root(void) {
    return TheContext;
}

program_context *context_current(void) {
    return &TheContext[TheInfo.count];
}

syntax_store *_context_syntax_push(syntax_store *s) {

    size_t bytes;
    if (TheInfo.syntax_count == TheInfo.syntax_capacity) {
        TheInfo.syntax_capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(syntax_store *) * TheInfo.syntax_capacity;
        TheInfo.syntax_stack = (syntax_store **) realloc(TheInfo.syntax_stack, bytes); 
    }

    TheInfo.syntax_stack[TheInfo.syntax_count] = s;

    TheInfo.syntax_count += 1;
    return *(TheInfo.syntax_stack + TheInfo.syntax_count);
}

syntax_store *_context_syntax_pop(void) {

    size_t bytes;
    if (TheInfo.syntax_count == TheInfo.syntax_capacity) {
        TheInfo.syntax_capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(syntax_store *) * TheInfo.syntax_capacity;
        TheInfo.syntax_stack = (syntax_store **) realloc(TheInfo.syntax_stack, bytes); 
    }
    
    TheInfo.syntax_count -= 1;
    return *(TheInfo.syntax_stack + TheInfo.syntax_count);
}

syntax_store *_update_context_scope(syntax_store *store) {
    
    /* Add a new program context to the stack. */
    program_context *current = context_push();
    current->syntax = store;

    /* Determine the topic context. */
    program_context *topic;

    /* If there is only one context, it's the global context. We're
       done. */
    if (TheInfo.count <= 1) { 
        topic = NULL;
    }

    /* Otherwise check prior contexts for the nearest context that  
       contains the current context. */
    else {
        syntax_store *statements;
        bool found = false;
        for (size_t i = 1; i != TheInfo.count; ++i) {
        
            /* Get the next context. */
            topic = current - i;     

            /* NOTE: This probably shouldn't be possible, but let's 
                     check anyways. */
            if (topic->syntax->size == 0) continue; 

            /* Look through the syntax tree held by the current 
               context. */
            statements = topic->syntax->content[0];

            /* Continue if the tree is empty. */
            if (statements->size == 0) continue;

            /* Otherwise check if any of the syntax tree nodes in this 
               context are the current current context. */
            for (size_t j = 0; j != statements->size; ++j) {
                if (statements->content[j] == store) {
                    
                    /* Trigger the success variable for the outer 
                       loop. */
                    found = true;
                    break;
                }
            }

            /* Halt if we found a match. */
            if (found) break;
        }

        // TODO: Maybe this can be improved in the future with better 
        //       parsing infrastructure. 
    }
    
    /* Set the topic of the current context. */
    current->topic = topic;

    // TODO: Add the current scope to the topic's content pointer.
    //       Needs dynamic memory and I don't want to do it right now.


    return NULL;
}

syntax_store * _update_context_alphabet_body(syntax_store *store) {

    return NULL;
}

syntax_store *update_program_context(syntax_store *store) {
    switch (store->type) { 
    case ast_statements:
        return NULL;
    case ast_program:
        return _update_context_scope(store);
    case ast_scope:
        return _update_context_scope(store);
    case ast_alphabet_body:
        return _update_context_alphabet_body(store);
    default: 
        return NULL; }
}

void validate_program_context (void) {

    lexical_store *lstore;
    syntax_store *tree, *current, *topic;
    tree = Syntax.tree();
    for (size_t i = 0; i < Syntax.info->count; ++i) {
        current = tree - i;
        if (current != NULL) {
            // _print_node_string(current->type);
            update_program_context(current);
        }
    }

    

    // TODO: Remove.
    for (size_t i = 0; i < TheInfo.count; ++i) {
        printf("%d current (%p) ", i, &TheContext[i]);
        topic = TheContext[i].topic;
        if (topic == NULL) {
            printf("(root context).\n");
        }
        else {
            current = TheContext[i].syntax->content[1];
            if (current == NULL) {
                printf("topic (%p) anonymous\n", topic);
            }
            else {
                lstore = Lex.store(current->token_index);
                printf("topic (%p) name (%.*s)\n", topic,
                    (int) (lstore->end - lstore->begin), lstore->begin);
            }
        }
    }
    return;
}

const struct context Context = {
    .validate = validate_program_context
};
