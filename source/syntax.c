#include "syntax.h"

extern int yyparse (void);

#define NODES_SIZE 64

static syntax_info TheInfo = {0};
static syntax_store *TheTree;

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

uint32_t maxspn(uint8_t *a, size_t as, uint8_t *b, size_t bs) {
    
    uint8_t *ap = (uint8_t *)a;           /* pointer to a       */
    uint32_t max = 0;                 /* max substring char */
    uint32_t maxi = 0;
    uint32_t maxj = 0;
    for (size_t i = 0; i < as; ++i) {
        ap = a + i;
        uint8_t *bp;
        for (size_t j = 0; j < bs; ++j) {
            bp = b + j;
            uint8_t *tmp = memchr(bp, *ap, bs - j);
            if (tmp == NULL) 
                break;

            j += tmp - bp;
            bp = tmp;

            uint8_t *spa, *spb;
            size_t len = 0;         /* find substring len */
            for (size_t k = 0; i + k < as && j + k < bs; ++k) {
                spa = ap + k;
                spb = bp + k;
                if (*spa != *spb) 
                    break;
                len++;
            } 
            if (len > max) {
                max = len;
                maxi = i;
                maxj = j;
            }
        }
    }

    return (maxi << 16) + (maxj << 8) + (max);
}

void _typecheck_alphabet_body_letters(syntax_store *store) {

    size_t    bytes;
    size_t   *lengths;
    uint8_t **letters;
    uint32_t  max;
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

        for (size_t i = 0; i < store->size - 1; ++i) {
            for (size_t j = i + 1; j < store->size; ++j) {
                max = maxspn(letters[i], lengths[i], 
                             letters[j], lengths[j]);
                size_t m = max & 0xFF;
                if (m >= lengths[i]) {
                    lstore = Lex.store(store->content[j]->token_index);
                    printf("%s:%u:%u: error: alphabet definition is ambiguous. " 
                        "Letter \'%.*s\' contains entirety of letter "
                        "\'%.*s\'.\n", Lex.file->name, lstore->column, lstore->row,
                        lengths[j], letters[j], lengths[i], 
                        letters[i]);
                }
                else if (m >= lengths[j]) {
                    lstore = Lex.store(store->content[i]->token_index);
                    printf("%s:%u:%u: error: alphabet definition is ambi"
                        "guous. Letter \'%.*s\' contains entirety of "
                        "letter \'%.*s\'.\n", Lex.file->name, lstore->column, 
                        lstore->row, lengths[i], letters[i], 
                        lengths[j], letters[j]);
                }
            }
        }
        
        free(lengths);
        free(letters);
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

void syntax_free(void) {
    free(TheTree);
}

const struct syntax Syntax = {
    .parse = syntax_parse,
    .push  = syntax_push,
    .check = syntax_check,
    .free  = syntax_free
};
