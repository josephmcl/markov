#pragma once
#include "string.h"

#include "definitions.h"

syntax_store *_update_context_letter(
    syntax_store         *store,
    program_context_info *info, 
    program_context      *context);

syntax_store *_update_known_context_letter(
    lexical_store   *letter,
    program_context *context);
