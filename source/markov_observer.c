#include "markov_observer.h"

#include <stdlib.h>
#include <string.h>

/* Maximum letter count expressible in a uint64 word for a given bits_per_letter. */
static inline uint8_t max_word_len_for_bpl(uint8_t bpl) {
    if (bpl == 0) return 0;
    return (uint8_t)(64u / bpl);
}

/* Find the leftmost position in word `bits` (length `len` letters) where
 * the packed pattern (length `pat_len` letters, pre-packed in `pat_bits`)
 * matches. Returns the letter-position, or -1 if no match. */
static int find_leftmost_match(uint64_t bits, uint8_t len,
                               uint32_t pat_bits, uint8_t pat_len,
                               uint8_t bpl) {
    if (pat_len == 0 || pat_len > len) return -1;
    uint8_t pat_total_bits = pat_len * bpl;
    uint64_t mask = (pat_total_bits >= 64) ? ~0ULL
                                            : ((1ULL << pat_total_bits) - 1ULL);
    uint64_t pat = (uint64_t)pat_bits & mask;
    int max_pos = (int)len - (int)pat_len;
    for (int pos = 0; pos <= max_pos; pos++) {
        uint8_t shift = (uint8_t)(pos * bpl);
        if (((bits >> shift) & mask) == pat) return pos;
    }
    return -1;
}

/* Replace `pat_len` letters at letter-position `pos` with `rep_len` letters
 * from `rep_bits`. Caller has verified the resulting length fits. */
static uint64_t substitute(uint64_t bits, uint8_t pos,
                           uint8_t pat_len,
                           uint32_t rep_bits, uint8_t rep_len,
                           uint8_t bpl) {
    uint8_t pos_bits = (uint8_t)(pos * bpl);
    uint8_t pat_total = (uint8_t)(pat_len * bpl);
    uint8_t rep_total = (uint8_t)(rep_len * bpl);
    uint64_t pre_mask = (pos_bits >= 64) ? ~0ULL
                                          : ((1ULL << pos_bits) - 1ULL);
    uint64_t pre = bits & pre_mask;
    uint64_t post = (pos_bits + pat_total >= 64) ? 0ULL
                                                 : (bits >> (pos_bits + pat_total));

    uint64_t rep_mask = (rep_total >= 64) ? ~0ULL : ((1ULL << rep_total) - 1ULL);
    uint64_t rep = (uint64_t)rep_bits & rep_mask;

    uint64_t out = pre;
    if (pos_bits < 64) out |= rep << pos_bits;
    uint8_t post_shift = (uint8_t)(pos_bits + rep_total);
    if (post_shift < 64) out |= post << post_shift;
    return out;
}

markov_trace_result markov_run_trace(const MarkovAlgorithm *a,
                                     uint64_t input_bits,
                                     uint8_t input_len) {
    markov_trace_result r;
    r.word_bits = input_bits;
    r.word_len = input_len;
    r.steps = 0;
    r.status = MARKOV_STATUS_NO_MATCH;

    if (a == NULL) return r;
    uint8_t bpl = a->header.bits_per_letter;
    if (bpl == 0) return r;
    uint8_t max_len = max_word_len_for_bpl(bpl);

    while (r.steps < MARKOV_TRACE_STEP_LIMIT) {
        bool fired = false;
        for (uint8_t k = 0; k < a->header.rule_count; k++) {
            const MarkovRule *rule = &a->rules[k];
            int pos = find_leftmost_match(r.word_bits, r.word_len,
                                          rule->pattern_bits, rule->pattern_len,
                                          bpl);
            if (pos < 0) continue;

            r.steps++;
            if (rule->flags & MARKOV_RULE_FLAG_TERMINAL) {
                r.status = MARKOV_STATUS_TERMINATED;
                return r;
            }
            uint16_t new_len = (uint16_t)r.word_len -
                               (uint16_t)rule->pattern_len +
                               (uint16_t)rule->replacement_len;
            if (new_len > max_len) {
                r.status = MARKOV_STATUS_TIMEOUT;
                return r;
            }
            r.word_bits = substitute(r.word_bits, (uint8_t)pos,
                                     rule->pattern_len,
                                     rule->replacement_bits, rule->replacement_len,
                                     bpl);
            r.word_len = (uint8_t)new_len;
            fired = true;
            break;
        }
        if (!fired) {
            r.status = MARKOV_STATUS_NO_MATCH;
            return r;
        }
    }
    r.status = MARKOV_STATUS_TIMEOUT;
    return r;
}

/* ---------------------------------------------------------------------- */
/* Fingerprint recipes                                                     */
/* ---------------------------------------------------------------------- */

