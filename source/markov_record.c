#include "markov_record.h"

#include <stdio.h>
#include <string.h>

#include "context/definitions.h"
#include "lex.h"
#include "syntax.h"

uint8_t markov_bits_per_letter(uint8_t abstract_size) {
    if (abstract_size <= 1) return 1;
    if (abstract_size <= 2) return 1;
    if (abstract_size <= 4) return 2;
    if (abstract_size <= 8) return 3;
    /* This version of the format does not support alphabets larger than 8;
     * markov_algorithm_valid will reject. Return a sentinel. */
    return 0;
}

void markov_header_init(MarkovHeader *h, uint8_t abstract_size, uint8_t rule_count) {
    if (h == NULL) return;
    memset(h, 0, sizeof(*h));
    h->magic = MARKOV_FORMAT_MAGIC;
    h->version_major = MARKOV_FORMAT_VERSION_MAJOR;
    h->version_minor = MARKOV_FORMAT_VERSION_MINOR;
    h->abstract_size = abstract_size;
    h->rule_count = rule_count;
    h->bits_per_letter = markov_bits_per_letter(abstract_size);
}

void markov_binding_identity(MarkovBinding *b, uint8_t abstract_size) {
    if (b == NULL) return;
    memset(b, 0, sizeof(*b));
    b->flags = MARKOV_BIND_FLAG_INJECTIVE | MARKOV_BIND_FLAG_TOTAL |
               MARKOV_BIND_FLAG_SURJECTIVE;
    b->abstract_size = abstract_size;
    b->target_size = abstract_size;
    for (uint8_t i = 0; i < MARKOV_MAX_ABSTRACT; i++) {
        b->map[i] = (i < abstract_size) ? i : MARKOV_BIND_NO_IMAGE;
    }
    /* Both masks cover indices 0..abstract_size-1. */
    if (abstract_size >= 64) {
        b->legal_input_mask = ~(uint64_t)0;
    } else {
        b->legal_input_mask = (((uint64_t)1) << abstract_size) - 1;
    }
    b->legal_replacement_mask = b->legal_input_mask;
}

void markov_policy_default(MarkovObservationPolicy *p, uint8_t lo, uint8_t hi) {
    if (p == NULL) return;
    memset(p, 0, sizeof(*p));
    p->length_lo = lo;
    p->length_hi = hi;
    p->sampling_kind = MARKOV_SAMPLE_EXHAUSTIVE;
    p->fingerprint_recipe = MARKOV_FP_IMAGE;
    p->sampling_param = 0;
}

bool markov_pack_pattern(const uint8_t *letters, uint8_t letter_count,
                         uint8_t bits_per_letter, uint32_t *out_bits) {
    if (out_bits == NULL || bits_per_letter == 0) return false;
    if ((uint32_t)letter_count * bits_per_letter > 32u) return false;

    uint32_t packed = 0;
    uint32_t mask = (bits_per_letter >= 32) ? ~0u : ((1u << bits_per_letter) - 1u);
    for (uint8_t i = 0; i < letter_count; i++) {
        uint32_t v = letters[i] & mask;
        packed |= v << (i * bits_per_letter);
    }
    *out_bits = packed;
    return true;
}

uint8_t markov_unpack_letter(uint32_t pattern_bits, uint8_t i, uint8_t bits_per_letter) {
    if (bits_per_letter == 0) return 0;
    uint32_t mask = (bits_per_letter >= 32) ? ~0u : ((1u << bits_per_letter) - 1u);
    return (uint8_t)((pattern_bits >> (i * bits_per_letter)) & mask);
}

