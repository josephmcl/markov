#include "context.h"
#include "codepoint.h"

/* Forward declarations for context update functions in subdirectories */
extern syntax_store *_update_context_algorithm(
    syntax_store *, program_context_info *, program_context *);
extern syntax_store *_update_context_algorithm_call(
    syntax_store *, program_context_info *, program_context *);
extern syntax_store *_update_context_bind(
    syntax_store *, program_context_info *, program_context *);

/* Report an error with source location from a token */
static void report_error(lexical_store *tok, const char *msg) {
    if (tok != NULL) {
        fprintf(stderr, "%s:%u:%u: error: %s\n",
            Lex.file->name, tok->row, tok->column, msg);
    } else {
        fprintf(stderr, "error: %s\n", msg);
    }
}

static void report_warning(lexical_store *tok, const char *msg) {
    if (tok != NULL) {
        fprintf(stderr, "%s:%u:%u: warning: %s\n",
            Lex.file->name, tok->row, tok->column, msg);
    } else {
        fprintf(stderr, "warning: %s\n", msg);
    }
}

/* Parse a NUMBER token into an integer */
static int parse_number_token(lexical_store *tok) {
    int n = 0;
    for (const uint8_t *p = tok->begin; p < tok->end; p++) {
        n = n * 10 + (*p - '0');
    }
    return n;
}

/* Evaluate a range AST node into a range_set.
 * Caller must free the returned values array. */
range_set evaluate_range(syntax_store *node) {
    range_set result = { 0, NULL };

    if (node == NULL) return result;

    if (node->type == ast_range_literal) {
        /* 0..5: content[0] = start, content[1] = end */
        if (node->size < 2 || !node->content[0] || !node->content[1])
            return result;
        int start = parse_number_token(Lex.store(node->content[0]->token_index));
        int end = parse_number_token(Lex.store(node->content[1]->token_index));
        if (end < start) return result;
        result.count = (size_t)(end - start + 1);
        result.values = (size_t *)malloc(sizeof(size_t) * result.count);
        for (int i = start; i <= end; i++) {
            result.values[i - start] = (size_t)i;
        }
    } else if (node->type == ast_range_function) {
        /* range(start, end, step): content[0..2] */
        if (node->size < 3 || !node->content[0] || !node->content[1] || !node->content[2])
            return result;
        int start = parse_number_token(Lex.store(node->content[0]->token_index));
        int end = parse_number_token(Lex.store(node->content[1]->token_index));
        int step = parse_number_token(Lex.store(node->content[2]->token_index));
        if (step <= 0 || end < start) return result;
        result.count = (size_t)((end - start) / step + 1);
        result.values = (size_t *)malloc(sizeof(size_t) * result.count);
        for (size_t i = 0; i < result.count; i++) {
            result.values[i] = (size_t)(start + (int)i * step);
        }
    }

    return result;
}

#define PROGRAM_CONTEXT_SIZE 64

program_context *TheContext = NULL;

static program_context_info TheInfo = { 0 };

program_context *context_push(void) {

    size_t bytes;
    if (TheInfo.count == TheInfo.capacity) {
        TheInfo.capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(program_context) * TheInfo.capacity;
        TheContext = (program_context *) realloc(TheContext, bytes); 
    }

    TheInfo.count += 1;
    return TheContext + TheInfo.count - 1;
}

program_context *context_root(void) {
    return TheContext;
}

program_context *context_current(void) {
    return &TheContext[TheInfo.count];
}

syntax_store *_context_syntax_push(syntax_store *s) {

    size_t bytes;
    if (TheInfo.syntax_count == TheInfo.syntax_capacity) {
        TheInfo.syntax_capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(syntax_store *) * TheInfo.syntax_capacity;
        TheInfo.syntax_stack = (syntax_store **) realloc(TheInfo.syntax_stack, bytes); 
    }

    TheInfo.syntax_stack[TheInfo.syntax_count] = s;

    TheInfo.syntax_count += 1;
    return *(TheInfo.syntax_stack + TheInfo.syntax_count);
}

syntax_store *_context_syntax_pop(void) {

    size_t bytes;
    if (TheInfo.syntax_count == TheInfo.syntax_capacity) {
        TheInfo.syntax_capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(syntax_store *) * TheInfo.syntax_capacity;
        TheInfo.syntax_stack = (syntax_store **) realloc(TheInfo.syntax_stack, bytes); 
    }
    
    TheInfo.syntax_count -= 1;
    return *(TheInfo.syntax_stack + TheInfo.syntax_count);
}

program_context *_context_push_content(
    program_context *context, 
    program_context *content) {

    size_t bytes;
    if (context->content_count == context->content_capacity) {
        context->content_capacity += 64;

        bytes = sizeof(program_context *) * context->content_capacity;
        context->content = 
            (program_context **) realloc(context->content, bytes); 
    }
    
    context->content[context->content_count] = content;
    context->content_count += 1;
    return *(context->content + context->content_count - 1);
}


syntax_store *_update_context_scope(syntax_store *store) {
    
    /* Add a new program context to the stack. */
    program_context *current = context_push();
    current->syntax = store;
    current->letters_count = 0;
    current->letters_capacity = 0;
    current->letters = NULL;
    current->content_count = 0;
    current->content_capacity = 0;
    current->content = NULL;
    current->variables_count = 0;
    current->variables_capacity = 0;
    current->variables = NULL;
    current->alphabet_literals_count = 0;
    current->alphabet_literals_capacity = 0;
    current->alphabet_literals = NULL;
    current->algorithms_count = 0;
    current->algorithms_capacity = 0;
    current->algorithms = NULL;
    current->calls_count = 0;
    current->calls_capacity = 0;
    current->calls = NULL;
    current->binds_count = 0;
    current->binds_capacity = 0;
    current->binds = NULL;
    bool found = false;
    lexical_store *letter, *capture_lstore;
    syntax_store *capture_store;

    /* Determine the topic context. */
    program_context *topic;

    /* If there is only one context, it's the global context. We're
       done. */
    if (TheInfo.count <= 1) { 
        topic = NULL;
    }
    /* Otherwise check prior contexts for the nearest context that  
       contains the current context. */
    else {

        syntax_store *statements;
        for (size_t i = 1; i != TheInfo.count; ++i) {
        
            /* Get the next context. */
            topic = current - i;     

            /* NOTE: This probably shouldn't be possible, but let's 
                     check anyways. */
            if (topic->syntax->size == 0) continue; 

            /* Look through the syntax tree held by the current 
               context. */
            statements = topic->syntax->content[0];

            /* Continue if the tree is empty. */
            if (statements->size == 0) continue;

            /* Otherwise check if any of the syntax tree nodes in this 
               context are the current current context. */
            for (size_t j = 0; j != statements->size; ++j) {
                if (statements->content[j] == store) {
                    
                    /* Trigger the success variable for the outer 
                       loop. */
                    found = true;
                    break;
                }
            }

            /* Halt if we found a match. */
            if (found) break;
        }

        // TODO: Maybe this can be improved in the future with better 
        //       parsing infrastructure. 
    }


    // TODO: Will need better error propagation at some point. 
    if (TheInfo.count > 1 && !found) {
        printf("Error. Topic program context could not be found in "
               "AST.\n");
    }
    else {
        printf("Found parent context.\n");
    }

    /* Set the topic of the current context. */
    current->topic = topic;

     /* Set the capture of the current context. */
     // TODO: This should be its own function.
    capture_store = (current->syntax->size > 2) ? current->syntax->content[2] : NULL;
    if (capture_store == NULL) {
        current->capture = capture_pure;
    } 
    else {
        if (capture_store->type == ast_scope_context_names_literal) {
            capture_lstore = Lex.store(capture_store->token_index);
            if (capture_lstore->token == TOKEN_ATSIGN) {
                current->capture = capture_letters;
            }
            if (capture_lstore->token == TOKEN_EQUAL) {
                current->capture = capture_parent;
            }
        }
    }

    printf("Captured current context.\n");

    if (topic != NULL) {

        _context_push_content(topic, current);

        if (current->capture != capture_pure) {

            for (size_t i = 0; i < topic->letters_count; ++i) {
                letter = topic->letters[i];
                _update_known_context_letter(letter, current);
            }
        }
    }

    printf("Updated current context.\n");

    // TODO: Propagate variable information
    

    return NULL;
}

