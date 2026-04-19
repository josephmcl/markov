#include "context.h"

/* Forward declarations from letter.c */
extern lexical_store *_context_push_letter(program_context *context, lexical_store *letter);
extern bool _context_has_letter(program_context *context, lexical_store *letter);

/* Resolve a variable to its assigned value in the AST.
 * Searches for an assignment_statement where content[0] matches the
 * given variable name, and returns content[1] (the assigned expression).
 * Returns NULL if not found. */
static syntax_store *resolve_variable_assignment(lexical_store *var_tok) {
    if (var_tok == NULL) return NULL;
    size_t var_len = var_tok->end - var_tok->begin;

    syntax_store *tree = Syntax.tree();
    for (size_t i = 0; i < Syntax.info->count; i++) {
        syntax_store *node = &tree[-((int)i)];
        if (node->type == ast_assignment_statement &&
            node->size >= 2 &&
            node->content[0] != NULL &&
            node->content[1] != NULL) {

            lexical_store *assign_var = Lex.store(node->content[0]->token_index);
            size_t assign_len = assign_var->end - assign_var->begin;
            if (assign_len == var_len &&
                memcmp(assign_var->begin, var_tok->begin, var_len) == 0) {
                return node->content[1];
            }
        }
    }
    return NULL;
}

#define ALGORITHM_DEFINITIONS_SIZE 16
#define ALGORITHM_RULES_SIZE 32
#define CONTEXT_ALGORITHMS_SIZE 16

algorithm_definition *TheAlgorithmDefinition = NULL;
algorithm_rule *TheAlgorithmRule = NULL;

static algorithm_definition_info TheInfo = {0};
static size_t TheRuleCount = 0;
static size_t TheRuleCapacity = 0;

algorithm_definition *algorithm_definition_push(void) {
    size_t bytes;
    if (TheInfo.count == TheInfo.capacity) {
        TheInfo.capacity += ALGORITHM_DEFINITIONS_SIZE;
        bytes = sizeof(algorithm_definition) * TheInfo.capacity;
        TheAlgorithmDefinition =
            (algorithm_definition *) realloc(TheAlgorithmDefinition, bytes);
    }

    TheInfo.count += 1;
    return TheAlgorithmDefinition + TheInfo.count - 1;
}

algorithm_definition *algorithm_definition_current(void) {
    return &TheAlgorithmDefinition[TheInfo.count - 1];
}

algorithm_rule *algorithm_rule_push(algorithm_definition *alg) {
    size_t bytes;

    /* Grow global rule storage if needed */
    if (TheRuleCount == TheRuleCapacity) {
        TheRuleCapacity += ALGORITHM_RULES_SIZE;
        bytes = sizeof(algorithm_rule) * TheRuleCapacity;
        TheAlgorithmRule = (algorithm_rule *) realloc(TheAlgorithmRule, bytes);
    }

    algorithm_rule *rule = &TheAlgorithmRule[TheRuleCount];
    TheRuleCount += 1;

    /* Add to algorithm's rule list */
    if (alg->rules_count == alg->rules_capacity) {
        alg->rules_capacity += ALGORITHM_RULES_SIZE;
        bytes = sizeof(algorithm_rule *) * alg->rules_capacity;
        alg->rules = (algorithm_rule **) realloc(alg->rules, bytes);
    }

    alg->rules[alg->rules_count] = rule;
    alg->rules_count += 1;

    return rule;
}

algorithm_definition *_context_push_algorithm(
    program_context      *context,
    algorithm_definition *alg) {

    size_t bytes;
    if (context->algorithms_count == context->algorithms_capacity) {
        context->algorithms_capacity += CONTEXT_ALGORITHMS_SIZE;
        bytes = sizeof(algorithm_definition *) * context->algorithms_capacity;
        context->algorithms =
            (algorithm_definition **) realloc(context->algorithms, bytes);
    }

    context->algorithms[context->algorithms_count] = alg;
    context->algorithms_count += 1;
    return alg;
}

