#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Markov algorithm portable record format.
 *
 * This is the on-the-wire representation used by:
 *   - the offline enumerator/canonicalizer for paper #1
 *   - cross-target harnesses (CPU bit-parallel, GPU/WebGPU later)
 *   - .obs file metadata blocks
 *
 * Design constraints:
 *   - No pointers anywhere; everything inline.
 *   - Fixed-stride rules within an algorithm (uses_schema = false implies
 *     all rules are 16-byte literal rules; uses_schema = true implies all
 *     rules are the larger schema layout — that branch is reserved here
 *     and not yet implemented).
 *   - Endian-explicit. All multi-byte fields are little-endian.
 *   - Forward-compatible. The format_version and reserved fields anticipate
 *     extension (schema rules, alphabets > 64, larger pattern bit widths).
 *
 * Limits in this version (1.0):
 *   - abstract_size ≤ 8 (fits 3-bit-per-letter packing in u32)
 *   - rule_count ≤ 16
 *   - target_size ≤ 64 (legal_*_mask are u64)
 *   - pattern/replacement up to ⌊32 / bits_per_letter⌋ letters
 */

#define MARKOV_FORMAT_MAGIC          0x4B52414DU  /* 'MARK' little-endian */
#define MARKOV_FORMAT_VERSION_MAJOR  1
#define MARKOV_FORMAT_VERSION_MINOR  0

#define MARKOV_MAX_RULES             16
#define MARKOV_MAX_ABSTRACT          8

/* Algorithm header flags */
#define MARKOV_ALG_FLAG_USES_SCHEMA  0x0001  /* RESERVED — schema rules not yet implemented */

/* Rule flags */
#define MARKOV_RULE_FLAG_TERMINAL    0x01  /* matches once and halts */
#define MARKOV_RULE_FLAG_HAS_EMIT    0x02  /* originally had an emit string (kept as metadata) */
#define MARKOV_RULE_FLAG_SCHEMA      0x04  /* RESERVED — schema rule encoding */

/* Binding flags */
#define MARKOV_BIND_FLAG_INJECTIVE   0x01  /* distinct abstract → distinct concrete */
#define MARKOV_BIND_FLAG_TOTAL       0x02  /* every abstract has a concrete image */
#define MARKOV_BIND_FLAG_SURJECTIVE  0x04  /* every concrete has an abstract preimage */

/* A binding map sentinel for abstract letters with no concrete image (Error binds). */
#define MARKOV_BIND_NO_IMAGE         0xFF

/* Sampling strategies for an observation policy. */
typedef enum {
    MARKOV_SAMPLE_EXHAUSTIVE = 0,  /* enumerate every word in the length range */
    MARKOV_SAMPLE_UNIFORM    = 1,  /* random uniform sampling, n samples per length */
    MARKOV_SAMPLE_STRATIFIED = 2,  /* stratified sampling, parameters in sampling_param */
} markov_sampling_kind;

/* Fingerprint recipes determine what is hashed to compute equivalence-class IDs. */
typedef enum {
    MARKOV_FP_IMAGE       = 0,  /* set of outputs */
    MARKOV_FP_BASIN       = 1,  /* input -> output partition */
    MARKOV_FP_TRACE_SHAPE = 2,  /* sequence of (rule, position) per input */
    MARKOV_FP_FIXED_POINT = 3,  /* set of words mapped to themselves */
    MARKOV_FP_STEP_COUNT  = 4,  /* per-input step counts */
} markov_fingerprint_recipe;

/* ---------------------------------------------------------------------- */
/* Records                                                                 */
/* ---------------------------------------------------------------------- */

#pragma pack(push, 1)

typedef struct {
    uint32_t magic;             /* MARKOV_FORMAT_MAGIC */
    uint8_t  version_major;     /* 1 */
    uint8_t  version_minor;     /* 0 */
    uint16_t flags;             /* MARKOV_ALG_FLAG_* */
    uint8_t  abstract_size;     /* 1..MARKOV_MAX_ABSTRACT */
    uint8_t  rule_count;        /* 0..MARKOV_MAX_RULES */
    uint8_t  bits_per_letter;   /* ceil(log2(abstract_size)), or 1 if abstract_size == 1 */
    uint8_t  reserved0;
    uint32_t measure;           /* compile-time termination bound; 0 = unknown */
    uint64_t canonical_id;      /* hash of canonical form; 0 = not canonicalised */
} MarkovHeader;
/* sizeof = 24 */

