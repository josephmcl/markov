#pragma once 

#include "string.h"

#include "lex.h"
#include "syntax.h"

struct data {
    size_t   ( * letters_count) (void);
    uint8_t *( * letters_data)  (void);
    void     ( * generate)      (
        const struct lex    *Lex,
        const struct syntax *Syntax);
};

extern const struct data Data;