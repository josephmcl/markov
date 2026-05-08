/* Standalone enumerator for paper #1.
 *
 * Walks every algorithm of a given shape, canonicalizes each, and reports:
 *   - structural class count (distinct canonical_id values)
 *   - behavioral class count (distinct fingerprint values under a chosen recipe)
 *   - linearity-bucket and overlap/match binning analyses
 *
 * Optionally writes per-canonical-algorithm records to disk for follow-up
 * analysis (--records <prefix> writes <prefix>.struct and <prefix>.<recipe>.behav).
 *
 * When built with -DENUM_USE_OPENMP and -fopenmp, the per-algorithm work is
 * parallelized across cores; thread-local buffers are merged at the end.
 *
 * Usage:
 *   enumerate [alphabet] [rules] [max_pat] [max_repl] [obs_lo] [obs_hi] [recipe]
 *             [--records <prefix>]
 *
 * recipe is one of: image, basin, fixed, steps. Default basin.
 * If obs_hi < obs_lo, no observation is run and only structural counts are reported.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "markov_record.h"
#include "markov_enumerate.h"
#include "markov_observer.h"
#include "markov_linearity.h"
#include "markov_records.h"

#if defined(ENUM_USE_OPENMP)
#  include <omp.h>
#  define ENUM_OMP 1
#else
#  define ENUM_OMP 0
#endif

/* ---------------------------------------------------------------- */
/* Per-algorithm record (in-memory, before sort/dedup)               */
/* ---------------------------------------------------------------- */

typedef struct {
    uint64_t canonical_id;
    uint64_t fingerprint;        /* 0 if observation skipped */
    uint64_t enum_index;         /* raw enumeration index — used as deterministic
                                  * tiebreaker for dedupe so parallel runs produce
                                  * byte-identical output */
    uint32_t overlap_count;
    uint32_t match_count;
    uint8_t  linearity;
    uint16_t rule_indices[MARKOV_MAX_RULES];
} AlgEntry;

typedef struct {
    AlgEntry *entries;
    size_t    count;
    size_t    capacity;
    size_t    invalid;
    /* Aggregate counters (avoid post-pass scans for these). */
    size_t    lin_count[MARKOV_LIN_COUNT];
} ThreadBuf;

static void threadbuf_init(ThreadBuf *b) {
    memset(b, 0, sizeof(*b));
}

static void threadbuf_free(ThreadBuf *b) {
    free(b->entries);
    memset(b, 0, sizeof(*b));
}

static void threadbuf_push(ThreadBuf *b, const AlgEntry *e) {
    if (b->count == b->capacity) {
        b->capacity = b->capacity ? b->capacity * 2 : 1024;
        b->entries = (AlgEntry *)realloc(b->entries, b->capacity * sizeof(AlgEntry));
        if (b->entries == NULL) {
            fprintf(stderr, "OOM growing thread buffer\n");
            exit(1);
        }
    }
    b->entries[b->count++] = *e;
}

/* ---------------------------------------------------------------- */
/* Comparison + sort helpers                                         */
/* ---------------------------------------------------------------- */

static int compare_uint64(const void *a, const void *b) {
    uint64_t aa = *(const uint64_t *)a;
    uint64_t bb = *(const uint64_t *)b;
    return (aa > bb) - (aa < bb);
}

static int compare_alg_by_canonical(const void *a, const void *b) {
    const AlgEntry *pa = (const AlgEntry *)a;
    const AlgEntry *pb = (const AlgEntry *)b;
    if (pa->canonical_id != pb->canonical_id)
        return pa->canonical_id < pb->canonical_id ? -1 : 1;
    /* Tiebreak by enumeration index so dedupe is deterministic across
     * parallel and serial runs. */
    if (pa->enum_index != pb->enum_index)
        return pa->enum_index < pb->enum_index ? -1 : 1;
    return 0;
}