static inline uint64_t fnv1a_step(uint64_t h, uint8_t b) {
    h ^= (uint64_t)b;
    h *= 0x100000001b3ULL;
    return h;
}
static inline uint64_t fnv1a_u64(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        h = fnv1a_step(h, (uint8_t)(v & 0xFF));
        v >>= 8;
    }
    return h;
}
static inline uint64_t fnv1a_u16(uint64_t h, uint16_t v) {
    h = fnv1a_step(h, (uint8_t)(v & 0xFF));
    h = fnv1a_step(h, (uint8_t)((v >> 8) & 0xFF));
    return h;
}

/* Pack (word_bits, word_len) into a single u128-equivalent (low: bits, high: len). */
typedef struct {
    uint64_t bits;
    uint8_t  len;
} packed_word;

static int compare_packed_word(const void *a, const void *b) {
    const packed_word *pa = (const packed_word *)a;
    const packed_word *pb = (const packed_word *)b;
    if (pa->bits != pb->bits) return pa->bits < pb->bits ? -1 : 1;
    if (pa->len  != pb->len)  return pa->len  < pb->len  ? -1 : 1;
    return 0;
}

static size_t total_inputs(uint8_t alphabet_size, uint8_t lo, uint8_t hi) {
    /* Sum_{L=lo..hi} alphabet_size^L; for binary that's 2^(hi+1) - 2^lo. */
    size_t total = 0;
    size_t count = 1;
    /* Start at L = 0; only accumulate when L >= lo. */
    for (uint8_t L = 0; L <= hi; L++) {
        if (L >= lo) total += count;
        count *= alphabet_size;
    }
    return total;
}

uint64_t markov_observe_fingerprint(const MarkovAlgorithm *a,
                                    uint8_t length_lo,
                                    uint8_t length_hi,
                                    markov_fingerprint_recipe recipe) {
    uint64_t h = 0xcbf29ce484222325ULL;
    if (a == NULL) return h;
    uint8_t alphabet = a->header.abstract_size;
    uint8_t bpl = a->header.bits_per_letter;
    if (alphabet == 0 || bpl == 0) return h;

    /* For Image and Fixed-point, accumulate words into a buffer for
     * sort-and-hash. For Basin and Step-count, hash directly in
     * canonical-enumeration order. */
    bool needs_collect = (recipe == MARKOV_FP_IMAGE ||
                         recipe == MARKOV_FP_FIXED_POINT);

    packed_word *buf = NULL;
    size_t buf_count = 0;
    size_t buf_cap = 0;
    if (needs_collect) {
        buf_cap = total_inputs(alphabet, length_lo, length_hi);
        if (buf_cap > 0) {
            buf = (packed_word *)malloc(buf_cap * sizeof(packed_word));
            if (buf == NULL) return h;
        }
    }

    for (uint8_t L = length_lo; L <= length_hi; L++) {
        size_t count = 1;
        for (uint8_t i = 0; i < L; i++) count *= alphabet;
        for (size_t idx = 0; idx < count; idx++) {
            /* Encode idx as L letters in base `alphabet`, packed LSB-first. */
            uint64_t word = 0;
            size_t v = idx;
            for (uint8_t i = 0; i < L; i++) {
                uint8_t letter = (uint8_t)(v % alphabet);
                v /= alphabet;
                word |= ((uint64_t)letter) << (i * bpl);
            }
            markov_trace_result r = markov_run_trace(a, word, L);

            switch (recipe) {
            case MARKOV_FP_BASIN:
                h = fnv1a_u64(h, r.word_bits);
                h = fnv1a_step(h, r.word_len);
                h = fnv1a_step(h, r.status);
                break;
            case MARKOV_FP_STEP_COUNT:
                h = fnv1a_u16(h, r.steps);
                h = fnv1a_step(h, r.status);
                break;
            case MARKOV_FP_IMAGE:
                buf[buf_count].bits = r.word_bits;
                buf[buf_count].len = r.word_len;
                buf_count++;
                break;
            case MARKOV_FP_FIXED_POINT:
                if (r.status == MARKOV_STATUS_TERMINATED &&
                    r.word_bits == word && r.word_len == L) {
                    buf[buf_count].bits = word;
                    buf[buf_count].len = L;
                    buf_count++;
                }
                break;
            case MARKOV_FP_TRACE_SHAPE:
                /* Trace-shape fingerprint requires per-step (rule, position)
                 * data which the current trace runner doesn't return. Stub
                 * by mixing in just the step count and status — refine later. */
                h = fnv1a_u16(h, r.steps);
                h = fnv1a_step(h, r.status);
                break;
            }
        }
    }

    if (needs_collect) {
        qsort(buf, buf_count, sizeof(packed_word), compare_packed_word);
        /* Image: dedupe before hashing. Fixed-point: already unique by
         * construction, but dedupe defensively. */
        size_t out = 0;
        for (size_t i = 0; i < buf_count; i++) {
            if (i == 0 || compare_packed_word(&buf[i], &buf[out - 1]) != 0) {
                buf[out++] = buf[i];
            }
        }
        for (size_t i = 0; i < out; i++) {
            h = fnv1a_u64(h, buf[i].bits);
            h = fnv1a_step(h, buf[i].len);
        }
        free(buf);
    }
    return h;
}

