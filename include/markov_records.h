#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "markov_record.h"

/* On-disk record format for per-algorithm experiment outputs.
 *
 * Two files per (cell, recipe) sweep:
 *
 *   <prefix>.struct           — recipe-independent, one record per canonical
 *                               algorithm (post letter-permutation dedup).
 *   <prefix>.<recipe>.behav   — one record per (canonical_id, fingerprint)
 *                               pair; sorted to match the struct file.
 *
 * Both files use little-endian, packed (no native-alignment padding) headers
 * followed by fixed-stride records. The struct record's stride depends on
 * rule_count and is written out in the header, not inferred from the file
 * size; readers should compute stride = round_up_8(16 + 2 * rule_count) and
 * verify against the file size.
 */

#define MARKOV_RECORD_STRUCT_MAGIC 0x4D415354U  /* 'M''A''S''T' little-endian */
#define MARKOV_RECORD_BEHAV_MAGIC  0x4D414245U  /* 'M''A''B''E' little-endian */
#define MARKOV_RECORD_VERSION      1u

/* Recipe-id values stored in MarkovBehavHeader.recipe_id. */
#define MARKOV_RECIPE_ID_IMAGE       0u
#define MARKOV_RECIPE_ID_BASIN       1u
#define MARKOV_RECIPE_ID_FIXED_POINT 2u
#define MARKOV_RECIPE_ID_STEP_COUNT  3u

#pragma pack(push, 1)

typedef struct {
    uint32_t magic;            /* MARKOV_RECORD_STRUCT_MAGIC */
    uint32_t format_version;
    uint32_t alphabet_size;
    uint32_t rule_count;
    uint32_t max_pat;
    uint32_t max_repl;
    uint64_t record_count;     /* number of MarkovStructRecord entries that follow */
    uint32_t reserved;
} MarkovStructHeader;
/* sizeof = 32 */

typedef struct {
    uint32_t magic;            /* MARKOV_RECORD_BEHAV_MAGIC */
    uint32_t format_version;
    uint32_t alphabet_size;
    uint32_t rule_count;
    uint32_t recipe_id;
    uint32_t reserved1;
    uint64_t record_count;
} MarkovBehavHeader;
/* sizeof = 32 */

typedef struct {
    uint64_t canonical_id;
    uint64_t fingerprint_hi;   /* always 0 in v1; reserved for 128-bit hashes */
    uint64_t fingerprint_lo;   /* the FNV-1a 64-bit fingerprint */
} MarkovBehavRecord;
/* sizeof = 24 */

#pragma pack(pop)

/* Per-canonical structural record. The trailing rule_indices[] is variable-
 * length (rule_count entries), padded to 8-byte alignment. We do not declare
 * a struct for this layout because C cannot express it portably; readers
 * and writers use markov_struct_record_size() to compute the stride and
 * memcpy at fixed byte offsets. */

/* Layout per record (little-endian, packed):
 *    [0,8)   canonical_id (uint64)
 *    [8,12)  overlap_count (uint32)
 *    [12,16) match_count (uint32)
 *    [16,17) linearity_bucket (uint8) — 0=bilinear, 1=lhs-only, 2=rhs-only, 3=neither
 *    [17,17+2k) rule_indices (uint16 × rule_count)
 *    pad to multiple of 8
 */

/* Compute the on-disk struct record size for a given rule_count, including
 * padding to 8-byte alignment. */
static inline size_t markov_struct_record_size(uint8_t rule_count) {
    size_t raw = 17u + 2u * (size_t)rule_count;
    return (raw + 7u) & ~(size_t)7u;
}

/* ------------------------------------------------------------------ */
/* Writer-side helpers                                                 */
/* ------------------------------------------------------------------ */

/* Serialize one struct record into out_buf. The buffer must be at least
 * markov_struct_record_size(rule_count) bytes. The padding bytes are
 * zeroed so the output is bytewise reproducible. */
void markov_pack_struct_record(uint8_t *out_buf,
                               uint8_t  rule_count,
                               uint64_t canonical_id,
                               uint32_t overlap_count,
                               uint32_t match_count,
                               uint8_t  linearity_bucket,
                               const uint16_t *rule_indices);

/* Read fields from a packed struct record. */
void markov_unpack_struct_record(const uint8_t *in_buf,
                                 uint8_t  rule_count,
                                 uint64_t *canonical_id,
                                 uint32_t *overlap_count,
                                 uint32_t *match_count,
                                 uint8_t  *linearity_bucket,
                                 uint16_t *rule_indices);

/* Write a complete struct file. Records must already be sorted by
 * canonical_id and deduplicated. Returns true on success. */
bool markov_write_struct_file(const char *path,
                              uint8_t alphabet_size,
                              uint8_t rule_count,
                              uint8_t max_pat,
                              uint8_t max_repl,
                              const uint8_t *records_buf,   /* record_count * stride */
                              size_t record_count);

/* Write a complete behav file. records must be sorted by canonical_id. */
bool markov_write_behav_file(const char *path,
                             uint8_t alphabet_size,
                             uint8_t rule_count,
                             uint32_t recipe_id,
                             const MarkovBehavRecord *records,
                             size_t record_count);

/* ------------------------------------------------------------------ */
/* Reader-side helpers                                                 */
/* ------------------------------------------------------------------ */

/* Read a full struct file into memory. Caller frees *out_records via free().
 * On success: *out_record_count is set, *out_records is malloc'd buffer of
 * (record_count * markov_struct_record_size(header->rule_count)) bytes. */
bool markov_read_struct_file(const char *path,
                             MarkovStructHeader *out_header,
                             uint8_t **out_records,
                             size_t *out_record_count);

bool markov_read_behav_file(const char *path,
                            MarkovBehavHeader *out_header,
                            MarkovBehavRecord **out_records,
                            size_t *out_record_count);

/* Map a recipe name string ("image", "basin", "fixed", "steps") to a
 * MARKOV_RECIPE_ID_* value. Returns -1 on unrecognised names. */
int markov_recipe_id_from_name(const char *name);
const char *markov_recipe_name_from_id(uint32_t recipe_id);