static size_t count_unique_sorted_u64(const uint64_t *arr, size_t n) {
    if (n == 0) return 0;
    size_t u = 1;
    for (size_t i = 1; i < n; i++) {
        if (arr[i] != arr[i - 1]) u++;
    }
    return u;
}

/* ---------------------------------------------------------------- */
/* Recipe parsing                                                    */
/* ---------------------------------------------------------------- */

static markov_fingerprint_recipe parse_recipe(const char *s) {
    if (s == NULL) return MARKOV_FP_BASIN;
    if (strcmp(s, "image") == 0) return MARKOV_FP_IMAGE;
    if (strcmp(s, "basin") == 0) return MARKOV_FP_BASIN;
    if (strcmp(s, "fixed") == 0) return MARKOV_FP_FIXED_POINT;
    if (strcmp(s, "steps") == 0) return MARKOV_FP_STEP_COUNT;
    fprintf(stderr, "Unknown recipe '%s'; defaulting to basin\n", s);
    return MARKOV_FP_BASIN;
}

static const char *recipe_short_name(markov_fingerprint_recipe r) {
    switch (r) {
    case MARKOV_FP_IMAGE:       return "image";
    case MARKOV_FP_BASIN:       return "basin";
    case MARKOV_FP_FIXED_POINT: return "fixed";
    case MARKOV_FP_STEP_COUNT:  return "steps";
    case MARKOV_FP_TRACE_SHAPE: return "trace";
    default:                    return "?";
    }
}

static const char *recipe_full_name(markov_fingerprint_recipe r) {
    switch (r) {
    case MARKOV_FP_IMAGE:       return "image";
    case MARKOV_FP_BASIN:       return "basin";
    case MARKOV_FP_FIXED_POINT: return "fixed-point";
    case MARKOV_FP_STEP_COUNT:  return "step-count";
    case MARKOV_FP_TRACE_SHAPE: return "trace-shape";
    default:                    return "?";
    }
}

static uint32_t recipe_id_for(markov_fingerprint_recipe r) {
    switch (r) {
    case MARKOV_FP_IMAGE:       return MARKOV_RECIPE_ID_IMAGE;
    case MARKOV_FP_BASIN:       return MARKOV_RECIPE_ID_BASIN;
    case MARKOV_FP_FIXED_POINT: return MARKOV_RECIPE_ID_FIXED_POINT;
    case MARKOV_FP_STEP_COUNT:  return MARKOV_RECIPE_ID_STEP_COUNT;
    default:                    return MARKOV_RECIPE_ID_BASIN;
    }
}

/* ---------------------------------------------------------------- */
/* Per-algorithm work                                                */
/* ---------------------------------------------------------------- */

static void process_one(size_t index,
                        uint8_t alphabet_size,
                        uint8_t rule_count,
                        uint8_t max_pat,
                        uint8_t max_repl,
                        uint8_t obs_lo,
                        uint8_t obs_hi,
                        bool observe,
                        markov_fingerprint_recipe recipe,
                        ThreadBuf *buf) {
    MarkovAlgorithm a;
    uint16_t indices[MARKOV_MAX_RULES] = {0};
    if (!markov_enumerate_at(&a, index, alphabet_size, rule_count,
                             max_pat, max_repl, indices)) {
        buf->invalid++;
        return;
    }
    if (!markov_algorithm_valid(&a)) {
        buf->invalid++;
        return;
    }
    markov_canonicalize_letters(&a);

    AlgEntry e;
    memset(&e, 0, sizeof(e));
    e.canonical_id  = a.header.canonical_id;
    e.enum_index    = (uint64_t)index;
    e.linearity     = (uint8_t)markov_classify_linearity(&a);
    e.overlap_count = (uint32_t)markov_total_overlap_count(&a, obs_lo, obs_hi);
    e.match_count   = (uint32_t)markov_total_match_count(&a, obs_lo, obs_hi);
    if (observe) {
        e.fingerprint = markov_observe_fingerprint(&a, obs_lo, obs_hi, recipe);
    } else {
        e.fingerprint = 0;
    }
    /* rule_indices captured from markov_enumerate_at output. */
    for (uint8_t k = 0; k < rule_count; k++) e.rule_indices[k] = indices[k];

    buf->lin_count[e.linearity]++;
    threadbuf_push(buf, &e);
}

