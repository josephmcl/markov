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

program_context *_context_push_content(
    program_context *context, 
    program_context *content) {

    size_t bytes;
    if (context->content_count == context->content_capacity) {
        context->content_capacity += 64;

        bytes = sizeof(program_context *) * context->content_capacity;
        context->content = 
            (program_context **) realloc(context->content, bytes); 
    }
    
    context->content[context->content_count] = content;
    context->content_count += 1;
    return *(context->content + context->content_count - 1);
}


syntax_store *_update_context_scope(syntax_store *store) {
    
    /* Add a new program context to the stack. */
    program_context *current = context_push();
    current->syntax = store;
    current->letters_count = 0;
    current->letters_capacity = 0;
    current->content_count = 0;
    current->content_capacity = 0;
    bool found = false;
    lexical_store *letter, *capture_lstore;
    syntax_store *capture_store;

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

    /* Set the topic of the current context. */
    current->topic = topic;

     /* Set the capture of the current context. */
     // TODO: This should be its own function.
    capture_store = current->syntax->content[2];
    if (capture_store == NULL) {
        current->capture = capture_pure;
    } 
    else {
        if (capture_store->type == ast_scope_context_names_literal) {
            capture_lstore = Lex.store(capture_store->token_index);
            if (capture_lstore->token == TOKEN_ATSIGN) {
                current->capture = capture_letters;
            }
            if (capture_lstore->token == TOKEN_EQUAL) {
                current->capture = capture_parent;
            }
        }
    }

    if (topic != NULL) {

        _context_push_content(topic, current);

        if (current->capture != capture_pure) {

            for (size_t i = 0; topic->letters_count; ++i) {
                letter = topic->letters[i];
                _update_known_context_letter(letter, current); 
            }
        }
    }

    // TODO: Propagate variable information
    

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
    case ast_variable:
        return _update_context_variable(store, &TheInfo, TheContext);
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
    printf("\nContext report.\n");

    for (size_t i = 0; i < TheInfo.count; ++i) {
        printf("Context %lu\n| address (%p)\n", i, (void *) TheContext[i].syntax);
        topic = TheContext[i].topic;
        if (topic == NULL) {
            printf("| parent NULL (root context) \n");
        }
        else {      
            printf("| parent  (%p)\n", (void *) topic->syntax);
            current = TheContext[i].syntax->content[1];
            if (current == NULL) {
                printf("| name    (anonymous)\n");
            }
            else {
                lstore = Lex.store(current->token_index);
                int size = (int) (lstore->end - lstore->begin);                        
                printf("| name    (%.*s)\n", size, lstore->begin);
            }
        }
        
        current = TheContext[i].syntax->content[2];
        // lstore = Lex.store(current->token_index);

        if (TheContext[i].capture == capture_pure) {
            printf("| capture (none)\n");
        }
        else if (TheContext[i].capture == capture_letters) {
            printf("| capture (letters)\n");
        }
        else if (TheContext[i].capture == capture_parent) {
            printf("| capture (parent)\n");
        }

        if (current == NULL) {
            printf("| capture (none)\n");
        }
        else if (current->type == ast_scope_context_names_literal) {
            lstore = Lex.store(current->token_index);
            if (lstore->token == TOKEN_ATSIGN) {
                printf("| capture (letters)\n");
            }
            if (lstore->token == TOKEN_EQUAL) {
                printf("| capture (parent)\n");
            }
        }

        printf("| letters (%lu)\n", TheContext[i].letters_count);
        for (size_t j = 0; j < TheContext[i].letters_count; ++j) {
            printf("| | size (%d) ", 
                TheContext[i].letters[j]->end - 
                TheContext[i].letters[j]->begin);
            printf("value (%.*s) ", 
                TheContext[i].letters[j]->end - 
                TheContext[i].letters[j]->begin, 
                TheContext[i].letters[j]->begin);
            printf("address (%p) \n",  
                TheContext[i].letters[j]->begin);
        }
        printf("| variables (%lu)\n", TheContext[i].variables_count);
        for (size_t j = 0; j < TheContext[i].variables_count; ++j) {
            printf("| | size (%d) ", 
                TheContext[i].variables[j]->end - 
                TheContext[i].variables[j]->begin);
            printf("value (%.*s) ", 
                TheContext[i].variables[j]->end - 
                TheContext[i].variables[j]->begin, 
                TheContext[i].variables[j]->begin);
            printf("address (%p) \n",  
                TheContext[i].variables[j]->begin);
        }
        printf("\n");
    }
    return;
}

const struct context Context = {
    .validate = validate_program_context
};
