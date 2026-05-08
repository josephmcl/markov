/* Standalone enumerator for paper #1.
 *
 * Walks every algorithm of a given shape, canonicalises each (letter-perm
 * canonicalisation), and reports:
 *   - structural class count: distinct canonical_id values
 *   - behavioral class count: distinct fingerprint values under a chosen
 *     observation recipe (image / basin / fixed-point / step-count) over
 *     a length range
 *
 * Usage:
 *   enumerate [alphabet] [rules] [max_pat] [max_repl] [obs_lo] [obs_hi] [recipe]
 *
 * recipe is one of: image, basin, fixed, steps. Default basin.
 * If obs_hi < obs_lo, no observation is run and only structural counts
 * are reported.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "markov_record.h"
#include "markov_enumerate.h"
#include "markov_observer.h"
#include "markov_linearity.h"

typedef struct {
    uint64_t *struct_ids;
    uint64_t *behav_ids;
    uint8_t  *linearity;        /* per-algorithm markov_linearity_status */
    uint32_t *match_count;      /* per-algorithm total match count */
    uint32_t *overlap_count;    /* per-algorithm total overlap count */
    size_t    count;
    size_t    capacity;
    size_t    invalid;
    /* Observation parameters; if obs_hi < obs_lo, observation is skipped. */
    uint8_t   obs_lo;
    uint8_t   obs_hi;
    markov_fingerprint_recipe recipe;
    bool      observe;
} ClassBuffer;

static void on_algorithm(const MarkovAlgorithm *a, void *ud) {
    ClassBuffer *buf = (ClassBuffer *)ud;
    MarkovAlgorithm copy = *a;
    if (!markov_algorithm_valid(&copy)) {
        buf->invalid++;
        return;
    }
    markov_canonicalize_letters(&copy);
    if (buf->count == buf->capacity) {
        buf->capacity = buf->capacity ? buf->capacity * 2 : 1024;
        buf->struct_ids = (uint64_t *)realloc(buf->struct_ids,
            buf->capacity * sizeof(uint64_t));
        if (buf->struct_ids == NULL) { fprintf(stderr, "OOM\n"); exit(1); }
        buf->linearity = (uint8_t *)realloc(buf->linearity,
            buf->capacity * sizeof(uint8_t));
        if (buf->linearity == NULL) { fprintf(stderr, "OOM\n"); exit(1); }
        buf->match_count = (uint32_t *)realloc(buf->match_count,
            buf->capacity * sizeof(uint32_t));
        if (buf->match_count == NULL) { fprintf(stderr, "OOM\n"); exit(1); }
        buf->overlap_count = (uint32_t *)realloc(buf->overlap_count,
            buf->capacity * sizeof(uint32_t));
        if (buf->overlap_count == NULL) { fprintf(stderr, "OOM\n"); exit(1); }
        if (buf->observe) {
            buf->behav_ids = (uint64_t *)realloc(buf->behav_ids,
                buf->capacity * sizeof(uint64_t));
            if (buf->behav_ids == NULL) { fprintf(stderr, "OOM\n"); exit(1); }
        }
    }
    buf->struct_ids[buf->count] = copy.header.canonical_id;
    buf->linearity[buf->count] = (uint8_t)markov_classify_linearity(&copy);
    buf->match_count[buf->count] = (uint32_t)markov_total_match_count(
        &copy, buf->obs_lo, buf->obs_hi);
    buf->overlap_count[buf->count] = (uint32_t)markov_total_overlap_count(
        &copy, buf->obs_lo, buf->obs_hi);
    if (buf->observe) {
        buf->behav_ids[buf->count] = markov_observe_fingerprint(
            &copy, buf->obs_lo, buf->obs_hi, buf->recipe);
    }
    buf->count++;
}

static int compare_uint64(const void *a, const void *b) {
    uint64_t aa = *(const uint64_t *)a;
    uint64_t bb = *(const uint64_t *)b;
    return (aa > bb) - (aa < bb);
}

static size_t count_unique_sorted(const uint64_t *arr, size_t n) {
    if (n == 0) return 0;
    size_t u = 1;
    for (size_t i = 1; i < n; i++) {
        if (arr[i] != arr[i - 1]) u++;
    }
    return u;
}

static markov_fingerprint_recipe parse_recipe(const char *s) {
    if (s == NULL) return MARKOV_FP_BASIN;
    if (strcmp(s, "image") == 0)  return MARKOV_FP_IMAGE;
    if (strcmp(s, "basin") == 0)  return MARKOV_FP_BASIN;
    if (strcmp(s, "fixed") == 0)  return MARKOV_FP_FIXED_POINT;
    if (strcmp(s, "steps") == 0)  return MARKOV_FP_STEP_COUNT;
    fprintf(stderr, "Unknown recipe '%s' (use image|basin|fixed|steps), defaulting to basin\n", s);
    return MARKOV_FP_BASIN;
}