/* ========================================================================
 * Compile-time Markov Algorithm Interpreter
 * Used for bounded equivalence checking (::[r]~ and ::[r]≈)
 * ======================================================================== */

#define INTERP_MAX_WORD 1024
#define INTERP_MAX_STEPS 10000
#define INTERP_MAX_TRACE 256
#define INTERP_MAX_TRACE_ENTRY 256

typedef struct {
    int status;       /* 0=matched, 1=terminated, 2=no_match, 3=max_steps */
    size_t steps;
    size_t trace_count;
    /* Emit trace: array of strings (simplified — just store has_emit flag per step) */
    bool trace_emitted[INTERP_MAX_TRACE];
    int trace_rule[INTERP_MAX_TRACE];  /* which rule fired */
} interp_result;

/* Match a pattern (sequence of token bytes) against word at position pos.
 * Returns pattern length in word indices if matched, 0 if not.
 * Pattern is from the algorithm's rule, decomposed to raw bytes from tokens. */
static size_t match_pattern_at(
    const uint8_t *word, size_t word_len, size_t pos,
    algorithm_rule *rule)
{
    if (rule->pattern == NULL) return 0;
    size_t tok_idx = rule->pattern->token_index;
    size_t tok_count = rule->pattern->size + 1;
    size_t scanned = 0;
    size_t pat_pos = 0;

    /* Collect pattern bytes */
    uint8_t pat_bytes[256];
    size_t pat_len = 0;
    for (size_t t = tok_idx; scanned < tok_count; t++) {
        lexical_store *tok = Lex.store(t);
        if (tok->token != TOKEN_IDENTIFIER) continue;
        scanned++;
        size_t tlen = tok->end - tok->begin;
        memcpy(&pat_bytes[pat_len], tok->begin, tlen);
        pat_len += tlen;
    }

    if (pos + pat_len > word_len) return 0;
    if (memcmp(&word[pos], pat_bytes, pat_len) == 0) return pat_len;
    return 0;
}

/* Get replacement bytes from a rule */
static size_t get_replacement_bytes(algorithm_rule *rule, uint8_t *out, size_t max) {
    if (rule->replacement == NULL) return 0;
    size_t tok_idx = rule->replacement->token_index;
    size_t tok_count = rule->replacement->size + 1;
    size_t scanned = 0;
    size_t len = 0;
    for (size_t t = tok_idx; scanned < tok_count; t++) {
        lexical_store *tok = Lex.store(t);
        if (tok->token != TOKEN_IDENTIFIER) continue;
        scanned++;
        size_t tlen = tok->end - tok->begin;
        if (len + tlen > max) break;
        memcpy(&out[len], tok->begin, tlen);
        len += tlen;
    }
    return len;
}

/* Run a Markov algorithm on a word. Returns result with trace info. */
static interp_result interpret_algorithm(
    algorithm_definition *alg,
    const uint8_t *input, size_t input_len)
{
    interp_result result = { 0 };
    uint8_t word[INTERP_MAX_WORD];
    size_t word_len = input_len;

    if (input_len > INTERP_MAX_WORD) {
        result.status = 3;
        return result;
    }
    memcpy(word, input, input_len);

    for (result.steps = 0; result.steps < INTERP_MAX_STEPS; result.steps++) {
        bool matched = false;
        bool terminated = false;

        /* Try each rule in order */
        for (size_t r = 0; r < alg->rules_count; r++) {
            algorithm_rule *rule = alg->rules[r];

            /* Scan for pattern match */
            for (size_t pos = 0; pos + 1 <= word_len + 1; pos++) {
                size_t pat_len = match_pattern_at(word, word_len, pos, rule);
                if (pat_len > 0) {
                    /* Record emit */
                    if (result.trace_count < INTERP_MAX_TRACE) {
                        result.trace_emitted[result.trace_count] = rule->has_emit;
                        result.trace_rule[result.trace_count] = (int)r;
                        result.trace_count++;
                    }

                    if (rule->is_terminal) {
                        terminated = true;
                        matched = true;
                    } else {
                        /* Perform substitution */
                        uint8_t repl[256];
                        size_t repl_len = get_replacement_bytes(rule, repl, 256);
                        int diff = (int)repl_len - (int)pat_len;

                        /* Shift tail */
                        if (diff != 0) {
                            memmove(&word[pos + repl_len],
                                    &word[pos + pat_len],
                                    word_len - pos - pat_len);
                        }
                        /* Write replacement */
                        memcpy(&word[pos], repl, repl_len);
                        word_len = (size_t)((int)word_len + diff);
                        matched = true;
                    }
                    break; /* first match fires */
                }
            }
            if (matched) break;
        }

        if (terminated) { result.status = 1; return result; }
        if (!matched) { result.status = 2; return result; }
    }

    result.status = 3; /* max steps */
    return result;
}

/* Enumerate all words of a given length over an alphabet of size K.
 * Calls callback for each word. */
typedef bool (*word_callback)(const uint8_t *word, size_t word_len, void *ctx);