bool markov_algorithm_valid(const MarkovAlgorithm *a) {
    if (a == NULL) return false;
    if (a->header.magic != MARKOV_FORMAT_MAGIC) return false;
    if (a->header.version_major != MARKOV_FORMAT_VERSION_MAJOR) return false;
    if (a->header.abstract_size == 0 ||
        a->header.abstract_size > MARKOV_MAX_ABSTRACT) return false;
    if (a->header.rule_count > MARKOV_MAX_RULES) return false;

    uint8_t bpl = markov_bits_per_letter(a->header.abstract_size);
    if (bpl == 0 || a->header.bits_per_letter != bpl) return false;

    /* Schema flag is reserved in 1.0 — must be unset. */
    if (a->header.flags & MARKOV_ALG_FLAG_USES_SCHEMA) return false;

    uint32_t max_pattern = bpl == 0 ? 0 : (32u / bpl);
    for (uint8_t r = 0; r < a->header.rule_count; r++) {
        const MarkovRule *rule = &a->rules[r];
        if (rule->flags & MARKOV_RULE_FLAG_SCHEMA) return false;
        if (rule->pattern_len == 0) return false;  /* empty pattern is meaningless */
        if (rule->pattern_len > max_pattern) return false;
        if (rule->replacement_len > max_pattern) return false;
        /* Letter indices must be < abstract_size. */
        for (uint8_t i = 0; i < rule->pattern_len; i++) {
            if (markov_unpack_letter(rule->pattern_bits, i, bpl) >=
                a->header.abstract_size) return false;
        }
        for (uint8_t i = 0; i < rule->replacement_len; i++) {
            if (markov_unpack_letter(rule->replacement_bits, i, bpl) >=
                a->header.abstract_size) return false;
        }
    }
    return true;
}

bool markov_binding_valid(const MarkovBinding *b, const MarkovAlgorithm *a) {
    if (b == NULL || a == NULL) return false;
    if (b->abstract_size != a->header.abstract_size) return false;
    if (b->target_size > 64) return false;
    /* Cross-check flags against the map. */
    bool injective_claimed  = (b->flags & MARKOV_BIND_FLAG_INJECTIVE) != 0;
    bool total_claimed      = (b->flags & MARKOV_BIND_FLAG_TOTAL) != 0;
    bool surjective_claimed = (b->flags & MARKOV_BIND_FLAG_SURJECTIVE) != 0;

    bool seen[64] = { false };
    bool any_no_image = false;
    for (uint8_t i = 0; i < b->abstract_size; i++) {
        uint8_t img = b->map[i];
        if (img == MARKOV_BIND_NO_IMAGE) {
            any_no_image = true;
            continue;
        }
        if (img >= b->target_size) return false;
        if (injective_claimed && seen[img]) return false;
        seen[img] = true;
    }
    if (any_no_image && total_claimed) return false;
    if (surjective_claimed) {
        for (uint8_t g = 0; g < b->target_size; g++) {
            if (!seen[g]) return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Encoding from algorithm_definition                                      */
/* ---------------------------------------------------------------------- */

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

/* ---------------------------------------------------------------------- */
/* Printing                                                                */
/* ---------------------------------------------------------------------- */

void markov_print_algorithm(const MarkovAlgorithm *a) {
    if (a == NULL) {
        printf("MarkovAlgorithm: <null>\n");
        return;
    }
    printf("MarkovAlgorithm v%u.%u abstract_size=%u rule_count=%u bits_per_letter=%u flags=0x%04x\n",
        a->header.version_major, a->header.version_minor,
        a->header.abstract_size, a->header.rule_count,
        a->header.bits_per_letter, a->header.flags);

    uint8_t bpl = a->header.bits_per_letter;
    for (uint8_t r = 0; r < a->header.rule_count; r++) {
        const MarkovRule *rule = &a->rules[r];
        printf("  rule[%u]:%s%s pattern=[", r,
            (rule->flags & MARKOV_RULE_FLAG_TERMINAL) ? " terminal" : "",
            (rule->flags & MARKOV_RULE_FLAG_HAS_EMIT) ? " emit" : "");
        for (uint8_t i = 0; i < rule->pattern_len; i++) {
            printf("%s%u", i ? " " : "",
                markov_unpack_letter(rule->pattern_bits, i, bpl));
        }
        printf("] replacement=[");
        for (uint8_t i = 0; i < rule->replacement_len; i++) {
            printf("%s%u", i ? " " : "",
                markov_unpack_letter(rule->replacement_bits, i, bpl));
        }
        printf("]\n");
    }
    printf("  valid=%s\n", markov_algorithm_valid(a) ? "yes" : "no");
}
