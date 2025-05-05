#pragma once 
#include "syntax.h"

syntax_store *scope_scope(
    syntax_store *statements,
    syntax_store *scope_name);

syntax_store *scope_scope_context_scope(
    syntax_store *scope_context,
    syntax_store *statements,
    syntax_store *scope_name);