static void enumerate_words(
    const uint8_t **letters, const size_t *letter_lens, size_t num_letters,
    size_t word_length, word_callback cb, void *ctx)
{
    if (word_length == 0) {
        cb(NULL, 0, ctx);
        return;
    }

    /* Use indices array to enumerate all combinations */
    size_t indices[64] = {0};
    uint8_t word[INTERP_MAX_WORD];
    if (word_length > 64) return;

    for (;;) {
        /* Build word from indices */
        size_t wpos = 0;
        for (size_t i = 0; i < word_length; i++) {
            size_t li = indices[i];
            if (wpos + letter_lens[li] > INTERP_MAX_WORD) goto next;
            memcpy(&word[wpos], letters[li], letter_lens[li]);
            wpos += letter_lens[li];
        }

        if (!cb(word, wpos, ctx)) return; /* callback returns false to stop */

    next:
        /* Increment indices (like counting in base num_letters) */
        size_t carry = word_length;
        while (carry > 0) {
            carry--;
            indices[carry]++;
            if (indices[carry] < num_letters) break;
            indices[carry] = 0;
            if (carry == 0) return; /* all combinations exhausted */
        }
    }
}

/* Context for observational equivalence checking */
typedef struct {
    algorithm_definition *alg1;
    algorithm_definition *alg2;
    bool equivalent;
    uint8_t counterexample[INTERP_MAX_WORD];
    size_t counterexample_len;
    size_t words_checked;
} obs_equiv_ctx;

static bool check_obs_equiv_word(const uint8_t *word, size_t word_len, void *ctx_ptr) {
    obs_equiv_ctx *ctx = (obs_equiv_ctx *)ctx_ptr;
    ctx->words_checked++;

    interp_result r1 = interpret_algorithm(ctx->alg1, word, word_len);
    interp_result r2 = interpret_algorithm(ctx->alg2, word, word_len);

    /* Compare: same status, same trace (emit pattern) */
    if (r1.status != r2.status ||
        r1.trace_count != r2.trace_count) {
        ctx->equivalent = false;
        if (word != NULL && word_len <= INTERP_MAX_WORD) {
            memcpy(ctx->counterexample, word, word_len);
        }
        ctx->counterexample_len = word_len;
        return false; /* stop */
    }

    for (size_t i = 0; i < r1.trace_count; i++) {
        if (r1.trace_emitted[i] != r2.trace_emitted[i]) {
            ctx->equivalent = false;
            if (word != NULL && word_len <= INTERP_MAX_WORD) {
                memcpy(ctx->counterexample, word, word_len);
            }
            ctx->counterexample_len = word_len;
            return false;
        }
    }

    return true; /* continue */
}

/* Find an algorithm definition by name in a context */
static algorithm_definition *find_algorithm_by_name(
    program_context *ctx, const uint8_t *name, size_t name_len) {
    for (size_t i = 0; i < ctx->algorithms_count; i++) {
        algorithm_definition *alg = ctx->algorithms[i];
        if (alg->name != NULL) {
            size_t alen = alg->name->end - alg->name->begin;
            if (alen == name_len &&
                memcmp(alg->name->begin, name, name_len) == 0) {
                return alg;
            }
        }
    }
    return NULL;
}

/* Compare two patterns for equality (same token content) */
static bool patterns_equal(syntax_store *p1, syntax_store *p2) {
    if (p1 == NULL && p2 == NULL) return true;
    if (p1 == NULL || p2 == NULL) return false;
    if (p1->size != p2->size) return false;

    /* Compare each token in the pattern */
    size_t count1 = p1->size + 1;
    size_t count2 = p2->size + 1;
    if (count1 != count2) return false;

    size_t ti1 = p1->token_index;
    size_t ti2 = p2->token_index;
    size_t scanned = 0;

    for (size_t t = 0; scanned < count1; t++) {
        lexical_store *tok1 = Lex.store(ti1 + t);
        lexical_store *tok2 = Lex.store(ti2 + t);
        if (tok1->token != TOKEN_IDENTIFIER || tok2->token != TOKEN_IDENTIFIER) continue;
        scanned++;
        size_t len1 = tok1->end - tok1->begin;
        size_t len2 = tok2->end - tok2->begin;
        if (len1 != len2) return false;
        if (memcmp(tok1->begin, tok2->begin, len1) != 0) return false;
    }
    return true;
}

/* Print a statement-level emit string with ~result interpolation */
static void print_statement_emit(syntax_store *store, const char *result) {
    if (store->size < 4 || store->content[3] == NULL) return;

    lexical_store *emit_tok = Lex.store(store->content[3]->token_index);
    /* Strip quotes */
    const uint8_t *str = emit_tok->begin + 1;
    size_t str_len = (emit_tok->end - emit_tok->begin) - 2;

    /* Simple interpolation: replace ~result with the result string */
    for (size_t i = 0; i < str_len; i++) {
        if (str[i] == '~') {
            if (i + 6 <= str_len && memcmp(&str[i+1], "result", 6) == 0) {
                printf("%s", result);
                i += 6;
                continue;
            }
            /* Bare ~ = ~result for statement emit */
            printf("%s", result);
            continue;
        }
        putchar(str[i]);
    }
    putchar('\n');
}

