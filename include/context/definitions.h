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

struct pcontext;
typedef struct aliteral {

   syntax_store     *store;
   struct pcontext  *context;

   /* List of pointers to other alphabets known to this alphabet. */
   size_t            alphabets_count;
   struct aliteral **alphabets_literals;

   /* List of pointers to lexical stores containing letters that 
      alphabets known to this alphabet use. */
   size_t            letters_count; 
   size_t          **letters; 
   /* A binary format of the letters. 1 marks inclucsion in this 
      alphabet. */
   
   size_t            letters_bytes_count;
   uint8_t         **letters_bytes;

   /* Bitmask representing which letters from the context are in this
      alphabet. Bit i is set if context->letters[i] is in this alphabet. */
   uint64_t          letter_mask;

} alphabet_literal;

/* A single substitution rule in a Markov algorithm.
   Rules have form: P -> Q (substitution) or P -. (terminal) */
typedef struct arule {
    syntax_store *store;           /* AST node for this rule */
    syntax_store *pattern;         /* Left side pattern (P) */
    syntax_store *replacement;     /* Right side replacement (Q), NULL if terminal */
    bool          is_terminal;     /* True if this is a terminal rule (P -.) */
} algorithm_rule;

/* Definition of a Markov algorithm.
   Syntax: name::alphabet (word_param) { rules } */
typedef struct adef {
    syntax_store     *store;       /* AST node (ast_algorithm) */
    struct pcontext  *context;     /* Context this algorithm is defined in */

    /* Algorithm name (from store->content[0]) */
    lexical_store    *name;

    /* Reference to alphabet the algorithm operates over */
    syntax_store     *alphabet_ref;    /* AST variable node */
    alphabet_literal *alphabet;        /* Resolved alphabet, NULL until resolved */

    /* Word parameter that the algorithm accepts */
    lexical_store    *word_param;

    /* List of substitution rules */
    size_t            rules_count;
    size_t            rules_capacity;
    algorithm_rule  **rules;
} algorithm_definition;

typedef struct {
    size_t                  count;
    size_t                  capacity;
} algorithm_definition_info;

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

    size_t                   algorithms_count;
    size_t                   algorithms_capacity;
    algorithm_definition   **algorithms;
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
