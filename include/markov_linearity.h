#pragma once

#include "markov_record.h"

/* Linearity classification of an algorithm's rule schemes.
 *
 * A rule pattern is "linear" iff every abstract letter appears at most once
 * in it (the multiplicative-linear-logic linearity condition restricted to
 * one rule scheme). An algorithm is LHS-linear iff every rule's pattern is
 * linear; RHS-linear iff every rule's replacement is linear; bilinear iff
 * both.
 *
 * The hypothesis under test (paper #1):
 *   rule-linearity is the master variable whose violation tracks
 *   binding-non-uniformity, GPU-unfriendliness, and Heyting-failure points.
 * This module provides the cheap classifier; the empirical loop is in
 * the enumerator. */

typedef enum {
    MARKOV_LIN_BILINEAR  = 0,  /* LHS-linear AND RHS-linear */
    MARKOV_LIN_LHS_ONLY  = 1,  /* LHS-linear AND NOT RHS-linear */
    MARKOV_LIN_RHS_ONLY  = 2,  /* NOT LHS-linear AND RHS-linear */
    MARKOV_LIN_NEITHER   = 3,  /* NOT LHS-linear AND NOT RHS-linear */
    MARKOV_LIN_COUNT     = 4
} markov_linearity_status;

/* Classify an algorithm's linearity. Letter-permutation invariant: two
 * algorithms in the same canonical class have the same status. */
markov_linearity_status markov_classify_linearity(const MarkovAlgorithm *a);

const char *markov_linearity_name(markov_linearity_status s);
