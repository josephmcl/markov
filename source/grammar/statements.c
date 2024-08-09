#include "grammar/statements.h"

syntax_store *statements_root(void) {

    /* Create and setup the program node */
    syntax_store *statements = Syntax.push();
    statements->type = ast_statements;
    statements->size = 0;

    return statements;
}

syntax_store *statements_statements_statementz(
    syntax_store *statements, 
    syntax_store *statementz) {
    
    /* Store the current statements size. */
    size_t oldsize = statements->size;

    /* Add the size of the statement groups together. */
    statements->size += statementz->size;

    /* Recallocate space for both statement groups. */
    size_t bytes = statements->size * sizeof(syntax_store *);
    statements->content = (syntax_store **) realloc(
        statements->content, bytes);

    /* Set all of the topic/content pointers. */
    for (size_t i = oldsize; i < statements->size; ++i) {
        statements->content[i] = statementz->content[i - oldsize];
        statements->content[i]->topic = statements;
    }

    /* Set the prune flag of the statementz node. */
    statementz->prune = true;

    return statements;
}

syntax_store *statements_statements_statementz_PERIOD(
    syntax_store *statements, 
    syntax_store *statementz) {
    
    /* Store the current statements size. */
    size_t oldsize = statements->size;

    /* Add the size of the statement groups together. */
    statements->size += statementz->size;

    /* Recallocate space for both statement groups. */
    size_t bytes = statements->size * sizeof(syntax_store *);
    statements->content = (syntax_store **) realloc(
        statements->content, bytes);

    /* Set all of the topic/content pointers. */
    for (size_t i = oldsize; i < statements->size; ++i) {
        statements->content[i] = statementz->content[i - oldsize];
        statements->content[i]->topic = statements;
    }

    /* Set the prune flag of the statementz node. */
    statementz->prune = true;

    return statements;
}



syntax_store *statements_statements_scope(
    syntax_store *statements, 
    syntax_store *scope) {
    
    /* Add the size of the statement groups together. */
    statements->size += 1;

    /* Recallocate space for both statement groups. */
    size_t bytes = statements->size * sizeof(syntax_store *);
    statements->content = (syntax_store **) realloc(
        statements->content, bytes);

    /* Set all the topic/content pointers. */
    statements->content[statements->size - 1] = scope;
    scope->topic = statements;

    return statements;
}
