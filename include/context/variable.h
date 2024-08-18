#pragma once

#include "string.h"

#include "definitions.h"

syntax_store *_update_context_variable(
    syntax_store         *store,
    program_context_info *info, 
    program_context      *context);

syntax_store *_update_known_context_variable(
    lexical_store   *variable,
    program_context *context);