/* ---------------------------------------------------------------- */
/* Argument parsing                                                  */
/* ---------------------------------------------------------------- */

typedef struct {
    uint8_t alphabet_size;
    uint8_t rule_count;
    uint8_t max_pat;
    uint8_t max_repl;
    int     obs_lo;
    int     obs_hi;
    markov_fingerprint_recipe recipe;
    const char *records_prefix;  /* NULL = no records */
} CliArgs;

static void parse_args(int argc, char **argv, CliArgs *out) {
    out->alphabet_size = 2;
    out->rule_count    = 2;
    out->max_pat       = 2;
    out->max_repl      = 2;
    out->obs_lo        = 0;
    out->obs_hi        = 6;
    out->recipe        = MARKOV_FP_BASIN;
    out->records_prefix = NULL;

    /* Strip out --records <prefix> first, treat the rest as positional. */
    char *positional[16];
    int pos_count = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--records") == 0 && i + 1 < argc) {
            out->records_prefix = argv[i + 1];
            i++;
        } else if (pos_count < 16) {
            positional[pos_count++] = argv[i];
        }
    }
    if (pos_count > 0) out->alphabet_size = (uint8_t)atoi(positional[0]);
    if (pos_count > 1) out->rule_count    = (uint8_t)atoi(positional[1]);
    if (pos_count > 2) out->max_pat       = (uint8_t)atoi(positional[2]);
    if (pos_count > 3) out->max_repl      = (uint8_t)atoi(positional[3]);
    if (pos_count > 4) out->obs_lo        = atoi(positional[4]);
    if (pos_count > 5) out->obs_hi        = atoi(positional[5]);
    if (pos_count > 6) out->recipe        = parse_recipe(positional[6]);
}

/* ---------------------------------------------------------------- */
/* Records: sort + dedupe + write                                    */
/* ---------------------------------------------------------------- */

/* Dedupe a sorted-by-canonical_id array: keep first occurrence of each
 * canonical_id (entries are otherwise identical for the same canonical_id —
 * letter-permutation-equivalent algorithms produce the same canonical form
 * after canonicalize_letters, so all derived fields agree). Returns the
 * deduped count. */
static size_t dedupe_sorted_entries(AlgEntry *entries, size_t n) {
    if (n == 0) return 0;
    size_t out = 1;
    for (size_t i = 1; i < n; i++) {
        if (entries[i].canonical_id != entries[out - 1].canonical_id) {
            entries[out++] = entries[i];
        }
    }
    return out;
}

