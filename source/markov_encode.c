/* Bridge between the language compiler's in-memory algorithm_definition
 * (defined in include/context/definitions.h) and the portable bit-packed
 * record format. Kept separate from markov_record.c so the offline tools
 * (enumerator, GPU harness) can link against the record helpers without
 * dragging in the language front-end. */

#include "markov_record.h"

#include <string.h>

#include "context/definitions.h"
#include "lex.h"
#include "syntax.h"

/* Decompose a pattern AST node into a flat list of abstract letter indices,
 * by walking its IDENTIFIER tokens and greedy-matching against the
 * algorithm's abstract alphabet names.
 *
 * Returns the letter count on success, or -1 on failure (unrecognised
 * letter, or output overflow). */
static int encode_pattern_letters(const syntax_store *pattern,
                                  const abstract_alphabet *abs,
                                  uint8_t *out, int max_out) {
    if (pattern == NULL || abs == NULL || out == NULL) return -1;

    int total = 0;
    size_t tok_idx = pattern->token_index;
    size_t tokens_needed = pattern->size + 1;
    size_t tokens_found = 0;

    while (tokens_found < tokens_needed) {
        lexical_store *tok = Lex.store(tok_idx);
        if (tok == NULL) return -1;
        if (tok->token == TOKEN_IDENTIFIER) {
            /* The token bytes might be a multi-letter concatenation like
             * "ba" — decompose against abs->names with greedy longest match. */
            size_t pos = 0;
            size_t bytes_len = tok->end - tok->begin;
            while (pos < bytes_len) {
                int best_idx = -1;
                size_t best_len = 0;
                for (size_t ai = 0; ai < abs->size; ai++) {
                    size_t nlen = abs->names[ai].len;
                    if (nlen > best_len && pos + nlen <= bytes_len &&
                        memcmp(tok->begin + pos, abs->names[ai].bytes, nlen) == 0) {
                        best_idx = (int)ai;
                        best_len = nlen;
                    }
                }
                if (best_idx < 0) return -1;
                if (total >= max_out) return -1;
                out[total++] = (uint8_t)best_idx;
                pos += best_len;
            }
            tokens_found++;
        }
        tok_idx++;
    }
    return total;
}

bool markov_encode_algorithm(const struct adef *alg, MarkovAlgorithm *out) {
    if (alg == NULL || out == NULL) return false;
    if (alg->abstract_alph == NULL) return false;
    if (alg->abstract_alph->size == 0 ||
        alg->abstract_alph->size > MARKOV_MAX_ABSTRACT) return false;
    if (alg->rules_count > MARKOV_MAX_RULES) return false;

    memset(out, 0, sizeof(*out));
    markov_header_init(&out->header,
        (uint8_t)alg->abstract_alph->size,
        (uint8_t)alg->rules_count);
    if (out->header.bits_per_letter == 0) return false;

    for (size_t r = 0; r < alg->rules_count; r++) {
        algorithm_rule *rule = alg->rules[r];
        if (rule == NULL) return false;
        MarkovRule *mr = &out->rules[r];
        memset(mr, 0, sizeof(*mr));

        if (rule->is_terminal) mr->flags |= MARKOV_RULE_FLAG_TERMINAL;
        if (rule->has_emit)    mr->flags |= MARKOV_RULE_FLAG_HAS_EMIT;

        uint8_t pat[32];
        int n = encode_pattern_letters(rule->pattern, alg->abstract_alph, pat, 32);
        if (n <= 0) return false;
        mr->pattern_len = (uint8_t)n;
        if (!markov_pack_pattern(pat, mr->pattern_len,
                                 out->header.bits_per_letter, &mr->pattern_bits))
            return false;

        if (!rule->is_terminal && rule->replacement != NULL) {
            uint8_t rep[32];
            int m = encode_pattern_letters(rule->replacement, alg->abstract_alph, rep, 32);
            if (m < 0) return false;
            mr->replacement_len = (uint8_t)m;
            if (!markov_pack_pattern(rep, mr->replacement_len,
                                     out->header.bits_per_letter, &mr->replacement_bits))
                return false;
        }
    }
    return true;
}