/* ---------------------------------------------------------------------- */
/* Continuous structural variables                                         */
/* ---------------------------------------------------------------------- */

/* Count match positions for a single rule against a single word. */
static size_t count_matches_at(uint64_t bits, uint8_t len,
                               uint32_t pat_bits, uint8_t pat_len,
                               uint8_t bpl) {
    if (pat_len == 0 || pat_len > len) return 0;
    uint8_t pat_total_bits = pat_len * bpl;
    uint64_t mask = (pat_total_bits >= 64) ? ~0ULL
                                            : ((1ULL << pat_total_bits) - 1ULL);
    uint64_t pat = (uint64_t)pat_bits & mask;
    int max_pos = (int)len - (int)pat_len;
    size_t count = 0;
    for (int pos = 0; pos <= max_pos; pos++) {
        uint8_t shift = (uint8_t)(pos * bpl);
        if (((bits >> shift) & mask) == pat) count++;
    }
    return count;
}

/* Returns true if rule r matches word at position pos. */
static bool matches_at(uint64_t bits, uint8_t len,
                       uint32_t pat_bits, uint8_t pat_len,
                       uint8_t bpl, uint8_t pos) {
    if (pat_len == 0 || pos + pat_len > len) return false;
    uint8_t pat_total_bits = pat_len * bpl;
    uint64_t mask = (pat_total_bits >= 64) ? ~0ULL
                                            : ((1ULL << pat_total_bits) - 1ULL);
    uint64_t pat = (uint64_t)pat_bits & mask;
    uint8_t shift = (uint8_t)(pos * bpl);
    return ((bits >> shift) & mask) == pat;
}

/* Iterate all words in [lo, hi] and apply a per-word callback. */
typedef void (*word_visitor)(uint64_t bits, uint8_t len, void *ud);

static void visit_words(uint8_t alphabet, uint8_t bpl,
                        uint8_t lo, uint8_t hi,
                        word_visitor cb, void *ud) {
    for (uint8_t L = lo; L <= hi; L++) {
        size_t count = 1;
        for (uint8_t i = 0; i < L; i++) count *= alphabet;
        for (size_t idx = 0; idx < count; idx++) {
            uint64_t word = 0;
            size_t v = idx;
            for (uint8_t i = 0; i < L; i++) {
                uint8_t letter = (uint8_t)(v % alphabet);
                v /= alphabet;
                word |= ((uint64_t)letter) << (i * bpl);
            }
            cb(word, L, ud);
        }
    }
}

typedef struct {
    const MarkovAlgorithm *a;
    uint8_t bpl;
    size_t  total;
} match_count_ctx;

static void match_count_cb(uint64_t bits, uint8_t len, void *ud) {
    match_count_ctx *c = (match_count_ctx *)ud;
    for (uint8_t r = 0; r < c->a->header.rule_count; r++) {
        const MarkovRule *rule = &c->a->rules[r];
        c->total += count_matches_at(bits, len,
            rule->pattern_bits, rule->pattern_len, c->bpl);
    }
}

size_t markov_total_match_count(const MarkovAlgorithm *a,
                                uint8_t length_lo,
                                uint8_t length_hi) {
    if (a == NULL) return 0;
    match_count_ctx c = { .a = a, .bpl = a->header.bits_per_letter, .total = 0 };
    visit_words(a->header.abstract_size, c.bpl, length_lo, length_hi,
        match_count_cb, &c);
    return c.total;
}

typedef struct {
    const MarkovAlgorithm *a;
    uint8_t bpl;
    size_t  total;
} overlap_ctx;

static void overlap_cb(uint64_t bits, uint8_t len, void *ud) {
    overlap_ctx *c = (overlap_ctx *)ud;
    uint8_t K = c->a->header.rule_count;
    /* For each position, count rules that match there; sum C(matches, 2)
     * across positions. */
    if (len == 0) return;
    for (uint8_t pos = 0; pos < len; pos++) {
        size_t count = 0;
        for (uint8_t r = 0; r < K; r++) {
            const MarkovRule *rule = &c->a->rules[r];
            if (matches_at(bits, len, rule->pattern_bits, rule->pattern_len,
                           c->bpl, pos)) {
                count++;
            }
        }
        if (count >= 2) {
            /* Number of unordered rule pairs that overlap at this position. */
            c->total += (count * (count - 1)) / 2;
        }
    }
}

size_t markov_total_overlap_count(const MarkovAlgorithm *a,
                                  uint8_t length_lo,
                                  uint8_t length_hi) {
    if (a == NULL) return 0;
    overlap_ctx c = { .a = a, .bpl = a->header.bits_per_letter, .total = 0 };
    visit_words(a->header.abstract_size, c.bpl, length_lo, length_hi,
        overlap_cb, &c);
    return c.total;
}
