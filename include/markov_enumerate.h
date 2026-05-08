#pragma once

#include "markov_record.h"

/* Enumerator for paper #1: walks every algorithm of a given shape and
 * passes each one to a user callback.
 *
 * Shape parameters control the search space:
 *   - alphabet_size: abstract alphabet size (1..MARKOV_MAX_ABSTRACT)
 *   - rule_count K:  number of rules
 *   - max_pattern_len: 1..ceil(32/bits_per_letter)
 *   - max_repl_len:    0..max_pattern_len
 *
 * The enumerator does not canonicalise — that's the callback's job — but
 * the algorithm passed to the callback is structurally valid by
 * construction. */

typedef void (*markov_enum_callback)(const MarkovAlgorithm *a, void *userdata);

/* Iterate every algorithm of the given shape. Returns the total count of
 * algorithms visited (i.e. the size of the search space, not the number of
 * distinct equivalence classes). */
size_t markov_enumerate(uint8_t alphabet_size,
                        uint8_t rule_count,
                        uint8_t max_pattern_len,
                        uint8_t max_repl_len,
                        markov_enum_callback cb,
                        void *userdata);

/* Number of distinct rule shapes for a given alphabet size and length
 * bounds. Useful for predicting search-space size without iterating. */
size_t markov_rule_shape_count(uint8_t alphabet_size,
                               uint8_t max_pattern_len,
                               uint8_t max_repl_len);

/* Total search-space size: shapes^rule_count. */
size_t markov_total_count(uint8_t alphabet_size,
                          uint8_t rule_count,
                          uint8_t max_pattern_len,
                          uint8_t max_repl_len);

/* Indexed enumeration: directly construct the algorithm at lex-order
 * position `index` (0 ≤ index < markov_total_count(...)). Each rule slot
 * k is set to shape (index / shapes^k) % shapes. If `rule_indices_out`
 * is non-NULL, the per-rule shape indices are written there.
 *
 * This is the parallel-friendly form: any thread can compute the algorithm
 * at any index without coordinating with other threads. */
bool markov_enumerate_at(MarkovAlgorithm *out,
                         size_t index,
                         uint8_t alphabet_size,
                         uint8_t rule_count,
                         uint8_t max_pattern_len,
                         uint8_t max_repl_len,
                         uint16_t *rule_indices_out);