bool _context_has_algorithm(
    program_context      *context,
    algorithm_definition *alg) {

    for (size_t i = 0; i < context->algorithms_count; ++i) {
        if (context->algorithms[i] == alg) {
            return true;
        }
    }
    return false;
}

syntax_store *_update_context_algorithm(
    syntax_store         *store,
    program_context_info *info,
    program_context      *context) {

    syntax_store *topic = store;
    algorithm_definition *alg;
    size_t index;

    /* Navigate to the parent scope or program node */
    while (topic != NULL
           && topic->type != ast_scope
           && topic->type != ast_program) {
        topic = topic->topic;
    }

    if (topic == NULL) {
        printf("Error. Could not determine algorithm's topic scope.\n");
        return NULL;
    }

    /* Find the matching context */
    index = info->count;
    for (size_t i = 0; i < info->count; ++i) {
        if (topic == context[i].syntax) {
            index = i;
            break;
        }
    }

    if (index == info->count) {
        printf("Error. Could not find matching context for algorithm.\n");
        return NULL;
    }

    /* Create new algorithm definition */
    alg = algorithm_definition_push();
    alg->store = store;
    alg->context = &context[index];
    alg->rules_count = 0;
    alg->rules_capacity = 0;
    alg->rules = NULL;
    alg->alphabet = NULL;
    alg->abstract_alph = NULL;

    /* Extract algorithm components from AST:
       content[0] = algorithm name (ast_variable)
       content[1] = alphabet variable (ast_variable) or abstract size (ast_abstract_size)
       content[2] = rules (ast_algorithm_rules) */

    if (store->size >= 1 && store->content[0] != NULL) {
        alg->name = Lex.store(store->content[0]->token_index);
    } else {
        alg->name = NULL;
    }

    if (store->size >= 2 && store->content[1] != NULL) {
        alg->alphabet_ref = store->content[1];

        /* Resolve variable indirection: if alphabet_ref is a variable,
           check if it was assigned to an abstract alphabet. */
        syntax_store *alph_node = store->content[1];
        if (alph_node->type == ast_variable) {
            lexical_store *var_tok = Lex.store(alph_node->token_index);
            syntax_store *resolved = resolve_variable_assignment(var_tok);
            if (resolved != NULL &&
                (resolved->type == ast_abstract_size ||
                 resolved->type == ast_abstract_named)) {
                alph_node = resolved;
            }
        }

        /* Handle abstract alphabet [N] — auto-named positions */
        if (alph_node->type == ast_abstract_size) {
            lexical_store *num_tok = Lex.store(alph_node->token_index);
            int n = 0;
            for (const uint8_t *p = num_tok->begin; p < num_tok->end; p++) {
                n = n * 10 + (*p - '0');
            }
            if (n > 0 && n <= MAX_ABSTRACT_LETTERS) {
                abstract_alphabet *abs = (abstract_alphabet *)malloc(sizeof(abstract_alphabet));
                abs->size = (size_t)n;
                for (size_t i = 0; i < abs->size; i++) {
                    abs->name_storage[i * 2] = 'a' + (uint8_t)i;
                    abs->name_storage[i * 2 + 1] = 0;
                    abs->names[i].bytes = &abs->name_storage[i * 2];
                    abs->names[i].len = 1;
                }
                alg->abstract_alph = abs;
            } else {
                printf("Error: abstract alphabet size must be 1-%d, got %d\n",
                    MAX_ABSTRACT_LETTERS, n);
            }
        }
        /* Handle abstract named alphabet [greater, lesser] — explicit names */
        else if (alph_node->type == ast_abstract_named) {
            syntax_store *named = alph_node;
            size_t n = named->size;
            if (n > 0 && n <= MAX_ABSTRACT_LETTERS) {
                abstract_alphabet *abs = (abstract_alphabet *)malloc(sizeof(abstract_alphabet));
                abs->size = n;
                for (size_t i = 0; i < n; i++) {
                    if (named->content[i] != NULL) {
                        lexical_store *name_tok = Lex.store(named->content[i]->token_index);
                        abs->names[i].bytes = name_tok->begin;
                        abs->names[i].len = name_tok->end - name_tok->begin;
                    }
                }
                alg->abstract_alph = abs;
            } else {
                printf("Error: abstract alphabet size must be 1-%d, got %zu\n",
                    MAX_ABSTRACT_LETTERS, n);
            }
        }
    } else {
        alg->alphabet_ref = NULL;
    }

    /* Process rules */
    if (store->size >= 3 && store->content[2] != NULL) {
        syntax_store *rules_node = store->content[2];

        for (size_t i = 0; i < rules_node->size; ++i) {
            syntax_store *rule_node = rules_node->content[i];
            if (rule_node == NULL || rule_node->type != ast_algorithm_rule) {
                continue;
            }

            algorithm_rule *rule = algorithm_rule_push(alg);
            rule->store = rule_node;

            /* AST content layout (uniform, size=4):
               content[0] = LHS pattern (always)
               content[1] = RHS replacement (NULL for terminal)
               content[2] = rule name node (NULL if unnamed)
               content[3] = emit string node (NULL if silent/default)
               token_index = arrow token (ARROW/TERMINAL/EMIT_ARROW/EMIT_TERMINAL) */

            rule->pattern = (rule_node->size >= 1) ? rule_node->content[0] : NULL;
            rule->replacement = (rule_node->size >= 2) ? rule_node->content[1] : NULL;

            /* Determine rule type from arrow token */
            lexical_store *arrow_tok = Lex.store(rule_node->token_index);
            rule->is_terminal = (arrow_tok->token == TOKEN_TERMINAL ||
                                 arrow_tok->token == TOKEN_EMIT_TERMINAL);
            rule->has_emit = (arrow_tok->token == TOKEN_EMIT_ARROW ||
                              arrow_tok->token == TOKEN_EMIT_TERMINAL);

            /* Rule name (content[2]) */
            if (rule_node->size >= 3 && rule_node->content[2] != NULL) {
                rule->rule_name = Lex.store(rule_node->content[2]->token_index);
            } else {
                rule->rule_name = NULL;
            }

            /* Emit string (content[3]) */
            if (rule_node->size >= 4 && rule_node->content[3] != NULL) {
                rule->emit_string = Lex.store(rule_node->content[3]->token_index);
            } else {
                rule->emit_string = NULL;
            }
        }
    }

    /* For [N] abstract alphabets: attempt to deduce multi-char letter names
     * from the rule patterns. If all patterns decompose cleanly into N
     * single-char names (a, b, c...), keep the auto-names. If not, try to
     * deduce multi-char names by constraint satisfaction on the first pattern. */
    if (alg->abstract_alph != NULL &&
        alg->alphabet_ref != NULL &&
        alg->alphabet_ref->type == ast_abstract_size &&
        alg->rules_count > 0) {

        abstract_alphabet *abs = alg->abstract_alph;
        size_t n = abs->size;

        /* Check if single-char auto-names work for all patterns */
        bool single_char_works = true;
        for (size_t r = 0; r < alg->rules_count && single_char_works; r++) {
            algorithm_rule *rule = alg->rules[r];
            if (rule->pattern == NULL) continue;

            /* Get pattern tokens and check each byte is a valid a-z name */
            size_t tok_idx = rule->pattern->token_index;
            size_t tok_count = rule->pattern->size + 1;
            for (size_t t = 0; t < tok_count; t++) {
                lexical_store *tok = Lex.store(tok_idx + t);
                for (const uint8_t *p = tok->begin; p < tok->end; p++) {
                    if (*p < 'a' || *p >= 'a' + n) {
                        single_char_works = false;
                        break;
                    }
                }
                if (!single_char_works) break;
                /* Skip to next IDENTIFIER token */
                while (tok_idx + t + 1 < tok_idx + tok_count) {
                    lexical_store *next = Lex.store(tok_idx + t + 1);
                    if (next->token == TOKEN_IDENTIFIER) break;
                    t++;
                }
            }
        }

        if (!single_char_works) {
            /* Try to deduce multi-char names from the first rule's pattern.
             * Collect all unique tokens from all patterns/replacements,
             * then check if exactly N unique names are found. */
            const uint8_t *found_names[MAX_ABSTRACT_LETTERS];
            size_t found_lens[MAX_ABSTRACT_LETTERS];
            size_t found_count = 0;

            for (size_t r = 0; r < alg->rules_count; r++) {
                algorithm_rule *rule = alg->rules[r];
                /* Collect tokens from pattern */
                if (rule->pattern != NULL) {
                    size_t tok_idx = rule->pattern->token_index;
                    size_t tok_count = rule->pattern->size + 1;
                    size_t scanned = 0;
                    for (size_t t = tok_idx; scanned < tok_count; t++) {
                        lexical_store *tok = Lex.store(t);
                        if (tok->token != TOKEN_IDENTIFIER) continue;
                        scanned++;
                        size_t tlen = tok->end - tok->begin;
                        /* Check if already found */
                        bool dup = false;
                        for (size_t f = 0; f < found_count; f++) {
                            if (found_lens[f] == tlen &&
                                memcmp(found_names[f], tok->begin, tlen) == 0) {
                                dup = true;
                                break;
                            }
                        }
                        if (!dup && found_count < MAX_ABSTRACT_LETTERS) {
                            found_names[found_count] = tok->begin;
                            found_lens[found_count] = tlen;
                            found_count++;
                        }
                    }
                }
                /* Collect tokens from replacement */
                if (rule->replacement != NULL) {
                    size_t tok_idx = rule->replacement->token_index;
                    size_t tok_count = rule->replacement->size + 1;
                    size_t scanned = 0;
                    for (size_t t = tok_idx; scanned < tok_count; t++) {
                        lexical_store *tok = Lex.store(t);
                        if (tok->token != TOKEN_IDENTIFIER) continue;
                        scanned++;
                        size_t tlen = tok->end - tok->begin;
                        bool dup = false;
                        for (size_t f = 0; f < found_count; f++) {
                            if (found_lens[f] == tlen &&
                                memcmp(found_names[f], tok->begin, tlen) == 0) {
                                dup = true;
                                break;
                            }
                        }
                        if (!dup && found_count < MAX_ABSTRACT_LETTERS) {
                            found_names[found_count] = tok->begin;
                            found_lens[found_count] = tlen;
                            found_count++;
                        }
                    }
                }
            }

            if (found_count == n) {
                /* Exact match: found exactly N unique names */
                for (size_t i = 0; i < n; i++) {
                    abs->names[i].bytes = found_names[i];
                    abs->names[i].len = found_lens[i];
                }
                printf("Deduced abstract letter names:");
                for (size_t i = 0; i < n; i++) {
                    printf(" %.*s", (int)abs->names[i].len, abs->names[i].bytes);
                }
                printf("\n");
            } else {
                printf("Error: cannot deduce abstract letter boundaries for [%zu]. "
                       "Found %zu unique tokens, expected %zu. "
                       "Use explicit names: [name1, name2, ...].\n",
                       n, found_count, n);
            }
        }
    }

    /* Add algorithm to context */
    if (!_context_has_algorithm(&context[index], alg)) {
        _context_push_algorithm(&context[index], alg);
    }

    return NULL;
}

