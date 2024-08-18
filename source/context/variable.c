#include "context.h"

#define CONTEXT_VARIABLES_SIZE 64

lexical_store *_context_push_variable(
    program_context *context, 
    lexical_store   *variable);

bool _context_has_variable(
    program_context *context, 
    lexical_store   *variable);

syntax_store *_update_context_variable(
    syntax_store         *store,
    program_context_info *info, 
    program_context      *context) {

    syntax_store  *topic = store;
    lexical_store *variable;
    size_t         index;

    /* For the given syntax tree node, nagvigate the topic nodes 
       until a scope or program node is found. */
    while (
        topic != NULL /* NOTE: This should not be possible. */
        && topic->type != ast_scope 
        && topic->type != ast_program) {

        if (topic != NULL) {
            topic = topic->topic;
        }
        else {
            printf("Error. Could not determine variables's topic "
                   "scope.\n");
            break; // TODO: Gracefully handle this. 
        }
    }

    /* Find the matching syntax tree node associated with existing 
       contexts. */
    index = info->count;
    for (size_t i = 0; i < info->count; ++i) {
        if (topic == context[i].syntax) {
            index = i;
            break;
        }
    }
    
    if (index == info->count) 
        printf("Error. Could not find matching context for "
               "variable.\n");
        // TODO: Gracefully handle errors... 
    
    /* Add variable to the current context if the context does not 
       already have that variable. */
    variable = Lex.store(store->token_index);
    if (!_context_has_variable(&context[index], variable)) {
    
        _context_push_variable(&context[index], variable);

        for (size_t i = 0; i < context[index].content_count; ++i) {
            _update_known_context_variable(
                variable, 
                context[index].content[i]);
        } 
    } 

    return NULL;
}

syntax_store *_update_known_context_variable(
    lexical_store   *variable,
    program_context *context) {

    if (!_context_has_variable(context, variable)) {
        _context_push_variable(context, variable);

        for (size_t i = 0; i < context->content_count; ++i) {
            _update_known_context_variable(
                variable, 
                context->content[i]);
        } 
    }

    return NULL;
}

lexical_store *_context_push_variable(
    program_context *context, 
    lexical_store   *variable) {

    size_t bytes;
    if (context->variables_count == context->variables_capacity) {
        context->variables_capacity += CONTEXT_VARIABLES_SIZE;

        bytes = sizeof(*context->variables) * context->variables_capacity;
        context->variables = 
            (lexical_store **) realloc(context->variables, bytes); 
    }

    context->variables[context->variables_count] = variable;
    context->variables_count += 1;
    return *(context->variables + context->variables_count - 1);
}

// TODO: Write a clean up / free function for the abovee. ^^ 

bool _context_has_variable(
    program_context *context, 
    lexical_store   *variable) {

    lexical_store *current;
    size_t size;
    bool match;
    
    size  = variable->end - variable->begin;
    match = false;
    for (size_t i = 0; i < context->variables_count; ++i) {
        
        current = context->variables[i];

        if (size != variable->end - variable->begin) continue; 

        
        if (memcmp(variable->begin, current->begin, size) == 0) {
            match = true;
            break;
        }
    }

    return match;
}