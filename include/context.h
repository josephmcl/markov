#pragma once 

#include "string.h"
#include "stdbool.h"

#include "syntax.h"
#include "lex.h"
#include "bison.h"

#include "context/definitions.h"
#include "context/letter.h"
#include "context/variable.h"
#include "context/alphabet_literal.h"
#include "context/algorithm.h"

struct context {
    void ( *validate) (void);
};

extern const struct context Context;