/* ======================================================================== */
/* Algorithm call handling                                                   */
/* ======================================================================== */

#define CONTEXT_CALLS_SIZE 16

static algorithm_call *TheAlgorithmCalls = NULL;
static size_t TheCallCount = 0;
static size_t TheCallCapacity = 0;

static algorithm_call *algorithm_call_push(void) {
    if (TheCallCount == TheCallCapacity) {
        TheCallCapacity += CONTEXT_CALLS_SIZE;
        TheAlgorithmCalls = (algorithm_call *)
            realloc(TheAlgorithmCalls, sizeof(algorithm_call) * TheCallCapacity);
    }
    return &TheAlgorithmCalls[TheCallCount++];
}

static void _context_push_call(program_context *ctx, algorithm_call *call) {
    size_t bytes;
    if (ctx->calls_count == ctx->calls_capacity) {
        ctx->calls_capacity += CONTEXT_CALLS_SIZE;
        bytes = sizeof(algorithm_call *) * ctx->calls_capacity;
        ctx->calls = (algorithm_call **) realloc(ctx->calls, bytes);
    }
    ctx->calls[ctx->calls_count++] = call;
}

syntax_store *_update_context_algorithm_call(
    syntax_store         *store,
    program_context_info *info,
    program_context      *context) {

    /* Skip inner calls that are part of a composition — they're handled
       by the outer call's context handler */
    if (store->topic != NULL && store->topic->type == ast_algorithm_call) {
        return NULL;
    }

    /* Find the enclosing scope context (same logic as algorithms) */
    syntax_store *topic = store;
    while (topic != NULL
           && topic->type != ast_scope
           && topic->type != ast_program) {
        topic = topic->topic;
    }
    if (topic == NULL) return NULL;

    size_t index = info->count;
    for (size_t i = 0; i < info->count; ++i) {
        if (topic == context[i].syntax) {
            index = i;
            break;
        }
    }
    if (index == info->count) return NULL;

    /* Extract call info from AST:
       content[0] = algorithm name (ast_variable)
       content[1] = argument (ast_word_literal, ast_variable, or NULL for stdin) */
    algorithm_call *call = algorithm_call_push();
    call->store = store;
    call->inner_call = NULL;

    if (store->size >= 1 && store->content[0] != NULL) {
        call->algorithm_name = Lex.store(store->content[0]->token_index);
    } else {
        call->algorithm_name = NULL;
    }

    if (store->size >= 2 && store->content[1] != NULL) {
        syntax_store *arg = store->content[1];
        if (arg->type == ast_algorithm_call) {
            /* Composed call: sort(reverse("...")) */
            call->input_type = CALL_COMPOSED;
            call->input_token = NULL;
            /* Recursively process the inner call */
            algorithm_call *inner = algorithm_call_push();
            inner->store = arg;
            inner->inner_call = NULL;
            if (arg->size >= 1 && arg->content[0] != NULL) {
                inner->algorithm_name = Lex.store(arg->content[0]->token_index);
            } else {
                inner->algorithm_name = NULL;
            }
            if (arg->size >= 2 && arg->content[1] != NULL) {
                syntax_store *inner_arg = arg->content[1];
                if (inner_arg->type == ast_word_literal) {
                    inner->input_type = CALL_LITERAL;
                    inner->input_token = Lex.store(inner_arg->token_index);
                } else if (inner_arg->type == ast_algorithm_call) {
                    inner->input_type = CALL_COMPOSED;
                    inner->input_token = NULL;
                    /* TODO: deeper nesting */
                } else {
                    inner->input_type = CALL_VARIABLE;
                    inner->input_token = Lex.store(inner_arg->token_index);
                }
            } else {
                inner->input_type = CALL_STDIN;
                inner->input_token = NULL;
            }
            call->inner_call = inner;
        } else {
            call->input_token = Lex.store(arg->token_index);
            if (arg->type == ast_word_literal) {
                call->input_type = CALL_LITERAL;
            } else {
                call->input_type = CALL_VARIABLE;
            }
        }
    } else {
        call->input_type = CALL_STDIN;
        call->input_token = NULL;
    }

    _context_push_call(&context[index], call);
    return NULL;
}

