#pragma once 

#include "stdint.h"

#include "lex.h"
#include "syntax.h"

typedef enum {
   capture_pure    = 0,
   capture_parent  = 1,
   capture_letters = 2,
   capture_list    = 3 // TODO: Support this.
} context_capture;

typedef struct aliteral {

   syntax_store         *store;

   /* List of pointers to other alphabets known to this alphabet. */
   size_t            alphabets_count;
   struct aliteral **alphabets_literals;

   /* List of pointers to lexical stores containing letters that 
      alphabets known to this alphabet use. */
   size_t            letters_count;
   /* NOTE: Letters is allocated once by the program context; all 
            alphabets in this context share the same pointer. */
   lexical_store   **letters;

   /* A binary format of the letters. 1 marks inclucsion in this 
      alphabet. */
   uint8_t         **letters_bytes;

} alphabet_literal;

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

    context_capture capture;

    size_t          letters_count;
    size_t          letters_capacity;
    lexical_store **letters;
    

    size_t          variables_count;
    size_t          variables_capacity;
    lexical_store **variables;

    size_t             alphabet_literals_count;
    size_t             alphabet_literals_capacity;
    alphabet_literal **alphabet_literals;
} program_context;

typedef struct {
   size_t         count;
   size_t         capacity;
   syntax_store **syntax_stack;
   size_t         syntax_count;
   size_t         syntax_capacity;
} alphabet_literal_info;

typedef struct {
    size_t         count;
    size_t         capacity;
    syntax_store **syntax_stack;
    size_t         syntax_count;
    size_t         syntax_capacity;
} program_context_info;
