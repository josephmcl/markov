#include "syntax.h"

extern int yyparse (void);

#define NODES_SIZE 64

static syntax_info TheInfo = {0};
static syntax_store *TheTree;
static size_t TheErrors = 0;

/* Invoke bison LALR1 parser. */
int syntax_parse(void) {
    return yyparse();
}

/* Allocate a new node in the ast. Return a pointer to the location. 
   If memory has already been allocated, do nothing. Otherwise, add 
   storage for more nodes. */
syntax_store *syntax_push(void) {

    size_t bytes;
    if (TheInfo.count == TheInfo.capacity) {
        TheInfo.capacity += NODES_SIZE;

        bytes = sizeof(syntax_store) * TheInfo.capacity;
        TheTree = (syntax_store *) realloc(TheTree, bytes); 
    }

    TheInfo.count += 1;
    return TheTree + TheInfo.count;
}

syntax_store **syntax_realloc(
    syntax_store **stores, 
    size_t        *count, 
    size_t        *capacity) {

    size_t bytes;
    if (*count == *capacity) {
        *capacity += NODES_SIZE;
        bytes = sizeof(syntax_store) * (*capacity);
        TheTree = (syntax_store *) realloc(stores, bytes); 
    }

    TheInfo.count += 1;
    return stores + (*count);
}

bool _letters_identical(
    uint8_t      **letters, 
    size_t        *lengths, 
    syntax_store  *store) {
    
    lexical_store *lstore;

    bool res = false;
    for (size_t i = 0; i < store->size - 1; ++i) {
        for (size_t j = i + 1; j < store->size; ++j) {
            
            if (lengths[i] == lengths[j] && 
                memcmp(letters[i], letters[j], lengths[i]) == 0) {
                lstore = Lex.store(store->content[i]->token_index);
                printf("%s:%u:%u: error: alphabet definition is "
                    "ambiguous and contains multiple instances of "
                    "the same letter \'%.*s\'.\n", Lex.file->name,  
                    lstore->column, lstore->row, (int) lengths[i],  
                    letters[i]);
                res = true;
            }
        }
    }
    return res;
}

bool _letters_ambiguous(
    uint8_t      **letters, 
    size_t        *lengths, 
    syntax_store  *store) {
    
    lexical_store *lstore;
    uint32_t  max;

    bool res = false;
    for (size_t i = 0; i < store->size - 1; ++i) {
        for (size_t j = i + 1; j < store->size; ++j) {
            max = max_shared_vlaues(letters[i], lengths[i], 
                            letters[j], lengths[j]);
            size_t m = max & 0x00F;
            if (m >= lengths[i]) {
                lstore = Lex.store(store->content[j]->token_index);
                printf("%s:%u:%u: error: alphabet definition is "
                    "ambiguous. Letter \'%.*s\' contains entirety "
                    "of letter \'%.*s\'.\n", Lex.file->name, 
                    lstore->column, lstore->row, (int) lengths[j], 
                    letters[j], (int) lengths[i], letters[i]);
                res = true;
            }
            else if (m >= lengths[j]) {
                lstore = Lex.store(store->content[i]->token_index);
                printf("%s:%u:%u: error: alphabet definition is "
                    "ambiguous. Letter \'%.*s\' contains entirety "
                    "of letter \'%.*s\'.\n", Lex.file->name, 
                    lstore->column, lstore->row, (int) lengths[i], 
                    letters[i], (int) lengths[j], letters[j]);
                res = true;
            }
        }
    }
    return res;
}

void _typecheck_alphabet_body_letters(syntax_store *store) {

    size_t    bytes;
    size_t   *lengths;
    uint8_t **letters;
    lexical_store *lstore;

    if (store->size > 1) {
        
        bytes = sizeof(size_t) * store->size;
        lengths = (size_t *) malloc(bytes);
        bytes = sizeof(uint8_t *) * store->size;
        letters = (uint8_t **) malloc(bytes);
        
        for (size_t i = 0; i < store->size; ++i) {
            lstore = Lex.store(store->content[i]->token_index);
            lengths[i] = lstore->end - lstore->begin;
            letters[i] = lstore->begin;
        }

        if (_letters_identical(letters, lengths, store)) goto fail;
        if (_letters_ambiguous(letters, lengths, store)) goto fail;

        goto pass;
        
        fail: 
            TheErrors += 1;
            free(lengths);
            free(letters);
            return;

        pass:
            free(lengths);
            free(letters);
            return;
    }

    return;
}

void _typecheck_alphabet_body(syntax_store *store) {

    if (store->size == 1 && store->content[0]->type == ast_letters) {
        _typecheck_alphabet_body_letters(store->content[0]);
    }

    return;
}

void syntax_check(syntax_store *store) {
    switch (store->type){ 
        case ast_alphabet_body:
            _typecheck_alphabet_body(store);
        default: 
            return;
    }
    return;
}

void _print_node_string(syntax_store_type type) {
    switch (type) {
    case ast_program:               printf("Program"); return;
    case ast_scope:                 printf("Scope"); return;
    case ast_scope_name:            printf("Scope Name"); return;
    case ast_scope_type:            printf("Scope Type"); return;
    case ast_statements:            printf("Statements"); return;
    case ast_statement:             printf("Statement"); return;
    case ast_l_expression:          printf("L Expression"); return;
    case ast_r_expression:          printf("R Expression"); return;
    case ast_extends_expression:    printf("Extends Expression"); return;
    case ast_variable:              printf("Variable"); return;
    case ast_assignment_statement:  printf("Assignment"); return;
    case ast_alphabet_body:         printf("Alphabet Body"); return;
    case ast_letters:               printf("Letters"); return;
    case ast_letter:                printf("Letter"); return;
    default:                        printf("UNKNOWN"); return;
    }
}

void depth_print(syntax_store *s, size_t indent) {

    for (size_t i = 0; i < indent; ++i) 
        printf("|  ");
    _print_node_string(s->type);
    printf("[%lu](%p)\n", s->size, (void *) s);

    for (size_t i = 0; i < s->size; ++i) {
        if (s->content[i] != NULL)
            depth_print(s->content[i], indent + 1);
    }

}

void syntax_print(void) {

    printf("Total AST Nodes: %lu\n", TheInfo.count);

    depth_print(&TheTree[TheInfo.count], 0);

}

void syntax_free(void) {
    for (size_t i = 0; i < TheInfo.capacity; ++i) {
        if (TheTree[i].size > 0)
            free(TheTree[i].content);
    }
    free(TheTree);
}

syntax_store *get_tree(void) {
    return &TheTree[TheInfo.count];
}


size_t syntax_errors(void) {
    return TheErrors;
}


const struct syntax Syntax = {
    .tree       =  get_tree,
    .info       = &TheInfo,
    .parse      =  syntax_parse,
    .push       =  syntax_push,
    .check      =  syntax_check,
    .print      =  syntax_print,
    .free       =  syntax_free,
    .errors     =  syntax_errors
};
