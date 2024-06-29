#pragma once

#include "stdio.h"
#include "string.h"

#include "lex.h"
#include "syntax.h"

struct webassembly {
    void ( *generate) (void);
}; 

extern const struct webassembly WebAssembly;