/* verify_records: read a <prefix>.struct file and one or more
 * <prefix>.<recipe>.behav files, join on canonical_id, and print histograms
 * matching the enumerator's aggregate output.
 *
 * Usage:
 *   verify_records <prefix> [recipe1 recipe2 ...]
 *
 * If no recipes are given, the tool prints structural-only stats. With
 * recipes, each .behav file is opened, joined on canonical_id, and the
 * structural × behavioral cross-tabulation is printed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "markov_records.h"
#include "markov_record.h"

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

static const char *linearity_name(uint8_t lin) {
    switch (lin) {
    case 0: return "bilinear";
    case 1: return "lhs-only";
    case 2: return "rhs-only";
    case 3: return "neither";
    default: return "?";
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <prefix> [recipe1 recipe2 ...]\n", argv[0]);
        return 1;
    }
    const char *prefix = argv[1];

    /* ---- Load structural file ---- */
    char path[1024];
    snprintf(path, sizeof(path), "%s.struct", prefix);

    MarkovStructHeader sh;
    uint8_t *struct_buf = NULL;
    size_t n_struct = 0;
    if (!markov_read_struct_file(path, &sh, &struct_buf, &n_struct)) {
        fprintf(stderr, "Failed to read %s\n", path);
        return 1;
    }
    size_t stride = markov_struct_record_size((uint8_t)sh.rule_count);

    fprintf(stderr,
        "Loaded %s\n"
        "  alphabet_size=%u rule_count=%u max_pat=%u max_repl=%u\n"
        "  records=%zu stride=%zu bytes\n",
        path, sh.alphabet_size, sh.rule_count, sh.max_pat, sh.max_repl,
        n_struct, stride);

    /* Sanity: are canonical_ids sorted and unique? */
    uint64_t prev_id = 0;
    bool sorted = true, unique = true;
    for (size_t i = 0; i < n_struct; i++) {
        uint64_t cid;
        markov_unpack_struct_record(struct_buf + i * stride,
            (uint8_t)sh.rule_count, &cid, NULL, NULL, NULL, NULL);
        if (i > 0) {
            if (cid < prev_id) sorted = false;
            if (cid == prev_id) unique = false;
        }
        prev_id = cid;
    }
    fprintf(stderr, "  sorted-by-canonical_id: %s, unique: %s\n",
        sorted ? "yes" : "NO", unique ? "yes" : "NO");

    /* ---- Per-bucket histogram from struct file ---- */
    size_t lin_count[4] = {0};
    uint32_t overlap_max = 0, match_max = 0;
    for (size_t i = 0; i < n_struct; i++) {
        uint64_t cid;
        uint32_t ov, mc;
        uint8_t lin;
        markov_unpack_struct_record(struct_buf + i * stride,
            (uint8_t)sh.rule_count, &cid, &ov, &mc, &lin, NULL);
        if (lin < 4) lin_count[lin]++;
        if (ov > overlap_max) overlap_max = ov;
        if (mc > match_max) match_max = mc;
    }

    fprintf(stderr, "\nLinearity buckets (canonical algorithms):\n");
    for (uint8_t lin = 0; lin < 4; lin++) {
        fprintf(stderr, "  %-9s  %6zu\n", linearity_name(lin), lin_count[lin]);
    }
    fprintf(stderr, "\noverlap_max=%u  match_max=%u\n", overlap_max, match_max);

    /* ---- For each recipe argument: load behav, join, print stats ---- */
    for (int ri = 2; ri < argc; ri++) {
        const char *recipe_name = argv[ri];
        snprintf(path, sizeof(path), "%s.%s.behav", prefix, recipe_name);
        MarkovBehavHeader bh;
        MarkovBehavRecord *behav = NULL;
        size_t n_behav = 0;
        if (!markov_read_behav_file(path, &bh, &behav, &n_behav)) {
            fprintf(stderr, "Failed to read %s\n", path);
            continue;
        }
        fprintf(stderr,
            "\nLoaded %s\n"
            "  recipe=%s alphabet_size=%u rule_count=%u records=%zu\n",
            path, markov_recipe_name_from_id(bh.recipe_id),
            bh.alphabet_size, bh.rule_count, n_behav);

        if (n_behav != n_struct) {
            fprintf(stderr,
                "  WARN: behav record count (%zu) != struct count (%zu)\n",
                n_behav, n_struct);
        }

        /* Behav file is sorted by canonical_id and parallel to struct file —
         * join is a single linear scan. */
        size_t mismatches = 0;
        uint64_t *fps = (uint64_t *)malloc(n_behav * sizeof(uint64_t));
        if (fps == NULL) { fprintf(stderr, "OOM\n"); free(behav); continue; }
        for (size_t i = 0; i < n_behav; i++) {
            fps[i] = behav[i].fingerprint_lo;
            if (i < n_struct) {
                uint64_t cid_struct;
                markov_unpack_struct_record(struct_buf + i * stride,
                    (uint8_t)sh.rule_count, &cid_struct, NULL, NULL, NULL, NULL);
                if (cid_struct != behav[i].canonical_id) mismatches++;
            }
        }
        fprintf(stderr, "  canonical_id alignment mismatches: %zu\n", mismatches);

        /* Distinct fingerprints. */
        qsort(fps, n_behav, sizeof(uint64_t), compare_uint64);
        size_t b_unique = count_unique_sorted(fps, n_behav);
        fprintf(stderr, "  distinct fingerprints (behavioral classes): %zu\n",
            b_unique);
        if (b_unique > 0) {
            fprintf(stderr, "  s/b ratio (canonical / behavioral): %.2fx\n",
                (double)n_struct / (double)b_unique);
        }
        free(fps);
        free(behav);
    }

    free(struct_buf);
    return 0;
}
