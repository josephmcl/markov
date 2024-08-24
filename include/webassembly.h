#pragma once

#include "stdio.h"
#include "string.h"
#include "stdbool.h"

#include "lex.h"
#include "syntax.h"
#include "bison.h"
#include "context.h"

struct webassembly {
    void ( *generate) (void);
}; 

extern const struct webassembly WebAssembly;