/* Handle equivalence statements */
static syntax_store *_update_context_equivalence(syntax_store *store) {
    if (store->size < 2 || store->content[0] == NULL || store->content[1] == NULL)
        return NULL;

    lexical_store *left_tok = Lex.store(store->content[0]->token_index);
    lexical_store *right_tok = Lex.store(store->content[1]->token_index);
    size_t left_len = left_tok->end - left_tok->begin;
    size_t right_len = right_tok->end - right_tok->begin;

    /* Determine equivalence type from token_index */
    lexical_store *op_tok = Lex.store(store->token_index);

    if (op_tok->token == TOKEN_RULE_EQ) {
        /* ::= rule equivalence — compile-time syntactic comparison */
        /* Find both algorithms across all contexts */
        algorithm_definition *alg1 = NULL, *alg2 = NULL;
        for (size_t ci = 0; ci < TheInfo.count && (alg1 == NULL || alg2 == NULL); ci++) {
            if (alg1 == NULL)
                alg1 = find_algorithm_by_name(&TheContext[ci], left_tok->begin, left_len);
            if (alg2 == NULL)
                alg2 = find_algorithm_by_name(&TheContext[ci], right_tok->begin, right_len);
        }

        if (alg1 == NULL) {
            char msg[256];
            snprintf(msg, sizeof(msg), "algorithm '%.*s' not found for ::= comparison",
                (int)left_len, left_tok->begin);
            report_error(left_tok, msg);
            return NULL;
        }
        if (alg2 == NULL) {
            char msg[256];
            snprintf(msg, sizeof(msg), "algorithm '%.*s' not found for ::= comparison",
                (int)right_len, right_tok->begin);
            report_error(right_tok, msg);
            return NULL;
        }

        {
            char result_buf[256];
            const char *result = NULL;

            /* Compare rule counts */
            if (alg1->rules_count != alg2->rules_count) {
                snprintf(result_buf, sizeof(result_buf),
                    "differs (rule count: %zu vs %zu)",
                    alg1->rules_count, alg2->rules_count);
                result = result_buf;
            }

            /* Compare each rule */
            if (result == NULL) {
                for (size_t i = 0; i < alg1->rules_count; i++) {
                    algorithm_rule *r1 = alg1->rules[i];
                    algorithm_rule *r2 = alg2->rules[i];

                    if (r1->is_terminal != r2->is_terminal) {
                        snprintf(result_buf, sizeof(result_buf),
                            "differs (rule %zu: terminal mismatch)", i + 1);
                        result = result_buf; break;
                    }
                    if (!patterns_equal(r1->pattern, r2->pattern)) {
                        snprintf(result_buf, sizeof(result_buf),
                            "differs (rule %zu: pattern mismatch)", i + 1);
                        result = result_buf; break;
                    }
                    if (!patterns_equal(r1->replacement, r2->replacement)) {
                        snprintf(result_buf, sizeof(result_buf),
                            "differs (rule %zu: replacement mismatch)", i + 1);
                        result = result_buf; break;
                    }
                }
            }

            if (result == NULL) {
                snprintf(result_buf, sizeof(result_buf),
                    "equivalent (%zu rules)", alg1->rules_count);
                result = result_buf;
            }

            printf("%.*s ::= %.*s → %s\n",
                (int)left_len, left_tok->begin,
                (int)right_len, right_tok->begin, result);
            print_statement_emit(store, result);
        }

    } else if (op_tok->token == TOKEN_TILDE ||
               op_tok->token == TOKEN_APPROX ||
               op_tok->token == TOKEN_DOUBLE_TILDE) {
        /* ::[r]~ observational equivalence or ::[r]≈ bisimulation */
        const char *op_name = (op_tok->token == TOKEN_TILDE) ? "~" :
                              (op_tok->token == TOKEN_APPROX) ? "≈" : "~~";

        /* Get the range from content[2] */
        if (store->content[2] == NULL) {
            report_error(op_tok, "bounded equivalence requires a range");
            return NULL;
        }
        range_set rs = evaluate_range(store->content[2]);
        if (rs.count == 0) {
            report_error(op_tok, "empty range for equivalence check");
            return NULL;
        }

        /* Find both algorithms */
        algorithm_definition *alg1 = NULL, *alg2 = NULL;
        for (size_t ci = 0; ci < TheInfo.count && (alg1 == NULL || alg2 == NULL); ci++) {
            if (alg1 == NULL)
                alg1 = find_algorithm_by_name(&TheContext[ci], left_tok->begin, left_len);
            if (alg2 == NULL)
                alg2 = find_algorithm_by_name(&TheContext[ci], right_tok->begin, right_len);
        }

        if (alg1 == NULL || alg2 == NULL) {
            char msg[256];
            snprintf(msg, sizeof(msg), "algorithm not found for ::%s comparison", op_name);
            report_error(op_tok, msg);
            free(rs.values);
            return NULL;
        }

        /* Get the concrete alphabet letters for enumeration.
         * Use the first algorithm's context letters. */
        program_context *alg_ctx = alg1->context;
        if (alg_ctx == NULL || alg_ctx->letters_count == 0) {
            printf("Error: no letters in context for enumeration\n");
            free(rs.values);
            return NULL;
        }

        const uint8_t *letters[64];
        size_t letter_lens[64];
        size_t num_letters = alg_ctx->letters_count;
        if (num_letters > 64) num_letters = 64;
        for (size_t i = 0; i < num_letters; i++) {
            letters[i] = alg_ctx->letters[i]->begin;
            letter_lens[i] = alg_ctx->letters[i]->end - alg_ctx->letters[i]->begin;
        }

        /* Enumerate all words for each length in the range */
        obs_equiv_ctx ectx;
        ectx.alg1 = alg1;
        ectx.alg2 = alg2;
        ectx.equivalent = true;
        ectx.counterexample_len = 0;
        ectx.words_checked = 0;

        for (size_t ri = 0; ri < rs.count && ectx.equivalent; ri++) {
            size_t wlen = rs.values[ri];
            enumerate_words(letters, letter_lens, num_letters, wlen,
                check_obs_equiv_word, &ectx);
        }

        {
            char result_buf[256];
            if (ectx.equivalent) {
                snprintf(result_buf, sizeof(result_buf),
                    "equivalent (checked %zu words)", ectx.words_checked);
            } else {
                snprintf(result_buf, sizeof(result_buf),
                    "counterexample: \"%.*s\"",
                    (int)ectx.counterexample_len,
                    (const char*)ectx.counterexample);
            }
            printf("%.*s ::%s %.*s → %s\n",
                (int)left_len, left_tok->begin, op_name,
                (int)right_len, right_tok->begin, result_buf);
            print_statement_emit(store, result_buf);
        }

        free(rs.values);
    }

    return NULL;
}

syntax_store *update_program_context(syntax_store *store) {
    switch (store->type) {
    case ast_statements:
        return NULL;
    case ast_program:
        return _update_context_scope(store);
    case ast_scope:
        return _update_context_scope(store);
    case ast_letter:
        return _update_context_letter(store, &TheInfo, TheContext);
    case ast_alphabet_body:
        return _update_context_alphabet_literal(store, &TheInfo, TheContext);
    case ast_variable:
        return _update_context_variable(store, &TheInfo, TheContext);
    case ast_algorithm:
        return _update_context_algorithm(store, &TheInfo, TheContext);
    case ast_algorithm_call:
        return _update_context_algorithm_call(store, &TheInfo, TheContext);
    case ast_bind_expression:
        return _update_context_bind(store, &TheInfo, TheContext);
    case ast_equivalence:
        return NULL;  /* Handled in validate phase, after all algorithms are registered */
    case ast_range_literal:
    case ast_range_function: {
        range_set rs = evaluate_range(store);
        if (rs.count > 0) {
            printf("Range evaluated: {");
            for (size_t i = 0; i < rs.count; i++) {
                printf("%s%zu", i > 0 ? ", " : "", rs.values[i]);
            }
            printf("} (%zu values)\n", rs.count);
            free(rs.values);
        }
        return NULL;
    }
    default:
        return NULL; }
}

/* Find the index of a letter in the context's letters array.
   Returns the index if found, or context->letters_count if not found. */
