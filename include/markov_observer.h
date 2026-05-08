#pragma once

#include "markov_record.h"

/* Bit-parallel CPU observer. Operates on uint64-packed words: each letter
 * occupies bits_per_letter bits in the word, position 0 at the LSB.
 *
 * For binary alphabet (bits_per_letter=1) a word fits up to 64 letters.
 * For 3-4-letter alphabets (bpl=2), up to 32. For 5-8 (bpl=3), up to 21.
 *
 * The observer performs deterministic Markov execution:
 *   for each rule in priority order:
 *     find leftmost position where the pattern matches
 *     if found and the rule is terminal: stop with status=terminated
 *     if found: substitute, increment step count, restart from rule 0
 *   if no rule matched: stop with status=no_match
 *   if step count exceeds limit or word overflows 64 bits: status=timeout
 */

#define MARKOV_TRACE_STEP_LIMIT  10000

typedef enum {
    MARKOV_STATUS_TERMINATED = 0,
    MARKOV_STATUS_NO_MATCH   = 1,
    MARKOV_STATUS_TIMEOUT    = 2,
} markov_trace_status;

typedef struct {
    uint64_t word_bits;     /* output word (post-execution) */
    uint16_t steps;         /* number of rule firings */
    uint8_t  word_len;      /* output letter count */
    uint8_t  status;        /* markov_trace_status */
} markov_trace_result;

/* Run the algorithm on a single input word, returning the trace summary.
 * The input word is packed as letters at bits [i * bpl, i * bpl + bpl).
 * Letter 0 is at the LSB; this matches the encoder's pattern packing. */
markov_trace_result markov_run_trace(const MarkovAlgorithm *a,
                                     uint64_t input_bits,
                                     uint8_t input_len);

/* Compute a behavioral fingerprint for the algorithm by running every
 * input word in the length range [length_lo, length_hi] and reducing the
 * trace results according to the chosen recipe. The recipe is one of the
 * MARKOV_FP_* values from markov_record.h.
 *
 * Image fingerprint:    hash of the sorted set of (output_bits, output_len).
 * Basin fingerprint:    hash of (output_bits, output_len, status) for each
 *                       input in canonical enumeration order.
 * Fixed-point:          hash of the sorted set of inputs whose output ==
 *                       input AND status==terminated.
 * Step-count:           hash of the per-input step counts, in canonical
 *                       enumeration order. */
uint64_t markov_observe_fingerprint(const MarkovAlgorithm *a,
                                    uint8_t length_lo,
                                    uint8_t length_hi,
                                    markov_fingerprint_recipe recipe);

/* ---------------------------------------------------------------------- */
/* Continuous structural variables                                         */
/*                                                                         */
/* These measure rule-level activity over an input range without running   */
/* traces. They are pure structural properties — alphabet-aware, free of   */
/* observation-recipe choice — and exist to test the hypothesis that       */
/* behavioral collapse correlates with rule-pattern density / interaction. */
/* ---------------------------------------------------------------------- */

/* Total match count: for each rule r in a, for each input word w of every
 * length L in [length_lo, length_hi], sum the number of positions where
 * r's pattern matches w. Captures how often rules find substrate to fire
 * on. Returns the raw count. */
size_t markov_total_match_count(const MarkovAlgorithm *a,
                                uint8_t length_lo,
                                uint8_t length_hi);

/* Total positional overlap: for each pair (r_i, r_j) with i < j and each
 * input word, sum the number of positions where both r_i and r_j match.
 * Captures priority-relevant interaction — positions where the priority
 * order makes a semantic difference. Returns the raw count. */
size_t markov_total_overlap_count(const MarkovAlgorithm *a,
                                  uint8_t length_lo,
                                  uint8_t length_hi);
