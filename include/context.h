#pragma once 

#include "string.h"
#include "stdbool.h"

#include "syntax.h"
#include "lex.h"
#include "bison.h"


extern 
struct context {
    void ( *validate) (void);
};

extern const struct context Context;
