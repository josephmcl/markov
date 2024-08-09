#pragma once 
#include "syntax.h"

syntax_store *statements_root(void);

syntax_store *statements_statements_statementz(
    syntax_store *statements, 
    syntax_store *statementz);

syntax_store *statements_statements_statementz_PERIOD(
    syntax_store *statements, 
    syntax_store *statementz);

syntax_store *statements_statements_scope(
    syntax_store *statements, 
    syntax_store *scope);