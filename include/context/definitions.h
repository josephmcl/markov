#pragma once 

#include "lex.h"
#include "syntax.h"

/* Struct containing the context of the program's scope. Including 
   variable names and compile-time data known only to this scope. */
typedef struct pcontext {
    /* Syntax tree node that the scope is associated with. Used to 
       access the scope's name if it has one. */
    syntax_store *syntax;

    /* Scope that the current scope is defined within. NULL if the 
       this scope is the top-level/global scope. */
    struct pcontext *topic;

    /* Scopes that are defined within the current context. */
    size_t            content_count;
    size_t            content_capacity;
    struct pcontext **content;

    /* Scopes that have been imported into the current scope. */
    // TODO: implement.
    struct pcontext **imports;

    /* True if the scope is to visible to external programs, false 
       otherwise. */
    // TODO: implement.
    bool exported;

    size_t          letters_count;
    size_t          letters_capacity;
    lexical_store **letters;
} program_context;

typedef struct {
    size_t         count;
    size_t         capacity;
    syntax_store **syntax_stack;
    size_t         syntax_count;
    size_t         syntax_capacity;
} program_context_info;
