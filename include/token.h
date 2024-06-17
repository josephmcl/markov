#pragma once

#include "io.h"
#include "codepoint.h"

#define TOKEN_SECTION 32
#define TOKEN_PACK 
#define MULTI_BYTE_TOKENS 32
#define SINGLE_BYTE_TOKENS 64
#define KEYWORD_TOKENS 96

typedef enum {
    TOKEN_UNUSED_BY_PARSER = -4,
    TOKEN_UNSUPPORTED_BY_PARSER = -3,
    TOKEN_UNKNOWN          = -2,
    TOKEN_START_OF_CONTENT = -1, // TODO: unused
    TOKEN_END_OF_CONTENT   = 0, 
    TOKEN_ESCAPE           = 1,
    TOKEN_LINE_END         = 2,
    TOKEN_LINE_COMMENT     = 3,
    TOKEN_IDENTIFIER       = 4,
    
    TOKEN_IN               = MULTI_BYTE_TOKENS,     // ∈  [1.2.5]
    TOKEN_NOT              = MULTI_BYTE_TOKENS + 1, // ¬  [1.2.5]
    TOKEN_EXTENDS          = MULTI_BYTE_TOKENS + 2, // ⊂  [1.2.7]
    TOKEN_DOUBLE_COLON     = MULTI_BYTE_TOKENS + 3, // :: [Extended]

    TOKEN_EQUAL            = SINGLE_BYTE_TOKENS,      // = [1.2.6]
    TOKEN_COMMA            = SINGLE_BYTE_TOKENS + 1,  // , [1.2.6]
    TOKEN_LCURL            = SINGLE_BYTE_TOKENS + 2,  // { [1.2.6]
    TOKEN_RCURL            = SINGLE_BYTE_TOKENS + 3,  // } [1.2.6]
    TOKEN_SEMICOLON        = SINGLE_BYTE_TOKENS + 4,  // ; [1.2.6]
    TOKEN_PERIOD           = SINGLE_BYTE_TOKENS + 5,  // . [1.2.6]
    TOKEN_LANGLE           = SINGLE_BYTE_TOKENS + 6,  // < [Extended]
    TOKEN_RANGLE           = SINGLE_BYTE_TOKENS + 7,  // > [Extended]

    TOKEN_EN_IN            = KEYWORD_TOKENS,      // in [1.2.5]
    TOKEN_EN_NOT           = KEYWORD_TOKENS + 1,  // not [1.2.6]
    TOKEN_EN_EXTENDS       = KEYWORD_TOKENS + 2,  // extends [1.2.6]

} lexical_token;

typedef struct {
    lexical_token token;
    uint8_t *begin, *end;
    unsigned int row, column, space_prior;
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
