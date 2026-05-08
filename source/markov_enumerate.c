#include "markov_enumerate.h"

#include <string.h>

/* Power: alphabet_size ^ exp. */
static size_t pow_size(size_t base, size_t exp) {
    size_t r = 1;
    for (size_t i = 0; i < exp; i++) r *= base;
    return r;
}

size_t markov_rule_shape_count(uint8_t alphabet_size,
                               uint8_t max_pattern_len,
                               uint8_t max_repl_len) {
    /* Substitution rules: pattern_len ∈ [1, max_pattern_len] × repl_len ∈ [0, max_repl_len]
     * Terminal rules:    pattern_len ∈ [1, max_pattern_len], no replacement. */
    size_t total = 0;
    for (uint8_t pl = 1; pl <= max_pattern_len; pl++) {
        size_t patterns = pow_size(alphabet_size, pl);
        /* Substitution: replacement_len 0..max_repl_len */
        for (uint8_t rl = 0; rl <= max_repl_len; rl++) {
            size_t repls = pow_size(alphabet_size, rl);  /* rl=0 -> 1 (empty) */
            total += patterns * repls;
        }
        /* Terminal: pattern only */
        total += patterns;
    }
    return total;
}

/* Set rule[idx] to the rule_shape_index-th shape. Shape ordering:
 *   for pl in 1..max_pl:
 *     // substitution rules first, then terminal
 *     for rl in 0..max_rl:
 *       for pattern_bits in 0..alphabet^pl:
 *         for repl_bits in 0..alphabet^rl:
 *           shape (pl, rl, sub, pattern_bits, repl_bits)
 *     for pattern_bits in 0..alphabet^pl:
 *       shape (pl, 0, terminal, pattern_bits, 0)
 */
static bool fill_rule_from_shape(MarkovRule *out,
                                 size_t shape,
                                 uint8_t alphabet_size,
                                 uint8_t max_pattern_len,
                                 uint8_t max_repl_len,
                                 uint8_t bpl) {
    memset(out, 0, sizeof(*out));
    for (uint8_t pl = 1; pl <= max_pattern_len; pl++) {
        size_t patterns = pow_size(alphabet_size, pl);
        for (uint8_t rl = 0; rl <= max_repl_len; rl++) {
            size_t repls = pow_size(alphabet_size, rl);
            size_t block = patterns * repls;
            if (shape < block) {
                size_t pat_idx = shape / repls;
                size_t rep_idx = shape % repls;
                /* Pack pattern_bits: pat_idx in base alphabet_size, length pl. */
                uint8_t letters[8];
                size_t v = pat_idx;
                for (uint8_t i = 0; i < pl; i++) {
                    letters[i] = (uint8_t)(v % alphabet_size);
                    v /= alphabet_size;
                }
                if (!markov_pack_pattern(letters, pl, bpl, &out->pattern_bits))
                    return false;
                v = rep_idx;
                for (uint8_t i = 0; i < rl; i++) {
                    letters[i] = (uint8_t)(v % alphabet_size);
                    v /= alphabet_size;
                }
                if (!markov_pack_pattern(letters, rl, bpl, &out->replacement_bits))
                    return false;
                out->pattern_len = pl;
                out->replacement_len = rl;
                out->flags = 0;  /* substitution */
                return true;
            }
            shape -= block;
        }
        /* Terminal block at this pattern length. */
        if (shape < patterns) {
            size_t pat_idx = shape;
            uint8_t letters[8];
            size_t v = pat_idx;
            for (uint8_t i = 0; i < pl; i++) {
                letters[i] = (uint8_t)(v % alphabet_size);
                v /= alphabet_size;
            }
            if (!markov_pack_pattern(letters, pl, bpl, &out->pattern_bits))
                return false;
            out->pattern_len = pl;
            out->replacement_len = 0;
            out->replacement_bits = 0;
            out->flags = MARKOV_RULE_FLAG_TERMINAL;
            return true;
        }
        shape -= patterns;
    }
    return false;
}

