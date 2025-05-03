#pragma once 

#include "syntax.h"

typedef struct {
    // TODO: Need to be able to talk about types the AST. We are 
    //       gonna need a type here... maybe in type.h im guessing.
    //       This will replace the syntax_store in context/variable
    void *type, 
    syntax_store *syntax
} variable_store;