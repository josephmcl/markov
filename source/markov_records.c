#include "markov_records.h"

#include <stdlib.h>
#include <string.h>

/* Little-endian helpers. Most modern hosts are LE so memcpy works directly,
 * but spelling the encoding explicitly keeps the format byte-exact across
 * any host. */

static void put_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static void put_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static void put_u64_le(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}
static uint16_t get_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t get_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t get_u64_le(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

void markov_pack_struct_record(uint8_t *out_buf,
                               uint8_t  rule_count,
                               uint64_t canonical_id,
                               uint32_t overlap_count,
                               uint32_t match_count,
                               uint8_t  linearity_bucket,
                               const uint16_t *rule_indices) {
    size_t stride = markov_struct_record_size(rule_count);
    memset(out_buf, 0, stride);
    put_u64_le(out_buf + 0, canonical_id);
    put_u32_le(out_buf + 8, overlap_count);
    put_u32_le(out_buf + 12, match_count);
    out_buf[16] = linearity_bucket;
    for (uint8_t k = 0; k < rule_count; k++) {
        put_u16_le(out_buf + 17 + 2 * k, rule_indices[k]);
    }
    /* Trailing bytes already zeroed by memset for deterministic padding. */
}

void markov_unpack_struct_record(const uint8_t *in_buf,
                                 uint8_t  rule_count,
                                 uint64_t *canonical_id,
                                 uint32_t *overlap_count,
                                 uint32_t *match_count,
                                 uint8_t  *linearity_bucket,
                                 uint16_t *rule_indices) {
    if (canonical_id)     *canonical_id = get_u64_le(in_buf + 0);
    if (overlap_count)    *overlap_count = get_u32_le(in_buf + 8);
    if (match_count)      *match_count = get_u32_le(in_buf + 12);
    if (linearity_bucket) *linearity_bucket = in_buf[16];
    if (rule_indices) {
        for (uint8_t k = 0; k < rule_count; k++) {
            rule_indices[k] = get_u16_le(in_buf + 17 + 2 * k);
        }
    }
}

static bool write_struct_header(FILE *f,
                                uint8_t alphabet_size,
                                uint8_t rule_count,
                                uint8_t max_pat,
                                uint8_t max_repl,
                                uint64_t record_count) {
    uint8_t hdr[32];
    memset(hdr, 0, sizeof(hdr));
    put_u32_le(hdr + 0,  MARKOV_RECORD_STRUCT_MAGIC);
    put_u32_le(hdr + 4,  MARKOV_RECORD_VERSION);
    put_u32_le(hdr + 8,  (uint32_t)alphabet_size);
    put_u32_le(hdr + 12, (uint32_t)rule_count);
    put_u32_le(hdr + 16, (uint32_t)max_pat);
    put_u32_le(hdr + 20, (uint32_t)max_repl);
    put_u64_le(hdr + 24, record_count);
    /* hdr[28..32] reserved (zero) */
    return fwrite(hdr, 32, 1, f) == 1;
}

static bool write_behav_header(FILE *f,
                               uint8_t alphabet_size,
                               uint8_t rule_count,
                               uint32_t recipe_id,
                               uint64_t record_count) {
    uint8_t hdr[32];
    memset(hdr, 0, sizeof(hdr));
    put_u32_le(hdr + 0,  MARKOV_RECORD_BEHAV_MAGIC);
    put_u32_le(hdr + 4,  MARKOV_RECORD_VERSION);
    put_u32_le(hdr + 8,  (uint32_t)alphabet_size);
    put_u32_le(hdr + 12, (uint32_t)rule_count);
    put_u32_le(hdr + 16, recipe_id);
    /* hdr[20..24] reserved1 (zero) */
    put_u64_le(hdr + 24, record_count);
    return fwrite(hdr, 32, 1, f) == 1;
}

bool markov_write_struct_file(const char *path,
                              uint8_t alphabet_size,
                              uint8_t rule_count,
                              uint8_t max_pat,
                              uint8_t max_repl,
                              const uint8_t *records_buf,
                              size_t record_count) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;
    bool ok = write_struct_header(f, alphabet_size, rule_count, max_pat,
                                  max_repl, (uint64_t)record_count);
    if (ok && record_count > 0) {
        size_t stride = markov_struct_record_size(rule_count);
        ok = (fwrite(records_buf, stride, record_count, f) == record_count);
    }
    if (fclose(f) != 0) ok = false;
    return ok;
}

bool markov_write_behav_file(const char *path,
                             uint8_t alphabet_size,
                             uint8_t rule_count,
                             uint32_t recipe_id,
                             const MarkovBehavRecord *records,
                             size_t record_count) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;
    bool ok = write_behav_header(f, alphabet_size, rule_count, recipe_id,
                                 (uint64_t)record_count);
    /* Encode each record explicitly (LE) to avoid host-alignment surprises. */
    if (ok) {
        for (size_t i = 0; i < record_count; i++) {
            uint8_t buf[24];
            put_u64_le(buf + 0, records[i].canonical_id);
            put_u64_le(buf + 8, records[i].fingerprint_hi);
            put_u64_le(buf + 16, records[i].fingerprint_lo);
            if (fwrite(buf, 24, 1, f) != 1) { ok = false; break; }
        }
    }
    if (fclose(f) != 0) ok = false;
    return ok;
}