size_t markov_total_count(uint8_t alphabet_size,
                          uint8_t rule_count,
                          uint8_t max_pattern_len,
                          uint8_t max_repl_len) {
    size_t shapes = markov_rule_shape_count(alphabet_size, max_pattern_len, max_repl_len);
    if (shapes == 0) return 0;
    size_t total = 1;
    for (uint8_t k = 0; k < rule_count; k++) total *= shapes;
    return total;
}

bool markov_enumerate_at(MarkovAlgorithm *out,
                         size_t index,
                         uint8_t alphabet_size,
                         uint8_t rule_count,
                         uint8_t max_pattern_len,
                         uint8_t max_repl_len,
                         uint16_t *rule_indices_out) {
    if (out == NULL) return false;
    if (rule_count > MARKOV_MAX_RULES) return false;
    if (alphabet_size == 0 || alphabet_size > MARKOV_MAX_ABSTRACT) return false;

    memset(out, 0, sizeof(*out));
    markov_header_init(&out->header, alphabet_size, rule_count);
    uint8_t bpl = out->header.bits_per_letter;
    if (bpl == 0) return false;

    size_t shapes = markov_rule_shape_count(alphabet_size, max_pattern_len, max_repl_len);
    if (shapes == 0) return false;

    for (uint8_t k = 0; k < rule_count; k++) {
        size_t shape_idx = index % shapes;
        index /= shapes;
        if (rule_indices_out != NULL) {
            rule_indices_out[k] = (uint16_t)shape_idx;
        }
        if (!fill_rule_from_shape(&out->rules[k], shape_idx, alphabet_size,
                                   max_pattern_len, max_repl_len, bpl)) {
            return false;
        }
    }
    return true;
}

size_t markov_enumerate(uint8_t alphabet_size,
                        uint8_t rule_count,
                        uint8_t max_pattern_len,
                        uint8_t max_repl_len,
                        markov_enum_callback cb,
                        void *userdata) {
    if (rule_count > MARKOV_MAX_RULES) return 0;
    if (alphabet_size == 0 || alphabet_size > MARKOV_MAX_ABSTRACT) return 0;

    size_t shapes = markov_rule_shape_count(alphabet_size, max_pattern_len, max_repl_len);
    if (shapes == 0) return 0;

    MarkovAlgorithm a;
    memset(&a, 0, sizeof(a));
    markov_header_init(&a.header, alphabet_size, rule_count);
    uint8_t bpl = a.header.bits_per_letter;
    if (bpl == 0) return 0;

    size_t total_count = 1;
    for (uint8_t k = 0; k < rule_count; k++) total_count *= shapes;

    /* Indices of the shape currently selected for each rule slot. */
    size_t shape_idx[MARKOV_MAX_RULES];
    for (uint8_t k = 0; k < rule_count; k++) shape_idx[k] = 0;

    /* Initialise all rule slots to shape 0. */
    for (uint8_t k = 0; k < rule_count; k++) {
        if (!fill_rule_from_shape(&a.rules[k], 0, alphabet_size,
                                   max_pattern_len, max_repl_len, bpl))
            return 0;
    }

    size_t visited = 0;
    while (1) {
        if (cb != NULL) cb(&a, userdata);
        visited++;

        /* Advance the lex-order index: increment slot 0 first, carry over. */
        uint8_t k = 0;
        while (k < rule_count) {
            shape_idx[k]++;
            if (shape_idx[k] < shapes) {
                fill_rule_from_shape(&a.rules[k], shape_idx[k], alphabet_size,
                                     max_pattern_len, max_repl_len, bpl);
                break;
            }
            /* Carry */
            shape_idx[k] = 0;
            fill_rule_from_shape(&a.rules[k], 0, alphabet_size,
                                 max_pattern_len, max_repl_len, bpl);
            k++;
        }
        if (k == rule_count) break;
    }

    return visited;
}