/* ======================================================================== */
/* Bind handling                                                             */
/* ======================================================================== */

#define CONTEXT_BINDS_SIZE 16

static alphabet_bind *TheBinds = NULL;
static size_t TheBindCount = 0;
static size_t TheBindCapacity = 0;

static alphabet_bind *alphabet_bind_push(void) {
    if (TheBindCount == TheBindCapacity) {
        TheBindCapacity += CONTEXT_BINDS_SIZE;
        TheBinds = (alphabet_bind *)
            realloc(TheBinds, sizeof(alphabet_bind) * TheBindCapacity);
    }
    return &TheBinds[TheBindCount++];
}

static void _context_push_bind(program_context *ctx, alphabet_bind *bind) {
    size_t bytes;
    if (ctx->binds_count == ctx->binds_capacity) {
        ctx->binds_capacity += CONTEXT_BINDS_SIZE;
        bytes = sizeof(alphabet_bind *) * ctx->binds_capacity;
        ctx->binds = (alphabet_bind **) realloc(ctx->binds, bytes);
    }
    ctx->binds[ctx->binds_count++] = bind;
}

syntax_store *_update_context_bind(
    syntax_store         *store,
    program_context_info *info,
    program_context      *context) {

    /* Find enclosing scope */
    syntax_store *topic = store;
    while (topic != NULL
           && topic->type != ast_scope
           && topic->type != ast_program) {
        topic = topic->topic;
    }
    if (topic == NULL) return NULL;

    size_t index = info->count;
    for (size_t i = 0; i < info->count; ++i) {
        if (topic == context[i].syntax) {
            index = i;
            break;
        }
    }
    if (index == info->count) return NULL;

    /* Extract bind info from AST:
       content[0] = source alphabet (left of :>)
       content[1] = target alphabet (right of :>)
       content[2] = bind rules list (NULL if universal) */
    alphabet_bind *bind = alphabet_bind_push();
    bind->store = store;
    bind->source_alph = (store->size >= 1) ? store->content[0] : NULL;
    bind->target_alph = (store->size >= 2) ? store->content[1] : NULL;

    /* Resolve variable indirection on source alphabet.
       Handles: a :> squares (where a = [greater, lesser])
       Also:    sort :> squares (where sort is an algorithm over [N]) */
    if (bind->source_alph != NULL && bind->source_alph->type == ast_variable) {
        lexical_store *var_tok = Lex.store(bind->source_alph->token_index);
        syntax_store *resolved = resolve_variable_assignment(var_tok);
        if (resolved != NULL &&
            (resolved->type == ast_abstract_size ||
             resolved->type == ast_abstract_named)) {
            bind->source_alph = resolved;
        } else {
            /* Check if the variable is an algorithm name — resolve to
               its alphabet_ref (the :: accessor) */
            size_t var_len = var_tok->end - var_tok->begin;
            for (size_t ai = 0; ai < context[index].algorithms_count; ai++) {
                algorithm_definition *alg = context[index].algorithms[ai];
                if (alg->name != NULL) {
                    size_t name_len = alg->name->end - alg->name->begin;
                    if (name_len == var_len &&
                        memcmp(alg->name->begin, var_tok->begin, var_len) == 0) {
                        /* Found the algorithm — use its alphabet_ref */
                        if (alg->abstract_alph != NULL && alg->alphabet_ref != NULL) {
                            bind->source_alph = alg->alphabet_ref;
                            /* Re-resolve if it's a variable pointing to abstract */
                            if (bind->source_alph->type == ast_variable) {
                                syntax_store *r2 = resolve_variable_assignment(
                                    Lex.store(bind->source_alph->token_index));
                                if (r2 != NULL &&
                                    (r2->type == ast_abstract_size ||
                                     r2->type == ast_abstract_named)) {
                                    bind->source_alph = r2;
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
    bind->is_universal = (store->size < 3 || store->content[2] == NULL);
    bind->rules_count = 0;
    bind->rules = NULL;
    bind->name = NULL;

    /* If this bind is the right side of an assignment, get the variable name */
    if (store->topic != NULL && store->topic->type == ast_assignment_statement) {
        syntax_store *var_node = store->topic->content[0];
        if (var_node != NULL) {
            bind->name = Lex.store(var_node->token_index);
        }
    }

    /* Extract bind rules if specified */
    if (!bind->is_universal && store->content[2] != NULL) {
        syntax_store *rules_list = store->content[2];

        /* If the bind rules list contains a single variable reference,
           resolve it to the named bind rules value. */
        if (rules_list->size == 1 &&
            rules_list->content[0] != NULL &&
            rules_list->content[0]->type == ast_variable) {

            lexical_store *var_tok = Lex.store(rules_list->content[0]->token_index);
            syntax_store *resolved = resolve_variable_assignment(var_tok);
            if (resolved != NULL && resolved->type == ast_bind_rules_list) {
                rules_list = resolved;
            }
        }

        bind->rules_count = rules_list->size;
        bind->rules = (bind_rule_entry *)malloc(
            sizeof(bind_rule_entry) * bind->rules_count);

        for (size_t i = 0; i < rules_list->size; i++) {
            syntax_store *rule_node = rules_list->content[i];
            bind_rule_entry *entry = &bind->rules[i];

            if (rule_node == NULL) continue;

            /* Determine rule type by structure:
               size=2, token_index at IDENTIFIER → map (a:□) or collapse (c.b)
               size=1, token_index at IDENTIFIER → inert (△:)
               size=1, token_index at EXCLAIM → error (!d) */
            lexical_store *first_tok = Lex.store(rule_node->token_index);

            if (first_tok->token == TOKEN_EXCLAIM) {
                entry->type = BIND_ERROR;
                entry->source = (rule_node->size >= 1 && rule_node->content[0])
                    ? Lex.store(rule_node->content[0]->token_index) : NULL;
                entry->target = NULL;
            } else if (rule_node->size == 1) {
                entry->type = BIND_INERT;
                entry->source = Lex.store(rule_node->content[0]->token_index);
                entry->target = NULL;
            } else if (rule_node->size == 2) {
                /* Check if it's a map (a:□) or collapse (c.b)
                   by looking at the token between them.
                   Map uses COLON token, collapse uses PERIOD.
                   The rule_node->token_index points to:
                   - first IDENTIFIER for map (a:□)
                   - PERIOD for collapse (c.b) */
                lexical_store *mid_tok = Lex.store(rule_node->token_index);
                if (mid_tok->token == TOKEN_PERIOD) {
                    entry->type = BIND_COLLAPSE;
                } else {
                    entry->type = BIND_MAP;
                }
                entry->source = Lex.store(rule_node->content[0]->token_index);
                entry->target = Lex.store(rule_node->content[1]->token_index);
            }
        }
    }

    _context_push_bind(&context[index], bind);
    return NULL;
}

/* ======================================================================== */
/* Cleanup                                                                   */
/* ======================================================================== */

void algorithm_free(void) {
    /* Free per-algorithm allocations */
    for (size_t i = 0; i < TheInfo.count; i++) {
        algorithm_definition *alg = &TheAlgorithmDefinition[i];
        free(alg->rules);
        free(alg->abstract_alph);
    }

    /* Free per-bind allocations */
    for (size_t i = 0; i < TheBindCount; i++) {
        free(TheBinds[i].rules);
    }

    free(TheAlgorithmDefinition);
    free(TheAlgorithmRule);
    free(TheAlgorithmCalls);
    free(TheBinds);

    TheAlgorithmDefinition = NULL;
    TheAlgorithmRule = NULL;
    TheAlgorithmCalls = NULL;
    TheBinds = NULL;
    TheInfo = (algorithm_definition_info){0};
    TheRuleCount = 0;
    TheRuleCapacity = 0;
    TheCallCount = 0;
    TheCallCapacity = 0;
    TheBindCount = 0;
    TheBindCapacity = 0;
}
