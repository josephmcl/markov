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