static bool write_records(const char *prefix,
                          const CliArgs *args,
                          const AlgEntry *deduped,
                          size_t n_canonical,
                          bool observe) {
    /* <prefix>.struct */
    char struct_path[1024];
    snprintf(struct_path, sizeof(struct_path), "%s.struct", prefix);

    size_t stride = markov_struct_record_size(args->rule_count);
    uint8_t *struct_buf = (uint8_t *)calloc(n_canonical, stride);
    if (struct_buf == NULL) {
        fprintf(stderr, "OOM allocating struct buffer\n");
        return false;
    }
    for (size_t i = 0; i < n_canonical; i++) {
        markov_pack_struct_record(struct_buf + i * stride,
            args->rule_count,
            deduped[i].canonical_id,
            deduped[i].overlap_count,
            deduped[i].match_count,
            deduped[i].linearity,
            deduped[i].rule_indices);
    }
    bool ok = markov_write_struct_file(struct_path,
        args->alphabet_size, args->rule_count,
        args->max_pat, args->max_repl,
        struct_buf, n_canonical);
    free(struct_buf);
    if (!ok) {
        fprintf(stderr, "Failed to write %s\n", struct_path);
        return false;
    }
    fprintf(stderr, "Wrote %s (%zu records, stride=%zu bytes)\n",
        struct_path, n_canonical, stride);

    if (!observe) return true;

    /* <prefix>.<recipe>.behav */
    char behav_path[1024];
    snprintf(behav_path, sizeof(behav_path), "%s.%s.behav",
        prefix, recipe_short_name(args->recipe));

    MarkovBehavRecord *behav = (MarkovBehavRecord *)
        malloc(n_canonical * sizeof(MarkovBehavRecord));
    if (behav == NULL) {
        fprintf(stderr, "OOM allocating behav buffer\n");
        return false;
    }
    for (size_t i = 0; i < n_canonical; i++) {
        behav[i].canonical_id   = deduped[i].canonical_id;
        behav[i].fingerprint_hi = 0;  /* reserved for 128-bit hashes */
        behav[i].fingerprint_lo = deduped[i].fingerprint;
    }
    ok = markov_write_behav_file(behav_path,
        args->alphabet_size, args->rule_count,
        recipe_id_for(args->recipe),
        behav, n_canonical);
    free(behav);
    if (!ok) {
        fprintf(stderr, "Failed to write %s\n", behav_path);
        return false;
    }
    fprintf(stderr, "Wrote %s (%zu records)\n", behav_path, n_canonical);
    return true;
}

/* ---------------------------------------------------------------- */
/* main                                                              */
/* ---------------------------------------------------------------- */

int main(int argc, char **argv) {
    CliArgs args;
    parse_args(argc, argv, &args);

    size_t shapes = markov_rule_shape_count(args.alphabet_size,
                                            args.max_pat, args.max_repl);
    size_t total = markov_total_count(args.alphabet_size, args.rule_count,
                                       args.max_pat, args.max_repl);
    bool observe = (args.obs_hi >= args.obs_lo && args.obs_hi >= 0);

    fprintf(stderr,
        "Enumerating: alphabet_size=%u rule_count=%u max_pat=%u max_repl=%u\n"
        "Per-rule shapes: %zu\n"
        "Total algorithms: %zu\n",
        args.alphabet_size, args.rule_count, args.max_pat, args.max_repl,
        shapes, total);
    if (observe) {
        fprintf(stderr, "Observation:  range=%d..%d  recipe=%s\n",
            args.obs_lo, args.obs_hi, recipe_full_name(args.recipe));
    } else {
        fprintf(stderr, "Observation:  (skipped)\n");
    }
#if ENUM_OMP
    fprintf(stderr, "Threads:      %d (OpenMP)\n", omp_get_max_threads());
#else
    fprintf(stderr, "Threads:      1 (serial)\n");
#endif
    if (args.records_prefix) {
        fprintf(stderr, "Records:      <%s>.struct, <%s>.%s.behav\n",
            args.records_prefix, args.records_prefix,
            recipe_short_name(args.recipe));
    }

    /* ---- Allocate per-thread buffers ---- */
    int n_threads = 1;
#if ENUM_OMP
    n_threads = omp_get_max_threads();
#endif
    ThreadBuf *bufs = (ThreadBuf *)calloc(n_threads, sizeof(ThreadBuf));
    if (bufs == NULL) { fprintf(stderr, "OOM\n"); exit(1); }

    uint8_t obs_lo = (uint8_t)(args.obs_lo < 0 ? 0 : args.obs_lo);
    uint8_t obs_hi = (uint8_t)(args.obs_hi < 0 ? 0 : args.obs_hi);

    clock_t t0 = clock();

#if ENUM_OMP
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        ThreadBuf *buf = &bufs[tid];
        threadbuf_init(buf);
        #pragma omp for schedule(dynamic, 1024)
        for (size_t i = 0; i < total; i++) {
            process_one(i, args.alphabet_size, args.rule_count,
                args.max_pat, args.max_repl,
                obs_lo, obs_hi, observe, args.recipe, buf);
        }
    }
