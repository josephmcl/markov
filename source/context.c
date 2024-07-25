#include "context.h"

#define PROGRAM_CONTEXT_SIZE 64

program_context *TheContext = NULL;

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
    current->letters_count = 0;
    current->letters_capacity = 0;
    bool found = false;

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
    
    // TODO: Will need better error propagation at some point. 
    if (TheInfo.count > 1 && !found) {
        printf("Error. Topic program context could not be found in "
               "AST.\n");
    }

    syntax_store *temp = NULL;
    if (TheInfo.count > 1) temp = topic->syntax;
    printf("(%p) -- (%p)\n", (void *)  current->syntax, (void *) temp);
    /* Set the topic of the current context. */
    current->topic = topic;

    // TODO: Add the current scope to the topic's content pointer.
    //       Needs dynamic memory and I don't want to do it right now.


    return NULL;
}

syntax_store * _update_context_alphabet_body(syntax_store *store) {

    syntax_store *context = store->topic->topic;
    size_t index = TheInfo.count;

    for (size_t i = 0; i < TheInfo.count; ++i) {
        if (context == TheContext[i].syntax) {
            index = i;
            break;
        }
    }

    printf("Alphabet %lu context (%p)\n", index, (void *) TheContext[index].syntax);

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
    case ast_letter:
        return _update_context_letter(store, &TheInfo, TheContext);
    case ast_alphabet_body:
        return _update_context_alphabet_body(store);
    default: 
        return NULL; }
}

void validate_program_context (void) {

    lexical_store *lstore;
    syntax_store *tree, *current;
    program_context *topic = NULL;
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
        printf("%lu current (%p) ", i, (void *) TheContext[i].syntax);
        topic = TheContext[i].topic;
        if (topic == NULL) {
            printf("(root context).\n");
        }
        else {
            printf("topic (%p) ", (void *) topic->syntax);
            current = TheContext[i].syntax->content[1];
            if (current == NULL) {
                printf("anonymous\n");
            }
            else {
                lstore = Lex.store(current->token_index);
                int size = (int) (lstore->end - lstore->begin);
                printf("name (%.*s)\n", size, lstore->begin);
            }
        }
    }
    return;
}

const struct context Context = {
    .validate = validate_program_context
};
