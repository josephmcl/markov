#pragma once

#include "io.h"
#include "codepoint.h"

#define TOKEN_SECTION 32
#define TOKEN_PACK 
#define MULTI_BYTE_TOKENS 32
#define SINGLE_BYTE_TOKENS 64
#define KEYWORD_TOKENS 96

typedef enum {
    UNKNOWN          = -2,
    START_OF_CONTENT = -1, // TODO: unused
    END_OF_CONTENT   = 0, 
    ESCAPE           = 1,
    LINE_END         = 2,
    LINE_COMMENT     = 3,
    IDENTIFIER       = 4,
    
    IN               = MULTI_BYTE_TOKENS,     // ∈ [1.2.5]
    NOT              = MULTI_BYTE_TOKENS + 1, // ¬ [1.2.5]
    EXTENDS          = MULTI_BYTE_TOKENS + 2, // ⊂ [1.2.7]

    EQUAL            = SINGLE_BYTE_TOKENS,      // = [1.2.6]
    COMMA            = SINGLE_BYTE_TOKENS + 1,  // , [1.2.6]
    LCURL            = SINGLE_BYTE_TOKENS + 2,  // { [1.2.6]
    RCURL            = SINGLE_BYTE_TOKENS + 3,  // } [1.2.6]
    SEMICOLON        = SINGLE_BYTE_TOKENS + 4,  // ; [1.2.6]
    PERIOD           = SINGLE_BYTE_TOKENS + 5,  // . [1.2.6]

    EN_IN            = KEYWORD_TOKENS,      // in [1.2.5]
    EN_NOT           = KEYWORD_TOKENS + 1,  // not [1.2.6]
    EN_EXTENDS       = KEYWORD_TOKENS + 2,  // extends [1.2.6]

} lexical_token;

typedef struct {
    lexical_token token;
    uint8_t *begin;
    uint8_t *end;
    unsigned int row, column;
} lexical_store;

extern const char *token_names[];


/* returns matched token */
lexical_token single_byte_token(uint8_t c);

/* returns matched token in the upper byte 
   and matched sequence length in the lower byte
   otherwise returns 0
 */
uint16_t multi_byte_token(uint8_t *head, uint8_t *end);
uint16_t keyword_token(uint8_t *head, uint8_t *end);

/* returns head + matched sequence length */
uint8_t *consume_whitespace(uint8_t *head);
uint8_t *identifier(uint8_t *head);
uint8_t *integer_literal(uint8_t *head);
uint8_t *float_literal(uint8_t *head);
uint8_t *rational_literal(uint8_t *head, uint8_t *end);
uint8_t *line_comment(uint8_t *head, uint8_t *end);
uint8_t *string_literal(uint8_t *head, uint8_t *end);

int is_escape(uint8_t c);