#else
    threadbuf_init(&bufs[0]);
    for (size_t i = 0; i < total; i++) {
        process_one(i, args.alphabet_size, args.rule_count,
            args.max_pat, args.max_repl,
            obs_lo, obs_hi, observe, args.recipe, &bufs[0]);
    }
#endif

    clock_t t1 = clock();
    double enum_secs = (double)(t1 - t0) / CLOCKS_PER_SEC;

    /* ---- Merge thread-local buffers into one big array ---- */
    size_t total_count = 0, total_invalid = 0;
    size_t lin_total[MARKOV_LIN_COUNT] = {0};
    for (int t = 0; t < n_threads; t++) {
        total_count   += bufs[t].count;
        total_invalid += bufs[t].invalid;
        for (int l = 0; l < MARKOV_LIN_COUNT; l++)
            lin_total[l] += bufs[t].lin_count[l];
    }

    AlgEntry *all = (AlgEntry *)malloc(total_count * sizeof(AlgEntry));
    if (all == NULL && total_count > 0) { fprintf(stderr, "OOM\n"); exit(1); }
    {
        size_t off = 0;
        for (int t = 0; t < n_threads; t++) {
            if (bufs[t].count > 0) {
                memcpy(all + off, bufs[t].entries,
                       bufs[t].count * sizeof(AlgEntry));
                off += bufs[t].count;
            }
            threadbuf_free(&bufs[t]);
        }
    }
    free(bufs);

    /* Sort merged array by canonical_id (stable order across runs). */
    qsort(all, total_count, sizeof(AlgEntry), compare_alg_by_canonical);

    /* ---- Counting structural / behavioral classes ---- */
    /* Structural: distinct canonical_ids = unique runs in the sorted array. */
    size_t struct_unique = 0;
    if (total_count > 0) {
        struct_unique = 1;
        for (size_t i = 1; i < total_count; i++) {
            if (all[i].canonical_id != all[i - 1].canonical_id) struct_unique++;
        }
    }

    /* Behavioral: distinct fingerprints. Build a temporary u64 array, sort, count.
       Note: count distinct fingerprints across raw algorithms (not deduped), which
       matches the original aggregate-count semantics and gives the same answer
       because letter-permutation-equivalent algorithms have identical fingerprints
       under all our recipes. */
    size_t behav_unique = 0;
    if (observe && total_count > 0) {
        uint64_t *fps = (uint64_t *)malloc(total_count * sizeof(uint64_t));
        if (fps == NULL) { fprintf(stderr, "OOM\n"); exit(1); }
        for (size_t i = 0; i < total_count; i++) fps[i] = all[i].fingerprint;
        qsort(fps, total_count, sizeof(uint64_t), compare_uint64);
        behav_unique = count_unique_sorted_u64(fps, total_count);
        free(fps);
    }
    clock_t t2 = clock();
    double sort_secs = (double)(t2 - t1) / CLOCKS_PER_SEC;

    fprintf(stderr,
        "Visited:   %zu\n"
        "Invalid:   %zu (skipped)\n"
        "Structural classes (canonical_id):  %zu\n",
        total_count + total_invalid, total_invalid, struct_unique);
    if (observe) {
        fprintf(stderr, "Behavioral classes (%s):  %zu\n",
            recipe_full_name(args.recipe), behav_unique);
    }
    fprintf(stderr,
        "Enum+canonicalise+observe: %.3fs\n"
        "Sort+uniq:                 %.3fs\n",
        enum_secs, sort_secs);

    if (struct_unique > 0) {
        fprintf(stderr, "Letter-perm reduction:    %.2fx\n",
            (double)total_count / (double)struct_unique);
    }
    if (observe && behav_unique > 0) {
        fprintf(stderr, "Structural / behavioral:  %.2fx\n",
            (double)struct_unique / (double)behav_unique);
    }

    /* ---- Linearity buckets (counts already in lin_total; struct/behav
     *      we'll compute from the sorted `all` array). ---- */
    fprintf(stderr,
        "\nLinearity buckets (rule-scheme classification):\n"
        "  bucket    count   struct%s%s\n",
        observe ? "  behav " : "",
        observe ? "  s/b" : "");
    for (uint8_t lin = 0; lin < MARKOV_LIN_COUNT; lin++) {
        size_t n = lin_total[lin];
        if (n == 0) {
            fprintf(stderr, "  %-9s  %6zu      0%s%s\n",
                markov_linearity_name((markov_linearity_status)lin), n,
                observe ? "      0" : "",
                observe ? "    -" : "");
            continue;
        }
        uint64_t *bs = (uint64_t *)malloc(n * sizeof(uint64_t));
        uint64_t *bb = observe ? (uint64_t *)malloc(n * sizeof(uint64_t)) : NULL;
        size_t k = 0;
        for (size_t i = 0; i < total_count; i++) {
            if (all[i].linearity == lin) {
                bs[k] = all[i].canonical_id;
                if (observe) bb[k] = all[i].fingerprint;
                k++;
            }
        }
        qsort(bs, k, sizeof(uint64_t), compare_uint64);
        size_t s_unique = count_unique_sorted_u64(bs, k);
        size_t b_unique = 0;
        if (observe) {
            qsort(bb, k, sizeof(uint64_t), compare_uint64);
            b_unique = count_unique_sorted_u64(bb, k);
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

    /* ---- Overlap and match binning ---- */
    if (observe) {
        const char *names[2] = { "overlap", "match" };
        for (int v = 0; v < 2; v++) {
            uint32_t maxv = 0;
            for (size_t i = 0; i < total_count; i++) {
                uint32_t val = (v == 0) ? all[i].overlap_count : all[i].match_count;
                if (val > maxv) maxv = val;
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
                size_t n = 0;
                for (size_t i = 0; i < total_count; i++) {
                    uint32_t val = (v == 0) ? all[i].overlap_count : all[i].match_count;
                    if (val >= bin_lo && val <= bin_hi) n++;
                }
                if (n == 0) continue;
                uint64_t *bs = (uint64_t *)malloc(n * sizeof(uint64_t));
                uint64_t *bb = (uint64_t *)malloc(n * sizeof(uint64_t));
                size_t k = 0;
                for (size_t i = 0; i < total_count; i++) {
                    uint32_t val = (v == 0) ? all[i].overlap_count : all[i].match_count;
                    if (val >= bin_lo && val <= bin_hi) {
                        bs[k] = all[i].canonical_id;
                        bb[k] = all[i].fingerprint;
                        k++;
                    }
                }
                qsort(bs, k, sizeof(uint64_t), compare_uint64);
                qsort(bb, k, sizeof(uint64_t), compare_uint64);
                size_t s_unique = count_unique_sorted_u64(bs, k);
                size_t b_unique = count_unique_sorted_u64(bb, k);
                fprintf(stderr,
                    "  %6u  %6u  %6zu  %6zu  %5zu  %5.3f\n",
                    bin_lo, bin_hi, n, s_unique, b_unique,
                    s_unique > 0 ? (double)b_unique / (double)s_unique : 0.0);
                free(bs);
                free(bb);
            }
        }
    }

    /* ---- Records: dedupe sorted entries to one per canonical_id, write ---- */
    if (args.records_prefix != NULL) {
        size_t n_canonical = dedupe_sorted_entries(all, total_count);
        if (n_canonical != struct_unique) {
            fprintf(stderr,
                "Warning: dedupe produced %zu canonical entries but counted %zu unique struct ids\n",
                n_canonical, struct_unique);
        }
        if (!write_records(args.records_prefix, &args, all, n_canonical, observe)) {
            free(all);
            return 1;
        }
    }

    free(all);
    return 0;
}