typedef struct {
    uint8_t  flags;             /* MARKOV_RULE_FLAG_* */
    uint8_t  pattern_len;       /* number of letters */
    uint8_t  replacement_len;
    uint8_t  reserved0;
    uint32_t pattern_bits;      /* letters packed LSB-first, bits_per_letter each */
    uint32_t replacement_bits;
    uint32_t reserved1;         /* reserved for schema extension or wider patterns */
} MarkovRule;
/* sizeof = 16 */

typedef struct {
    MarkovHeader header;
    MarkovRule   rules[MARKOV_MAX_RULES];  /* only header.rule_count are valid */
} MarkovAlgorithm;
/* sizeof = 24 + 16*16 = 280 */

typedef struct {
    uint8_t  flags;             /* MARKOV_BIND_FLAG_* */
    uint8_t  abstract_size;     /* must equal algorithm's abstract_size */
    uint8_t  target_size;       /* concrete alphabet size, ≤ 64 in this version */
    uint8_t  reserved0;
    uint8_t  map[MARKOV_MAX_ABSTRACT];  /* abstract i -> concrete index, or NO_IMAGE */
    uint64_t legal_input_mask;       /* bit g set iff concrete index g is admissible as input */
    uint64_t legal_replacement_mask; /* bit g set iff concrete index g may appear in output */
} MarkovBinding;
/* sizeof = 4 + 8 + 8 + 8 = 28 */

typedef struct {
    uint8_t  length_lo;
    uint8_t  length_hi;
    uint8_t  sampling_kind;     /* markov_sampling_kind */
    uint8_t  fingerprint_recipe;/* markov_fingerprint_recipe */
    uint32_t reserved0;
    uint64_t sampling_param;    /* widened from u32 to forestall version migration */
} MarkovObservationPolicy;
/* sizeof = 16 */

typedef struct {
    MarkovAlgorithm        algorithm;
    MarkovBinding          binding;
    MarkovObservationPolicy policy;
} MarkovExecutionUnit;

#pragma pack(pop)

/* ---------------------------------------------------------------------- */
/* Constructors                                                            */
/* ---------------------------------------------------------------------- */

/* Initialise an empty algorithm header at major.minor 1.0 with the given size. */
void markov_header_init(MarkovHeader *h, uint8_t abstract_size, uint8_t rule_count);

/* Compute bits_per_letter from abstract_size. */
uint8_t markov_bits_per_letter(uint8_t abstract_size);

/* Construct an identity binding for an abstract algorithm of the given size.
 * Maps abstract i → concrete i, marks all flags set. */
void markov_binding_identity(MarkovBinding *b, uint8_t abstract_size);

/* Default observation policy: exhaustive over the given length range, image fingerprint. */
void markov_policy_default(MarkovObservationPolicy *p, uint8_t lo, uint8_t hi);

/* ---------------------------------------------------------------------- */
/* Pattern bit packing                                                     */
/* ---------------------------------------------------------------------- */

/* Pack a sequence of letter indices into pattern_bits using bits_per_letter
 * bits each, LSB-first. Returns false on overflow. */
bool markov_pack_pattern(const uint8_t *letters, uint8_t letter_count,
                         uint8_t bits_per_letter, uint32_t *out_bits);

/* Unpack the i-th letter (0..len-1) from a packed pattern_bits value. */
uint8_t markov_unpack_letter(uint32_t pattern_bits, uint8_t i, uint8_t bits_per_letter);

/* ---------------------------------------------------------------------- */
/* Validation                                                              */
/* ---------------------------------------------------------------------- */

/* Returns true if the record is structurally well-formed (magic, version,
 * sizes within bounds, schema flag consistent). Does NOT check semantic
 * properties like rule reachability or termination. */
bool markov_algorithm_valid(const MarkovAlgorithm *a);
bool markov_binding_valid(const MarkovBinding *b, const MarkovAlgorithm *a);

/* ---------------------------------------------------------------------- */
/* Encoding from the language's in-memory algorithm_definition.            */
/*                                                                         */
/* These bridge between the language compiler's internal representation    */
/* (in include/context/definitions.h) and the portable record format. The  */
/* encoder requires an algorithm with an abstract alphabet; concrete-only  */
/* algorithms are not yet supported in this version.                       */
/* ---------------------------------------------------------------------- */

struct adef;  /* algorithm_definition forward decl */

/* Encode an in-memory algorithm into a MarkovAlgorithm record. Returns
 * true on success. Fails if the algorithm has no abstract alphabet, has
 * too many rules, or contains patterns/replacements with letters outside
 * its declared abstract alphabet. */
bool markov_encode_algorithm(const struct adef *alg, MarkovAlgorithm *out);

/* Pretty-print a record to stdout, one line per rule plus a header line.
 * Format mirrors the textual form of the rules for easy verification. */
void markov_print_algorithm(const MarkovAlgorithm *a);
