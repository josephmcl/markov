#include "context.h"

#define ALPHABET_LITERALS_SIZE 64
#define CONTEXT_ALPHABET_LITERALS_SIZE 64

alphabet_literal *TheAlphabetLiteral = NULL;

static alphabet_literal_info TheInfo = {0};

alphabet_literal *alphabet_literal_push(void) {

    size_t bytes;
    if (TheInfo.count == TheInfo.capacity) {
        TheInfo.capacity += ALPHABET_LITERALS_SIZE;

        bytes = sizeof(alphabet_literal) * TheInfo.capacity;
        TheAlphabetLiteral = 
            (alphabet_literal *) realloc(TheAlphabetLiteral, bytes); 
    }

    TheInfo.count += 1;
    return TheAlphabetLiteral + TheInfo.count - 1;
}

alphabet_literal *alphabet_literal_current(void) {
    return &TheAlphabetLiteral[TheInfo.count];
}

alphabet_literal *_context_push_alphabet_literal(
    program_context *context, 
    alphabet_literal   *alphabet);

bool _context_has_alphabet_literal(
    program_context *context, 
    alphabet_literal   *alphabet);

syntax_store *_update_context_alphabet_literal(
    syntax_store         *store,
    program_context_info *info, 
    program_context      *context) {

    syntax_store  *topic = store;
    alphabet_literal *alphabet;
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
            printf("Error. Could not determine alphabet's topic "
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
               "alphabet.\n");
        // TODO: Gracefully handle errors... 

    /* Add variable to the current context if the context does not 
       already have that variable. */

    alphabet_literal_push();
    alphabet = alphabet_literal_current();

    alphabet->store = store;

    if (!_context_has_alphabet_literal(&context[index], alphabet)) {
    
        _context_push_alphabet_literal(&context[index], alphabet);

        for (size_t i = 0; i < context[index].content_count; ++i) {
            //_update_known_context_alphabet_literal(
            //    alphabet, 
            //    context[index].content[i]);

            // TODO TODO TODO: What to do here? 
        } 
    } 
   
    return NULL;
}

alphabet_literal *_context_push_alphabet_literal(
    program_context *context, 
    alphabet_literal   *alphabet) {
        

    size_t bytes;
    if (context->alphabet_literals_count == context->alphabet_literals_capacity) {
        context->alphabet_literals_capacity += CONTEXT_ALPHABET_LITERALS_SIZE;

        bytes = sizeof(*context->alphabet_literals) * context->alphabet_literals_capacity;
        context->alphabet_literals = 
            (alphabet_literal **) realloc(context->alphabet_literals, bytes); 
    }

    context->alphabet_literals[context->alphabet_literals_count] = alphabet;
    context->alphabet_literals_count += 1;
    return *(context->alphabet_literals + context->alphabet_literals_count - 1);
}

// TODO: Write a clean up / free function for the abovee. ^^ 

bool _context_has_alphabet_literal(
    program_context *context, 
    alphabet_literal   *alphabet) {

    alphabet_literal *current;
    bool match;
    
    match = false;
    for (size_t i = 0; i < context->alphabet_literals_count; ++i) {
        
        current = context->alphabet_literals[i];

        if (current == alphabet) {
            match = true;
            break;
        }
    }

    return match;
}

alphabet_literal *_update_known_context_alphabet_literal(
    alphabet_literal   *alphabet,
    program_context *context) {
    
    // TODO: Short circuit if we have the wrong capture type. I would
    //       like a better way to do this though.
    if (context->capture == capture_pure) {
        return NULL;
    }

    if (!_context_has_alphabet_literal(context, alphabet)) {
        _context_push_alphabet_literal(context, alphabet);

        for (size_t i = 0; i < context->content_count; ++i) {
            _update_known_context_alphabet_literal(
                alphabet, 
                context->content[i]);
        } 
    }

    return NULL;
}