size_t _context_find_letter_index(
    program_context *context,
    lexical_store   *letter) {

    lexical_store *current;
    size_t size = letter->end - letter->begin;

    for (size_t i = 0; i < context->letters_count; ++i) {
        current = context->letters[i];
        size_t current_size = current->end - current->begin;
        if (size == current_size &&
            memcmp(letter->begin, current->begin, size) == 0) {
            return i;
        }
    }
    return context->letters_count;
}

void update_program_context_letter_data(program_context *context) {

    alphabet_literal *alphabet;
    syntax_store *alphabet_body, *letters, *letter_node;
    lexical_store *letter;
    size_t letter_index;

    for (size_t i = 0; i < context->alphabet_literals_count; ++i) {
        alphabet = context->alphabet_literals[i];
        alphabet->letter_mask = 0;

        /* alphabet->store is the ast_alphabet_body node */
        alphabet_body = alphabet->store;
        if (alphabet_body == NULL || alphabet_body->size == 0) continue;

        /* content[0] is the ast_letters node */
        letters = alphabet_body->content[0];
        if (letters == NULL) continue;

        /* Iterate through each letter in this alphabet */
        for (size_t j = 0; j < letters->size; ++j) {
            letter_node = letters->content[j];
            if (letter_node == NULL) continue;

            letter = Lex.store(letter_node->token_index);
            letter_index = _context_find_letter_index(context, letter);

            if (letter_index < context->letters_count && letter_index < 64) {
                alphabet->letter_mask |= (1ULL << letter_index);
            }
        }
    }
}

/* Find the alphabet_literal associated with an ast_alphabet_body node. */
alphabet_literal *_find_alphabet_literal_for_syntax(
    program_context *context,
    syntax_store    *syntax) {

    for (size_t i = 0; i < context->alphabet_literals_count; ++i) {
        if (context->alphabet_literals[i]->store == syntax) {
            return context->alphabet_literals[i];
        }
    }
    return NULL;
}

/* Compare two variable names by their lexical tokens. */
static bool _variables_match(syntax_store *var1, syntax_store *var2) {
    if (var1 == NULL || var2 == NULL) return false;
    if (var1->type != ast_variable || var2->type != ast_variable) return false;

    lexical_store *lex1 = Lex.store(var1->token_index);
    lexical_store *lex2 = Lex.store(var2->token_index);

    size_t size1 = lex1->end - lex1->begin;
    size_t size2 = lex2->end - lex2->begin;

    if (size1 != size2) return false;
    return memcmp(lex1->begin, lex2->begin, size1) == 0;
}

/* Find the r_expression assigned to a variable by searching the AST.
   Searches for ast_assignment_statement nodes where content[0] matches
   the variable name. Returns the r_expression (content[1]) if found. */
syntax_store *_find_variable_assignment(syntax_store *var) {
    if (var == NULL || var->type != ast_variable) return NULL;

    syntax_store *tree = Syntax.tree();

    for (size_t i = 0; i < Syntax.info->count; ++i) {
        syntax_store *node = tree - i;
        if (node == NULL) continue;

        if (node->type == ast_assignment_statement && node->size >= 2) {
            /* content[0] is l_expression (variable), content[1] is r_expression */
            syntax_store *l_expr = node->content[0];

            /* l_expression wraps variable, check if it matches */
            if (l_expr != NULL && _variables_match(l_expr, var)) {
                return node->content[1];
            }
        }
    }
    return NULL;
}

/* Lazily compute the letter mask for any r_expression.
   This supports nested expressions like: A extends B union C */
uint64_t _compute_expression_mask(
    program_context *context,
    syntax_store    *expr) {

    if (expr == NULL) return 0;

    /* Base case: alphabet literal */
    if (expr->type == ast_alphabet_body) {
        alphabet_literal *alph = _find_alphabet_literal_for_syntax(context, expr);
        return alph ? alph->letter_mask : 0;
    }

    /* Binary operations: recursively compute both sides */
    if (expr->type == ast_union_expression) {
        uint64_t left = _compute_expression_mask(context, expr->content[0]);
        uint64_t right = _compute_expression_mask(context, expr->content[1]);
        return left | right;
    }

    if (expr->type == ast_intersect_expression) {
        uint64_t left = _compute_expression_mask(context, expr->content[0]);
        uint64_t right = _compute_expression_mask(context, expr->content[1]);
        return left & right;
    }

    if (expr->type == ast_difference_expression) {
        uint64_t left = _compute_expression_mask(context, expr->content[0]);
        uint64_t right = _compute_expression_mask(context, expr->content[1]);
        return left & ~right;
    }

    /* Extends expression: returns the left operand's mask after validation */
    if (expr->type == ast_extends_expression) {
        return _compute_expression_mask(context, expr->content[0]);
    }

    /* Variable lookup: find the variable's assigned r_expression */
    if (expr->type == ast_variable) {
        syntax_store *assigned = _find_variable_assignment(expr);
        if (assigned != NULL) {
            return _compute_expression_mask(context, assigned);
        }
        return 0;
    }

    return 0;
}

/* Recursively get the alphabet_literal for an r_expression.
   For binary expressions, returns the left operand's alphabet. */
alphabet_literal *_get_alphabet_for_expression(
    program_context *context,
    syntax_store    *expr) {

    if (expr == NULL) return NULL;

    if (expr->type == ast_alphabet_body) {
        return _find_alphabet_literal_for_syntax(context, expr);
    }
    else if (expr->type == ast_extends_expression ||
             expr->type == ast_union_expression ||
             expr->type == ast_intersect_expression ||
             expr->type == ast_difference_expression) {
        /* For binary ops, return the left operand's alphabet */
        return _get_alphabet_for_expression(context, expr->content[0]);
    }
    return NULL;
}

/* Validate extends expressions. B ⊂ A checks that B contains all letters of A.
   Validation: (A.mask & B.mask) == A.mask
   Uses lazy mask computation to support nested expressions like: A extends B union C */
bool _validate_extends_expression(
    program_context *context,
    syntax_store    *expr) {

    if (expr == NULL || expr->type != ast_extends_expression) return true;

    syntax_store *left_expr = expr->content[0];   /* B (the superset) */
    syntax_store *right_expr = expr->content[1];  /* A (the subset) */

    /* Use lazy computation for both sides */
    uint64_t left_mask = _compute_expression_mask(context, left_expr);
    uint64_t right_mask = _compute_expression_mask(context, right_expr);

    /* Check if right (A) is a subset of left (B): (A & B) == A */
    if ((right_mask & left_mask) != right_mask) {
        printf("Error: alphabet extension validation failed.\n");
        printf("       Left expression (mask 0x%llx) does not contain all letters of right expression (mask 0x%llx).\n",
               left_mask, right_mask);
        return false;
    }

    printf("Extends validation passed: 0x%llx ⊃ 0x%llx\n", left_mask, right_mask);
    return true;
}

