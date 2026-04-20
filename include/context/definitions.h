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
    bool          is_terminal;     /* True if this is a terminal rule (-. or ~.) */
    bool          has_emit;        /* True if this is an emit rule (~> or ~.) */
    lexical_store *emit_string;    /* Emit string literal token, NULL if silent/default */
    lexical_store *rule_name;      /* Rule name identifier token, NULL if unnamed */
} algorithm_rule;

/* Abstract alphabet: [N] with positional names a, b, c, ... or [cat, dog] */
#define MAX_ABSTRACT_LETTERS 26
typedef struct {
    size_t size;                              /* number of abstract positions */
    struct {
        const uint8_t *bytes;                 /* pointer to name string */
        size_t len;                           /* length of name */
    } names[MAX_ABSTRACT_LETTERS];
    uint8_t name_storage[MAX_ABSTRACT_LETTERS * 32]; /* backing storage for auto-named */
} abstract_alphabet;

/* Definition of a Markov algorithm.
   Syntax: name::alphabet { rules } or name::[N] { rules } */
typedef struct adef {
    syntax_store     *store;       /* AST node (ast_algorithm) */
    struct pcontext  *context;     /* Context this algorithm is defined in */

    /* Algorithm name (from store->content[0]) */
    lexical_store    *name;

    /* Reference to alphabet the algorithm operates over */
    syntax_store     *alphabet_ref;    /* AST variable or ast_abstract_size node */
    alphabet_literal *alphabet;        /* Resolved concrete alphabet, NULL if abstract */

    /* Abstract alphabet (non-NULL if algorithm is over [N]) */
    abstract_alphabet *abstract_alph;

    /* List of substitution rules */
    size_t            rules_count;
    size_t            rules_capacity;
    algorithm_rule  **rules;
} algorithm_definition;

typedef struct {
    size_t                  count;
    size_t                  capacity;
} algorithm_definition_info;

/* Algorithm call: swap("□□■"), swap(w), swap(~), sort(reverse("...")) */
typedef enum {
    CALL_LITERAL,   /* string literal argument */
    CALL_VARIABLE,  /* word variable reference */
    CALL_STDIN,     /* ~ (read from stdin) */
    CALL_COMPOSED   /* nested algorithm call */
} algorithm_call_type;

typedef struct acall algorithm_call;
struct acall {
    syntax_store       *store;           /* AST node */
    lexical_store      *algorithm_name;  /* algorithm to invoke */
    algorithm_call_type input_type;
    lexical_store      *input_token;     /* string literal or variable name, NULL for stdin */
    struct acall       *inner_call;      /* nested call for CALL_COMPOSED, NULL otherwise */
    lexical_store      *selected_bind;   /* explicit bind name (sort::b1(...)), NULL for auto */
};

/* Range set: finite, enumerable set of non-negative integers */
typedef struct {
    size_t   count;
    size_t  *values;    /* sorted, deduplicated */
} range_set;

/* Alphabet bind: [N] :> {□, ■} or [N] :[rules]> {□, ■} */
typedef enum {
    BIND_MAP,       /* a:□ — abstract position to concrete letter */
    BIND_INERT,     /* △: — letter passes through unchanged */
    BIND_COLLAPSE,  /* c.b — maps to same letter as another position */
    BIND_ERROR      /* !d — halt if encountered */
} bind_rule_type;

typedef struct {
    bind_rule_type  type;
    lexical_store  *source;     /* abstract position or concrete letter */
    lexical_store  *target;     /* concrete letter or collapse target, NULL for inert/error */
} bind_rule_entry;

typedef struct {
    syntax_store    *store;          /* AST node */
    syntax_store    *source_alph;    /* left of :> (abstract or concrete alphabet) */
    syntax_store    *target_alph;    /* right of :> (concrete alphabet) */
    bool             is_universal;   /* true if :>, false if :[...]> */
    size_t           rules_count;
    bind_rule_entry *rules;          /* NULL if universal */
    lexical_store   *name;           /* variable name if assigned, NULL otherwise */
} alphabet_bind;

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

    size_t                   calls_count;
    size_t                   calls_capacity;
    algorithm_call         **calls;

    size_t                   binds_count;
    size_t                   binds_capacity;
    alphabet_bind          **binds;
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
