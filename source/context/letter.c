#include "context.h"

#define CONTEXT_LETTERS_SIZE 64

lexical_store *_context_push_letter(
    program_context *context, 
    lexical_store   *letter);

bool _context_has_letter(
    program_context *context, 
    lexical_store   *letter);

syntax_store *_update_context_letter(
    syntax_store         *store,
    program_context_info *info, 
    program_context      *context) {

    syntax_store  *topic = store;
    lexical_store *letter;
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
            printf("Error. Could not determine letter's topic "
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
        printf("Error. Could not find matching context for letter.\n");
        // TODO: Gracefully handle errors... 
    
    /* Add letter to the current context if the context does not 
       already have that letter. */
    letter = Lex.store(store->token_index);
    if (!_context_has_letter(&context[index], letter)) {
    
        _context_push_letter(&context[index], letter);

        for (size_t i = 0; i < context[index].content_count; ++i) {
            _update_known_context_letter(letter, context[index].content[i]);
        } 
    } 

    return NULL;
}

syntax_store *_update_known_context_letter(
    lexical_store   *letter,
    program_context *context) {

    // TODO: Short circuit if we have the wrong capture type. I would
    //       like a better way to do this though.
    if (context->capture == capture_pure) {
        return NULL;
    }

    if (!_context_has_letter(context, letter)) {
        _context_push_letter(context, letter);

        for (size_t i = 0; i < context->content_count; ++i) {
            _update_known_context_letter(letter, context->content[i]);
        } 
    }

    return NULL;
}

lexical_store *_context_push_letter(
    program_context *context, 
    lexical_store   *letter) {

    size_t bytes;
    if (context->letters_count == context->letters_capacity) {
        context->letters_capacity += CONTEXT_LETTERS_SIZE;

        bytes = sizeof(*context->letters) * context->letters_capacity;
        context->letters = 
            (lexical_store **) realloc(context->letters, bytes); 
    }

    context->letters[context->letters_count] = letter;
    context->letters_count += 1;
    return *(context->letters + context->letters_count - 1);
}

// TODO: Write a clean up / free function for the abovee. ^^ 

bool _context_has_letter(
    program_context *context, 
    lexical_store   *letter) {

    lexical_store *current;
    size_t size;
    bool match;
    
    size  = letter->end - letter->begin;
    match = false;
    for (size_t i = 0; i < context->letters_count; ++i) {
        
        current = context->letters[i];

        if (size != letter->end - letter->begin) continue; 

        
        if (memcmp(letter->begin, current->begin, size) == 0) {
            match = true;
            break;
        }
    }

    return match;
}