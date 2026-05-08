#include "markov_linearity.h"

static bool pattern_is_linear(uint32_t bits, uint8_t letters, uint8_t bpl) {
    /* Each abstract letter index ≤ MARKOV_MAX_ABSTRACT-1 = 7 fits in a u8 mask. */
    uint8_t seen = 0;
    for (uint8_t i = 0; i < letters; i++) {
        uint8_t letter = markov_unpack_letter(bits, i, bpl);
        uint8_t bit = (uint8_t)(1u << letter);
        if (seen & bit) return false;
        seen |= bit;
    }
    return true;
}

markov_linearity_status markov_classify_linearity(const MarkovAlgorithm *a) {
    if (a == NULL) return MARKOV_LIN_NEITHER;
    uint8_t bpl = a->header.bits_per_letter;
    bool lhs_lin = true, rhs_lin = true;
    for (uint8_t r = 0; r < a->header.rule_count; r++) {
        const MarkovRule *rule = &a->rules[r];
        if (!pattern_is_linear(rule->pattern_bits, rule->pattern_len, bpl))
            lhs_lin = false;
        if (rule->replacement_len > 0 &&
            !pattern_is_linear(rule->replacement_bits, rule->replacement_len, bpl))
            rhs_lin = false;
        if (!lhs_lin && !rhs_lin) break;  /* short-circuit */
    }
    if (lhs_lin && rhs_lin)  return MARKOV_LIN_BILINEAR;
    if (lhs_lin && !rhs_lin) return MARKOV_LIN_LHS_ONLY;
    if (!lhs_lin && rhs_lin) return MARKOV_LIN_RHS_ONLY;
    return MARKOV_LIN_NEITHER;
}

const char *markov_linearity_name(markov_linearity_status s) {
    switch (s) {
    case MARKOV_LIN_BILINEAR: return "bilinear";
    case MARKOV_LIN_LHS_ONLY: return "lhs-only";
    case MARKOV_LIN_RHS_ONLY: return "rhs-only";
    case MARKOV_LIN_NEITHER:  return "neither";
    default:                  return "?";
    }
}