/* Compute and print set operation results using lazy evaluation.
   Union: A ∪ B = A.mask | B.mask
   Intersection: A ∩ B = A.mask & B.mask
   Difference: A \ B = A.mask & ~B.mask
   Supports nested expressions like: (A union B) intersect C */
void _compute_set_operation(
    program_context *context,
    syntax_store    *expr) {

    if (expr == NULL) return;

    syntax_store *left_expr = expr->content[0];
    syntax_store *right_expr = expr->content[1];

    /* Use lazy computation for both sides */
    uint64_t left_mask = _compute_expression_mask(context, left_expr);
    uint64_t right_mask = _compute_expression_mask(context, right_expr);
    uint64_t result_mask = 0;
    const char *op_name = "";

    switch (expr->type) {
    case ast_union_expression:
        result_mask = left_mask | right_mask;
        op_name = "∪";
        break;
    case ast_intersect_expression:
        result_mask = left_mask & right_mask;
        op_name = "∩";
        break;
    case ast_difference_expression:
        result_mask = left_mask & ~right_mask;
        op_name = "\\";
        break;
    default:
        return;
    }

    printf("Set operation: 0x%llx %s 0x%llx = 0x%llx\n",
           left_mask, op_name, right_mask, result_mask);
}

/* Validate word_in_expression: check that every letter in the word
   exists in the alphabet.
   P = "abc" in A checks that a, b, c are all letters of A. */
bool _validate_word_in_expression(
    program_context *context,
    syntax_store    *expr) {

    if (expr == NULL || expr->type != ast_word_in_expression) return true;
    if (expr->size < 2) return false;

    syntax_store *word_node = expr->content[0];      /* word_literal */
    syntax_store *alphabet_expr = expr->content[1];  /* r_expression (alphabet) */

    if (word_node == NULL || word_node->type != ast_word_literal) {
        printf("Error: word_in_expression missing word literal.\n");
        return false;
    }

    /* Get the word string (includes quotes) */
    lexical_store *word_lex = Lex.store(word_node->token_index);
    uint8_t *word_begin = word_lex->begin + 1;  /* skip opening quote */
    uint8_t *word_end = word_lex->end - 1;      /* skip closing quote */
    size_t word_len = word_end - word_begin;

    /* Get the alphabet's mask */
    uint64_t alphabet_mask = _compute_expression_mask(context, alphabet_expr);

    printf("Word validation: \"");
    for (uint8_t *p = word_begin; p < word_end; ) {
        int cp_len = utf8_code_point_length(*p);
        printf("%.*s", cp_len, p);
        p += cp_len;
    }
    printf("\" in alphabet (mask 0x%llx)\n", alphabet_mask);

    /* Check each letter in the word against the context's letters */
    bool valid = true;
    uint8_t *p = word_begin;
    while (p < word_end) {
        int cp_len = utf8_code_point_length(*p);

        /* Find this letter in the context's letters array */
        bool found = false;
        size_t letter_index = 0;
        for (size_t i = 0; i < context->letters_count; ++i) {
            lexical_store *letter = context->letters[i];
            size_t letter_len = letter->end - letter->begin;
            if ((size_t)cp_len == letter_len &&
                memcmp(p, letter->begin, letter_len) == 0) {
                found = true;
                letter_index = i;
                break;
            }
        }

        if (!found) {
            printf("  Error: letter '%.*s' not found in context.\n", cp_len, p);
            valid = false;
        } else {
            /* Check if this letter is in the alphabet */
            if (letter_index < 64 && !(alphabet_mask & (1ULL << letter_index))) {
                printf("  Error: letter '%.*s' (index %zu) not in alphabet.\n",
                       cp_len, p, letter_index);
                valid = false;
            } else {
                printf("  Letter '%.*s' (index %zu) OK.\n", cp_len, p, letter_index);
            }
        }

        p += cp_len;
    }

    return valid;
}

/* Validate all expressions in a context. */
void _validate_context_expressions(program_context *context) {
    syntax_store *tree = Syntax.tree();

    for (size_t i = 0; i < Syntax.info->count; ++i) {
        syntax_store *current = tree - i;
        if (current == NULL) continue;

        if (current->type == ast_extends_expression) {
            _validate_extends_expression(context, current);
        }
        else if (current->type == ast_union_expression ||
                 current->type == ast_intersect_expression ||
                 current->type == ast_difference_expression) {
            _compute_set_operation(context, current);
        }
        else if (current->type == ast_word_in_expression) {
            _validate_word_in_expression(context, current);
        }
    }
}

