#pragma once

#include "stdio.h"
#include "string.h"
#include "stdbool.h"

#include "lex.h"
#include "syntax.h"
#include "bison.h"
#include "context.h"
#include "data.h"

struct webassembly {
    void ( *generate) (struct data *Data);
}; 

extern const struct webassembly WebAssembly;