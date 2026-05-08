#include "markov_record.h"

#include <stdio.h>
#include <string.h>

/* This file is intentionally pure: no dependencies on the language
 * compiler's lexer/parser/context. The encoder bridge that converts an
 * in-memory algorithm_definition into a MarkovAlgorithm record lives in
 * source/markov_encode.c so that downstream tools (the offline enumerator,
 * future GPU harnesses) can link against the record helpers without
 * pulling in the language-front-end. */

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

/* ---------------------------------------------------------------------- */
/* Printing                                                                */
/* ---------------------------------------------------------------------- */

/* ---------------------------------------------------------------------- */
/* Canonicalisation                                                        */
/* ---------------------------------------------------------------------- */

/* Re-pack a single packed pattern under a letter permutation. Each letter
 * value k is replaced by perm[k] before re-packing. */
static uint32_t apply_perm_to_packed(uint32_t bits, uint8_t len,
                                     uint8_t bpl, const uint8_t *perm) {
    uint32_t out = 0;
    uint32_t mask = (bpl >= 32) ? ~0u : ((1u << bpl) - 1u);
    for (uint8_t i = 0; i < len; i++) {
        uint8_t k = (uint8_t)((bits >> (i * bpl)) & mask);
        uint8_t pk = perm[k];
        out |= ((uint32_t)pk & mask) << (i * bpl);
    }
    return out;
}

void markov_apply_permutation(const MarkovAlgorithm *src,
                              MarkovAlgorithm *dst,
                              const uint8_t *perm) {
    if (src == NULL || dst == NULL || perm == NULL) return;
    if (dst != src) *dst = *src;
    dst->header.canonical_id = 0;

    uint8_t bpl = src->header.bits_per_letter;
    for (uint8_t r = 0; r < src->header.rule_count; r++) {
        const MarkovRule *in = &src->rules[r];
        MarkovRule *out = &dst->rules[r];
        *out = *in;
        out->pattern_bits =
            apply_perm_to_packed(in->pattern_bits, in->pattern_len, bpl, perm);
        out->replacement_bits =
            apply_perm_to_packed(in->replacement_bits, in->replacement_len, bpl, perm);
    }
}

size_t markov_serialize_rules(const MarkovAlgorithm *a, uint8_t *out, size_t cap) {
    if (a == NULL || out == NULL) return 0;
    size_t n = 0;
    /* Per rule: flags (1) + pattern_len (1) + replacement_len (1) +
     * pattern_bits (4 LE) + replacement_bits (4 LE) = 11 bytes. */
    for (uint8_t r = 0; r < a->header.rule_count; r++) {
        const MarkovRule *rule = &a->rules[r];
        if (n + 11 > cap) return n;
        out[n++] = rule->flags;
        out[n++] = rule->pattern_len;
        out[n++] = rule->replacement_len;
        out[n++] = (uint8_t)(rule->pattern_bits & 0xFF);
        out[n++] = (uint8_t)((rule->pattern_bits >> 8) & 0xFF);
        out[n++] = (uint8_t)((rule->pattern_bits >> 16) & 0xFF);
        out[n++] = (uint8_t)((rule->pattern_bits >> 24) & 0xFF);
        out[n++] = (uint8_t)(rule->replacement_bits & 0xFF);
        out[n++] = (uint8_t)((rule->replacement_bits >> 8) & 0xFF);
        out[n++] = (uint8_t)((rule->replacement_bits >> 16) & 0xFF);
        out[n++] = (uint8_t)((rule->replacement_bits >> 24) & 0xFF);
    }
    return n;
}

uint64_t markov_hash_rules(const MarkovAlgorithm *a) {
    uint8_t buf[MARKOV_MAX_RULES * 16];
    size_t n = markov_serialize_rules(a, buf, sizeof(buf));
    /* FNV-1a 64-bit */
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= (uint64_t)buf[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/* Generate the next permutation of perm[] in lexicographic order, in place.
 * Returns true if a next permutation exists, false if perm is the last
 * (descending) permutation. Standard "next permutation" algorithm. */
static bool next_permutation(uint8_t *perm, uint8_t n) {
    if (n < 2) return false;
    int i = (int)n - 2;
    while (i >= 0 && perm[i] >= perm[i + 1]) i--;
    if (i < 0) return false;
    int j = (int)n - 1;
    while (perm[j] <= perm[i]) j--;
    uint8_t tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp;
    /* Reverse perm[i+1 .. n-1] */
    int lo = i + 1, hi = (int)n - 1;
    while (lo < hi) {
        tmp = perm[lo]; perm[lo] = perm[hi]; perm[hi] = tmp;
        lo++; hi--;
    }
    return true;
}

static int byte_compare(const uint8_t *a, size_t na, const uint8_t *b, size_t nb) {
    size_t m = na < nb ? na : nb;
    for (size_t i = 0; i < m; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    if (na < nb) return -1;
    if (na > nb) return 1;
    return 0;
}

void markov_canonicalize_letters(MarkovAlgorithm *a) {
    if (a == NULL) return;
    if (!markov_algorithm_valid(a)) return;
    uint8_t n = a->header.abstract_size;
    if (n == 0) return;

    uint8_t perm[MARKOV_MAX_ABSTRACT];
    uint8_t best_perm[MARKOV_MAX_ABSTRACT];
    for (uint8_t i = 0; i < n; i++) perm[i] = i;
    for (uint8_t i = 0; i < n; i++) best_perm[i] = i;

    MarkovAlgorithm cand;
    uint8_t best_buf[MARKOV_MAX_RULES * 16];
    uint8_t cand_buf[MARKOV_MAX_RULES * 16];
    size_t best_len = markov_serialize_rules(a, best_buf, sizeof(best_buf));

    /* Walk all n! permutations starting from identity (already accounted
     * for as the initial best). next_permutation generates the rest. */
    while (next_permutation(perm, n)) {
        markov_apply_permutation(a, &cand, perm);
        size_t cand_len = markov_serialize_rules(&cand, cand_buf, sizeof(cand_buf));
        if (byte_compare(cand_buf, cand_len, best_buf, best_len) < 0) {
            memcpy(best_buf, cand_buf, cand_len);
            best_len = cand_len;
            memcpy(best_perm, perm, n);
        }
    }

    /* Apply the winning permutation in place and stamp canonical_id. */
    markov_apply_permutation(a, a, best_perm);
    a->header.canonical_id = markov_hash_rules(a);
}

void markov_print_algorithm(const MarkovAlgorithm *a) {
    if (a == NULL) {
        printf("MarkovAlgorithm: <null>\n");
        return;
    }
    printf("MarkovAlgorithm v%u.%u abstract_size=%u rule_count=%u bits_per_letter=%u flags=0x%04x canonical_id=0x%016llx\n",
        a->header.version_major, a->header.version_minor,
        a->header.abstract_size, a->header.rule_count,
        a->header.bits_per_letter, a->header.flags,
        (unsigned long long)a->header.canonical_id);

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
