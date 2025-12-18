#pragma once
#include "definitions.h"

algorithm_definition *algorithm_definition_push(void);
algorithm_definition *algorithm_definition_current(void);

algorithm_rule *algorithm_rule_push(algorithm_definition *alg);

syntax_store *_update_context_algorithm(
    syntax_store         *store,
    program_context_info *info,
    program_context      *context);

algorithm_definition *_context_push_algorithm(
    program_context      *context,
    algorithm_definition *alg);

bool _context_has_algorithm(
    program_context      *context,
    algorithm_definition *alg);