/* Reader helpers ----------------------------------------------------- */

static bool read_struct_header(FILE *f, MarkovStructHeader *out) {
    uint8_t hdr[32];
    if (fread(hdr, 32, 1, f) != 1) return false;
    out->magic         = get_u32_le(hdr + 0);
    out->format_version= get_u32_le(hdr + 4);
    out->alphabet_size = get_u32_le(hdr + 8);
    out->rule_count    = get_u32_le(hdr + 12);
    out->max_pat       = get_u32_le(hdr + 16);
    out->max_repl      = get_u32_le(hdr + 20);
    out->record_count  = get_u64_le(hdr + 24);
    out->reserved      = 0;
    return out->magic == MARKOV_RECORD_STRUCT_MAGIC &&
           out->format_version == MARKOV_RECORD_VERSION;
}

static bool read_behav_header(FILE *f, MarkovBehavHeader *out) {
    uint8_t hdr[32];
    if (fread(hdr, 32, 1, f) != 1) return false;
    out->magic         = get_u32_le(hdr + 0);
    out->format_version= get_u32_le(hdr + 4);
    out->alphabet_size = get_u32_le(hdr + 8);
    out->rule_count    = get_u32_le(hdr + 12);
    out->recipe_id     = get_u32_le(hdr + 16);
    out->reserved1     = 0;
    out->record_count  = get_u64_le(hdr + 24);
    return out->magic == MARKOV_RECORD_BEHAV_MAGIC &&
           out->format_version == MARKOV_RECORD_VERSION;
}

bool markov_read_struct_file(const char *path,
                             MarkovStructHeader *out_header,
                             uint8_t **out_records,
                             size_t *out_record_count) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    MarkovStructHeader hdr;
    if (!read_struct_header(f, &hdr)) { fclose(f); return false; }
    size_t stride = markov_struct_record_size((uint8_t)hdr.rule_count);
    size_t bytes = (size_t)hdr.record_count * stride;
    uint8_t *buf = NULL;
    if (bytes > 0) {
        buf = (uint8_t *)malloc(bytes);
        if (buf == NULL) { fclose(f); return false; }
        if (fread(buf, 1, bytes, f) != bytes) {
            free(buf); fclose(f); return false;
        }
    }
    fclose(f);
    if (out_header)        *out_header = hdr;
    if (out_records)       *out_records = buf;
    else free(buf);
    if (out_record_count)  *out_record_count = (size_t)hdr.record_count;
    return true;
}

bool markov_read_behav_file(const char *path,
                            MarkovBehavHeader *out_header,
                            MarkovBehavRecord **out_records,
                            size_t *out_record_count) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    MarkovBehavHeader hdr;
    if (!read_behav_header(f, &hdr)) { fclose(f); return false; }
    size_t n = (size_t)hdr.record_count;
    MarkovBehavRecord *recs = NULL;
    if (n > 0) {
        recs = (MarkovBehavRecord *)malloc(n * sizeof(MarkovBehavRecord));
        if (recs == NULL) { fclose(f); return false; }
        for (size_t i = 0; i < n; i++) {
            uint8_t buf[24];
            if (fread(buf, 24, 1, f) != 1) {
                free(recs); fclose(f); return false;
            }
            recs[i].canonical_id   = get_u64_le(buf + 0);
            recs[i].fingerprint_hi = get_u64_le(buf + 8);
            recs[i].fingerprint_lo = get_u64_le(buf + 16);
        }
    }
    fclose(f);
    if (out_header)        *out_header = hdr;
    if (out_records)       *out_records = recs;
    else free(recs);
    if (out_record_count)  *out_record_count = n;
    return true;
}

int markov_recipe_id_from_name(const char *name) {
    if (name == NULL) return -1;
    if (strcmp(name, "image") == 0) return MARKOV_RECIPE_ID_IMAGE;
    if (strcmp(name, "basin") == 0) return MARKOV_RECIPE_ID_BASIN;
    if (strcmp(name, "fixed") == 0) return MARKOV_RECIPE_ID_FIXED_POINT;
    if (strcmp(name, "fixed-point") == 0) return MARKOV_RECIPE_ID_FIXED_POINT;
    if (strcmp(name, "steps") == 0) return MARKOV_RECIPE_ID_STEP_COUNT;
    if (strcmp(name, "step-count") == 0) return MARKOV_RECIPE_ID_STEP_COUNT;
    return -1;
}

const char *markov_recipe_name_from_id(uint32_t recipe_id) {
    switch (recipe_id) {
    case MARKOV_RECIPE_ID_IMAGE:       return "image";
    case MARKOV_RECIPE_ID_BASIN:       return "basin";
    case MARKOV_RECIPE_ID_FIXED_POINT: return "fixed-point";
    case MARKOV_RECIPE_ID_STEP_COUNT:  return "step-count";
    default:                           return "unknown";
    }
}