static const char *recipe_name(markov_fingerprint_recipe r) {
    switch (r) {
    case MARKOV_FP_IMAGE:       return "image";
    case MARKOV_FP_BASIN:       return "basin";
    case MARKOV_FP_FIXED_POINT: return "fixed-point";
    case MARKOV_FP_STEP_COUNT:  return "step-count";
    case MARKOV_FP_TRACE_SHAPE: return "trace-shape";
    default:                    return "?";
    }
}

int main(int argc, char **argv) {
    uint8_t alphabet_size   = (uint8_t)((argc > 1) ? atoi(argv[1]) : 2);
    uint8_t rule_count      = (uint8_t)((argc > 2) ? atoi(argv[2]) : 2);
    uint8_t max_pattern_len = (uint8_t)((argc > 3) ? atoi(argv[3]) : 2);
    uint8_t max_repl_len    = (uint8_t)((argc > 4) ? atoi(argv[4]) : 2);
    int     obs_lo_i        = (argc > 5) ? atoi(argv[5]) : 0;
    int     obs_hi_i        = (argc > 6) ? atoi(argv[6]) : 6;
    markov_fingerprint_recipe recipe = parse_recipe((argc > 7) ? argv[7] : NULL);

    size_t shapes = markov_rule_shape_count(alphabet_size, max_pattern_len, max_repl_len);
    size_t total = 1;
    for (uint8_t k = 0; k < rule_count; k++) total *= shapes;

    bool observe = (obs_hi_i >= obs_lo_i && obs_hi_i >= 0);

    fprintf(stderr,
        "Enumerating: alphabet_size=%u rule_count=%u max_pat=%u max_repl=%u\n"
        "Per-rule shapes: %zu\n"
        "Total algorithms: %zu\n",
        alphabet_size, rule_count, max_pattern_len, max_repl_len,
        shapes, total);
    if (observe) {
        fprintf(stderr,
            "Observation:  range=%d..%d  recipe=%s\n",
            obs_lo_i, obs_hi_i, recipe_name(recipe));
    } else {
        fprintf(stderr, "Observation:  (skipped)\n");
    }

    ClassBuffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.observe = observe;
    buf.obs_lo = (uint8_t)(obs_lo_i < 0 ? 0 : obs_lo_i);
    buf.obs_hi = (uint8_t)(obs_hi_i < 0 ? 0 : obs_hi_i);
    buf.recipe = recipe;

    clock_t t0 = clock();
    size_t visited = markov_enumerate(alphabet_size, rule_count,
                                       max_pattern_len, max_repl_len,
                                       on_algorithm, &buf);
    clock_t t1 = clock();
    double enum_secs = (double)(t1 - t0) / CLOCKS_PER_SEC;

    /* Compute overall structural and behavioral class counts using
     * scratch copies (qsort destroys the array's per-entry alignment with
     * the linearity array, which we still need for per-bucket analysis). */
    uint64_t *scratch_a = (uint64_t *)malloc(buf.count * sizeof(uint64_t));
    uint64_t *scratch_b = (uint64_t *)malloc(buf.count * sizeof(uint64_t));
    if (scratch_a == NULL || scratch_b == NULL) { fprintf(stderr, "OOM\n"); exit(1); }

    memcpy(scratch_a, buf.struct_ids, buf.count * sizeof(uint64_t));
    qsort(scratch_a, buf.count, sizeof(uint64_t), compare_uint64);
    size_t struct_unique = count_unique_sorted(scratch_a, buf.count);

    size_t behav_unique = 0;
    if (observe) {
        memcpy(scratch_b, buf.behav_ids, buf.count * sizeof(uint64_t));
        qsort(scratch_b, buf.count, sizeof(uint64_t), compare_uint64);
        behav_unique = count_unique_sorted(scratch_b, buf.count);
    }
    clock_t t3 = clock();
    double sort_secs = (double)(t3 - t1) / CLOCKS_PER_SEC;

    fprintf(stderr,
        "Visited:   %zu\n"
        "Invalid:   %zu (skipped)\n"
        "Structural classes (canonical_id):  %zu\n",
        visited, buf.invalid, struct_unique);
    if (observe) {
        fprintf(stderr,
            "Behavioral classes (%s):  %zu\n",
            recipe_name(recipe), behav_unique);
    }
    fprintf(stderr,
        "Enum+canonicalise+observe: %.3fs\n"
        "Sort+uniq:                 %.3fs\n",
        enum_secs, sort_secs);

    if (struct_unique > 0) {
        fprintf(stderr, "Letter-perm reduction:    %.2fx\n",
            (double)visited / (double)struct_unique);
    }
    if (observe && behav_unique > 0) {
        fprintf(stderr, "Structural / behavioral:  %.2fx\n",
            (double)struct_unique / (double)behav_unique);
    }

    /* ---- Linearity-conditional analysis ---- */
    fprintf(stderr,
        "\nLinearity buckets (rule-scheme classification):\n"
        "  bucket    count   struct%s%s\n",
        observe ? "  behav " : "",
        observe ? "  s/b" : "");
    for (uint8_t lin = 0; lin < MARKOV_LIN_COUNT; lin++) {
        size_t n = 0;
        for (size_t i = 0; i < buf.count; i++) {
            if (buf.linearity[i] == lin) n++;
        }
        if (n == 0) {
            fprintf(stderr, "  %-9s  %6zu      0%s%s\n",
                markov_linearity_name((markov_linearity_status)lin), n,
                observe ? "      0" : "",
                observe ? "    -" : "");
            continue;
        }
        /* Collect this bucket's struct and behav ids, then count distinct. */
        uint64_t *bs = (uint64_t *)malloc(n * sizeof(uint64_t));
        uint64_t *bb = observe ? (uint64_t *)malloc(n * sizeof(uint64_t)) : NULL;
        size_t k = 0;
        for (size_t i = 0; i < buf.count; i++) {
            if (buf.linearity[i] == lin) {
                bs[k] = buf.struct_ids[i];
                if (observe) bb[k] = buf.behav_ids[i];
                k++;
            }
        }
        qsort(bs, k, sizeof(uint64_t), compare_uint64);
        size_t s_unique = count_unique_sorted(bs, k);
        size_t b_unique = 0;
        if (observe) {
            qsort(bb, k, sizeof(uint64_t), compare_uint64);
            b_unique = count_unique_sorted(bb, k);
        }
        if (observe) {
            fprintf(stderr, "  %-9s  %6zu  %6zu  %6zu  %5.2f\n",
                markov_linearity_name((markov_linearity_status)lin),
                n, s_unique, b_unique,
                b_unique > 0 ? (double)s_unique / (double)b_unique : 0.0);
        } else {
            fprintf(stderr, "  %-9s  %6zu  %6zu\n",
                markov_linearity_name((markov_linearity_status)lin),
                n, s_unique);
        }
        free(bs);
        free(bb);
    }

    /* ---- Continuous-variable binning analysis (overlap & match count) ---- */
    if (observe) {
        const char *names[2] = { "overlap", "match" };
        uint32_t *vars[2] = { buf.overlap_count, buf.match_count };
        for (int v = 0; v < 2; v++) {
            uint32_t *vals = vars[v];
            /* Find max for binning. */
            uint32_t maxv = 0;
            for (size_t i = 0; i < buf.count; i++) {
                if (vals[i] > maxv) maxv = vals[i];
            }
            const int B = 10;
            uint32_t bin_size = (maxv / (uint32_t)B) + 1;
            fprintf(stderr,
                "\n%s binning (max=%u, %d equal-width bins):\n"
                "  bin_lo  bin_hi   count   struct  behav  s/b\n",
                names[v], maxv, B);
            for (int b = 0; b < B; b++) {
                uint32_t bin_lo = (uint32_t)b * bin_size;
                uint32_t bin_hi = bin_lo + bin_size - 1;
                if (bin_hi > maxv) bin_hi = maxv;
                /* Collect this bin. */
                size_t n = 0;
                for (size_t i = 0; i < buf.count; i++) {
                    if (vals[i] >= bin_lo && vals[i] <= bin_hi) n++;
                }
                if (n == 0) continue;
                uint64_t *bs = (uint64_t *)malloc(n * sizeof(uint64_t));
                uint64_t *bb = (uint64_t *)malloc(n * sizeof(uint64_t));
                size_t k = 0;
                for (size_t i = 0; i < buf.count; i++) {
                    if (vals[i] >= bin_lo && vals[i] <= bin_hi) {
                        bs[k] = buf.struct_ids[i];
                        bb[k] = buf.behav_ids[i];
                        k++;
                    }
                }
                qsort(bs, k, sizeof(uint64_t), compare_uint64);
                qsort(bb, k, sizeof(uint64_t), compare_uint64);
                size_t s_unique = count_unique_sorted(bs, k);
                size_t b_unique = count_unique_sorted(bb, k);
                fprintf(stderr,
                    "  %6u  %6u  %6zu  %6zu  %5zu  %5.3f\n",
                    bin_lo, bin_hi, n, s_unique, b_unique,
                    s_unique > 0 ? (double)b_unique / (double)s_unique : 0.0);
                free(bs);
                free(bb);
            }
        }
    }

    free(scratch_a);
    free(scratch_b);
    free(buf.struct_ids);
    free(buf.behav_ids);
    free(buf.linearity);
    free(buf.match_count);
    free(buf.overlap_count);
    return 0;
}
