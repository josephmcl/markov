#pragma once 

#include "string.h"
#include "stdbool.h"

#include "syntax.h"
#include "lex.h"
#include "bison.h"

#include "context/definitions.h"
#include "context/letter.h"
#include "context/variable.h"

struct context {
    void ( *validate) (void);
};

extern const struct context Context;