void validate_program_context (void) {

    lexical_store *lstore;
    syntax_store *tree, *current;
    program_context *topic = NULL;
    tree = Syntax.tree();

    for (size_t i = 0; i < Syntax.info->count; ++i) {
        current = tree - i;
        if (current != NULL) {
            // _print_node_string(current->type);
            update_program_context(current);
        }
    }

    for (size_t i = 0; i < TheInfo.count; ++i) {
        update_program_context_letter_data(context_root() + i);
    }

    /* Validate expressions after letter masks are computed */
    for (size_t i = 0; i < TheInfo.count; ++i) {
        _validate_context_expressions(context_root() + i);
    }

    /* Process equivalence statements (deferred from AST walk so
       algorithms are registered first) */
    for (size_t i = 0; i < Syntax.info->count; ++i) {
        current = tree - i;
        if (current != NULL && current->type == ast_equivalence) {
            _update_context_equivalence(current);
        }
    }


    /* Termination analysis for all algorithms */
    for (size_t ci = 0; ci < TheInfo.count; ci++) {
        program_context *ctx = &TheContext[ci];
        for (size_t ai = 0; ai < ctx->algorithms_count; ai++) {
            algorithm_definition *alg = ctx->algorithms[ai];
            if (alg == NULL || alg->name == NULL || alg->rules_count == 0) continue;

            int name_len = (int)(alg->name->end - alg->name->begin);
            bool has_terminal = false;
            bool all_rules_checked = true;
            const char *witness_pattern = NULL;
            size_t witness_pattern_len = 0;

            /* Check if there's at least one terminal rule */
            for (size_t r = 0; r < alg->rules_count; r++) {
                if (alg->rules[r]->is_terminal) { has_terminal = true; break; }
            }

            if (!has_terminal) {
                /* No terminal rule — check if all rules shrink the word */
                bool all_shrink = true;
                for (size_t r = 0; r < alg->rules_count; r++) {
                    algorithm_rule *rule = alg->rules[r];
                    if (rule->pattern == NULL) { all_shrink = false; break; }

                    /* Get pattern and replacement lengths in tokens */
                    size_t pat_tokens = rule->pattern->size + 1;
                    size_t repl_tokens = 0;
                    if (rule->replacement != NULL) {
                        repl_tokens = rule->replacement->size + 1;
                    }
                    if (repl_tokens >= pat_tokens) { all_shrink = false; break; }
                }
                if (all_shrink) {
                    printf("%.*s: terminates (all rules shrink word length)\n",
                        name_len, alg->name->begin);
                    continue;
                }

                /* No terminal and not all shrinking — warn */
                fprintf(stderr, "%.*s: warning: no termination witness found "
                    "(no terminal rule, word length not decreasing)\n",
                    name_len, alg->name->begin);
                continue;
            }

            /* Has terminal rule(s). Try candidate measures.
             * For each non-terminal rule's LHS pattern, try count(pattern) as measure. */
            for (size_t r = 0; r < alg->rules_count && witness_pattern == NULL; r++) {
                algorithm_rule *rule = alg->rules[r];
                if (rule->is_terminal || rule->pattern == NULL) continue;

                /* Get this rule's pattern bytes as the candidate measure */
                uint8_t pat_bytes[256];
                size_t pat_len = 0;
                size_t tok_idx = rule->pattern->token_index;
                size_t tok_count = rule->pattern->size + 1;
                size_t scanned = 0;
                for (size_t t = tok_idx; scanned < tok_count; t++) {
                    lexical_store *tok = Lex.store(t);
                    if (tok->token != TOKEN_IDENTIFIER) continue;
                    scanned++;
                    size_t tlen = tok->end - tok->begin;
                    if (pat_len + tlen > 256) break;
                    memcpy(&pat_bytes[pat_len], tok->begin, tlen);
                    pat_len += tlen;
                }

                /* Check: does every non-terminal rule decrease count(pattern)?
                 * A rule P -> Q decreases count(X) if:
                 *   1. P contains X (the match removes one X)
                 *   2. Q does not contain X (the replacement doesn't add X back) */
                bool measure_works = true;
                for (size_t r2 = 0; r2 < alg->rules_count; r2++) {
                    algorithm_rule *rule2 = alg->rules[r2];
                    if (rule2->is_terminal) continue;
                    if (rule2->pattern == NULL) { measure_works = false; break; }

                    /* Get rule2's pattern bytes */
                    uint8_t r2_pat[256];
                    size_t r2_pat_len = 0;
                    size_t ti2 = rule2->pattern->token_index;
                    size_t tc2 = rule2->pattern->size + 1;
                    size_t sc2 = 0;
                    for (size_t t = ti2; sc2 < tc2; t++) {
                        lexical_store *tok = Lex.store(t);
                        if (tok->token != TOKEN_IDENTIFIER) continue;
                        sc2++;
                        size_t tlen = tok->end - tok->begin;
                        if (r2_pat_len + tlen > 256) break;
                        memcpy(&r2_pat[r2_pat_len], tok->begin, tlen);
                        r2_pat_len += tlen;
                    }

                    /* Check if rule2's pattern contains the measure pattern */
                    bool pat_contains = false;
                    if (r2_pat_len >= pat_len) {
                        for (size_t p = 0; p <= r2_pat_len - pat_len; p++) {
                            if (memcmp(&r2_pat[p], pat_bytes, pat_len) == 0) {
                                pat_contains = true; break;
                            }
                        }
                    }

                    /* Check if replacement contains the measure pattern.
                     * Even if this rule's pattern doesn't contain the measure,
                     * the replacement might CREATE the measure pattern. */
                    if (rule2->replacement != NULL) {
                        uint8_t r2_repl[256];
                        size_t r2_repl_len = 0;
                        size_t ti3 = rule2->replacement->token_index;
                        size_t tc3 = rule2->replacement->size + 1;
                        size_t sc3 = 0;
                        for (size_t t = ti3; sc3 < tc3; t++) {
                            lexical_store *tok = Lex.store(t);
                            if (tok->token != TOKEN_IDENTIFIER) continue;
                            sc3++;
                            size_t tlen = tok->end - tok->begin;
                            if (r2_repl_len + tlen > 256) break;
                            memcpy(&r2_repl[r2_repl_len], tok->begin, tlen);
                            r2_repl_len += tlen;
                        }

                        bool repl_contains = false;
                        if (r2_repl_len >= pat_len) {
                            for (size_t p = 0; p <= r2_repl_len - pat_len; p++) {
                                if (memcmp(&r2_repl[p], pat_bytes, pat_len) == 0) {
                                    repl_contains = true; break;
                                }
                            }
                        }

                        if (repl_contains) {
                            /* Replacement contains the pattern — measure doesn't decrease */
                            measure_works = false;
                            break;
                        }
                    }
                }

                if (measure_works) {
                    witness_pattern = (const char *)pat_bytes;
                    witness_pattern_len = pat_len;
                    printf("%.*s: terminates (witness: count(%.*s) decreasing)\n",
                        name_len, alg->name->begin,
                        (int)pat_len, pat_bytes);
                }
            }

            if (witness_pattern == NULL) {
                /* Check for obvious cycles */
                bool has_cycle = false;
                for (size_t r1 = 0; r1 < alg->rules_count && !has_cycle; r1++) {
                    for (size_t r2 = r1 + 1; r2 < alg->rules_count && !has_cycle; r2++) {
                        algorithm_rule *rule1 = alg->rules[r1];
                        algorithm_rule *rule2 = alg->rules[r2];
                        if (rule1->is_terminal || rule2->is_terminal) continue;
                        if (rule1->pattern == NULL || rule2->pattern == NULL) continue;
                        if (rule1->replacement == NULL || rule2->replacement == NULL) continue;

                        /* Check if rule1's replacement matches rule2's pattern
                           and rule2's replacement matches rule1's pattern */
                        if (patterns_equal(rule1->replacement, rule2->pattern) &&
                            patterns_equal(rule2->replacement, rule1->pattern)) {
                            has_cycle = true;
                        }
                    }
                }

                if (has_cycle) {
                    fprintf(stderr, "%.*s: warning: no termination witness found "
                        "(cycle detected between rules)\n",
                        name_len, alg->name->begin);
                } else {
                    fprintf(stderr, "%.*s: warning: no termination witness found\n",
                        name_len, alg->name->begin);
                }
            }
        }
    }

    // TODO: Remove.
    printf("\nContext report.\n");

    for (size_t i = 0; i < TheInfo.count; ++i) {
        printf("Context %lu\n| address   (%p)\n", i, (void *) TheContext[i].syntax);
        topic = TheContext[i].topic;
        if (topic == NULL) {
            printf("| parent   (NULL, root context) \n");
        }
        else {      
            printf("| parent    (%p)\n", (void *) topic->syntax);
            current = TheContext[i].syntax->content[1];
            if (current == NULL) {
                printf("| name      (anonymous)\n");
            }
            else {
                lstore = Lex.store(current->token_index);
                int size = (int) (lstore->end - lstore->begin);                        
                printf("| name      (%.*s)\n", size, lstore->begin);
            }
        }
        
        current = (TheContext[i].syntax->size > 2) ? TheContext[i].syntax->content[2] : NULL;

        if (TheContext[i].capture == capture_pure) {
            printf("| capture   (none)\n");
        }
        else if (TheContext[i].capture == capture_letters) {
            printf("| capture   (letters)\n");
        }
        else if (TheContext[i].capture == capture_parent) {
            printf("| capture   (parent)\n");
        }

        if (current == NULL) {
            printf("| capture   (none)\n");
        }
        else if (current->type == ast_scope_context_names_literal) {
            lstore = Lex.store(current->token_index);
            if (lstore->token == TOKEN_ATSIGN) {
                printf("| capture   (letters)\n");
            }
            if (lstore->token == TOKEN_EQUAL) {
                printf("| capture   (parent)\n");
            }
        }

        printf("| Alphabets (%lu)\n", TheContext[i].alphabet_literals_count);
        for (size_t j = 0; j < TheContext[i].alphabet_literals_count; ++j) {
            alphabet_literal *alph = TheContext[i].alphabet_literals[j];
            printf("| | mask (0x%016llx)\n", alph->letter_mask);
        }

        printf("| letters   (%lu)\n", TheContext[i].letters_count);
        for (size_t j = 0; j < TheContext[i].letters_count; ++j) {
            printf("| | size (%d) ", 
                TheContext[i].letters[j]->end - 
                TheContext[i].letters[j]->begin);
            printf("value (%.*s) ", 
                TheContext[i].letters[j]->end - 
                TheContext[i].letters[j]->begin, 
                TheContext[i].letters[j]->begin);
            printf("address (%p) \n",  
                TheContext[i].letters[j]->begin);
        }
        printf("| variables (%lu)\n", TheContext[i].variables_count);
        for (size_t j = 0; j < TheContext[i].variables_count; ++j) {
            printf("| | size (%d) ",
                TheContext[i].variables[j]->end -
                TheContext[i].variables[j]->begin);
            printf("value (%.*s) ",
                TheContext[i].variables[j]->end -
                TheContext[i].variables[j]->begin,
                TheContext[i].variables[j]->begin);
            printf("address (%p) \n",
                TheContext[i].variables[j]->begin);
        }

        printf("| algorithms (%lu)\n", TheContext[i].algorithms_count);
        for (size_t j = 0; j < TheContext[i].algorithms_count; ++j) {
            algorithm_definition *alg = TheContext[i].algorithms[j];
            if (alg->name != NULL) {
                int name_size = (int)(alg->name->end - alg->name->begin);
                printf("| | name (%.*s)\n", name_size, alg->name->begin);
            }
            if (alg->abstract_alph != NULL) {
                printf("| | alphabet [%zu] (abstract:", alg->abstract_alph->size);
                for (size_t k = 0; k < alg->abstract_alph->size; k++) {
                    printf(" %.*s", (int)alg->abstract_alph->names[k].len,
                        alg->abstract_alph->names[k].bytes);
                }
                printf(")\n");
            } else if (alg->alphabet_ref != NULL) {
                lexical_store *alph_lex = Lex.store(alg->alphabet_ref->token_index);
                int alph_size = (int)(alph_lex->end - alph_lex->begin);
                printf("| | alphabet (%.*s)\n", alph_size, alph_lex->begin);
            }
            printf("| | rules (%lu)\n", alg->rules_count);
            for (size_t k = 0; k < alg->rules_count; ++k) {
                algorithm_rule *rule = alg->rules[k];
                const char *type = rule->is_terminal ? "terminal" : "substitution";
                const char *emit = rule->has_emit ? " emit" : "";
                printf("| | | [%s%s]", type, emit);
                if (rule->rule_name != NULL) {
                    int rn_size = (int)(rule->rule_name->end - rule->rule_name->begin);
                    printf(" name(%.*s)", rn_size, rule->rule_name->begin);
                }
                if (rule->emit_string != NULL) {
                    int es_size = (int)(rule->emit_string->end - rule->emit_string->begin);
                    printf(" emit(%.*s)", es_size, rule->emit_string->begin);
                }
                printf("\n");
            }
        }
        printf("| calls (%lu)\n", TheContext[i].calls_count);
        for (size_t j = 0; j < TheContext[i].calls_count; ++j) {
            algorithm_call *call = TheContext[i].calls[j];
            int name_size = (int)(call->algorithm_name->end - call->algorithm_name->begin);
            printf("| | %.*s(", name_size, call->algorithm_name->begin);
            if (call->input_type == CALL_STDIN) {
                printf("~");
            } else if (call->input_token != NULL) {
                int tok_size = (int)(call->input_token->end - call->input_token->begin);
                printf("%.*s", tok_size, call->input_token->begin);
            }
            printf(")\n");
        }
        printf("| binds (%lu)\n", TheContext[i].binds_count);
        for (size_t j = 0; j < TheContext[i].binds_count; ++j) {
            alphabet_bind *bind = TheContext[i].binds[j];
            printf("| | ");
            if (bind->name != NULL) {
                int n = (int)(bind->name->end - bind->name->begin);
                printf("%.*s = ", n, bind->name->begin);
            }
            if (bind->is_universal) {
                printf(":> (universal)");
            } else {
                printf(":[%zu rules]>", bind->rules_count);
            }
            printf("\n");
        }
        printf("\n");
    }
    return;
}

/* Free all context-related allocations */
extern void algorithm_free(void);
extern void alphabet_literal_free(void);

void context_free(void) {
    /* Free per-context arrays */
    for (size_t i = 0; i < TheInfo.count; i++) {
        free(TheContext[i].content);
        free(TheContext[i].letters);
        free(TheContext[i].variables);
        free(TheContext[i].alphabet_literals);
        free(TheContext[i].algorithms);
        free(TheContext[i].calls);
        free(TheContext[i].binds);
    }
    free(TheContext);
    free(TheInfo.syntax_stack);
    TheContext = NULL;
    TheInfo = (program_context_info){0};

    /* Free sub-module allocations */
    algorithm_free();
    alphabet_literal_free();
}

const struct context Context = {
    .validate = validate_program_context,
    .free     = context_free
};
