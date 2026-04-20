#include "webassembly.h"

/*
 * WASM Generation Strategy (Letter-Index Encoding):
 *
 * Words are represented as sequences of 1-byte letter indices, not raw bytes.
 * Each unique letter across all contexts gets a global index 0..N-1.
 *
 * Memory Layout:
 *   Offset 0..1023:     Letter string data (null-separated raw bytes)
 *   Offset 1024..4095:  Letter offset table (i32 offset + i32 length per entry)
 *   Offset 4096+:       Working word buffer (letter index bytes)
 *
 * Generated functions:
 *   $memmove       - bidirectional memory move for substitutions
 *   $encode_word   - raw bytes -> letter indices (for host I/O)
 *   $decode_word   - letter indices -> raw bytes (for host I/O)
 *   $<algorithm>   - one per algorithm, operates on letter indices
 */

#define LETTER_DATA_BASE  0
#define LETTER_TABLE_BASE 1024
#define WORD_BUFFER_BASE  4096
#define EMIT_BUFFER_BASE  8192
#define MATCH_SAVE_BASE   12288
#define STRING_DATA_BASE  16384

/* ========================================================================
 * WAT Writer Infrastructure
 * ======================================================================== */

typedef struct {
    FILE *file;
    const uint8_t left, right, newline, blank;
    size_t depth;
    void (*c) (char c);
    void (*s) (const char *s, size_t n);
    void (*str) (const char *s);
    void (*l) (void);
    void (*r) (void);
    void (*space) (void);
    void (*newline_fn) (void);
    void (*indent) (void);
    void (*increase_indent) (void);
    void (*decrease_indent) (void);
} wat;

wat Wat = {
    .left    = '(',
    .right   = ')',
    .blank   = ' ',
    .newline = '\n',
};

/* Convenience: write an indented line */
#define WL(s) do { Wat.indent(); Wat.str(s); Wat.newline_fn(); } while(0)
#define WI() Wat.increase_indent()
#define WD() Wat.decrease_indent()
#define WN() Wat.newline_fn()

void write_left(void) { fwrite(&Wat.left, 1, 1, Wat.file); }
void write_right(void) { fwrite(&Wat.right, 1, 1, Wat.file); }
void write_space(void) { fwrite(&Wat.blank, 1, 1, Wat.file); }
void write_newline(void) { fwrite(&Wat.newline, 1, 1, Wat.file); }

void write_character(char c) { fwrite(&c, 1, 1, Wat.file); }

void write_string(const char *s, size_t n) { fwrite(s, 1, n, Wat.file); }

void write_str(const char *s) { fwrite(s, 1, strlen(s), Wat.file); }

void write_indent(void) {
    for (size_t i = 0; i < Wat.depth * 2; ++i)
        fwrite(&Wat.blank, 1, 1, Wat.file);
}

void increase_indent(void) { Wat.depth += 1; }

void decrease_indent(void) {
    if (Wat.depth > 0) Wat.depth -= 1;
}

void init_wat(void) {
    if (Wat.l == NULL) {
        Wat.c = write_character;
        Wat.s = write_string;
        Wat.str = write_str;
        Wat.l = write_left;
        Wat.r = write_right;
        Wat.space = write_space;
        Wat.newline_fn = write_newline;
        Wat.indent = write_indent;
        Wat.increase_indent = increase_indent;
        Wat.decrease_indent = decrease_indent;
    }
}

/* Forward declarations */
extern program_context *context_root(void);

/* ========================================================================
 * Global Letter Table (compile-time data structure)
 * ======================================================================== */

#define MAX_LETTERS 256

typedef struct {
    int count;
    struct {
        const uint8_t *bytes;
        size_t len;
    } entries[MAX_LETTERS];
} global_letter_table;

static global_letter_table GlobalLetters = { 0 };

/* Abstract-to-concrete index mapping for bound abstract algorithms.
 * When non-NULL, decompose_to_indices uses this to map abstract letter
 * names (a, b, c, ...) to concrete GlobalLetters indices. */
static int *AbstractBindMap = NULL;
static size_t AbstractBindMapSize = 0;
static abstract_alphabet *AbstractAlph = NULL;

/* String constant table for emit expressions (algorithm names, rule names, etc.) */
#define MAX_STRING_CONSTANTS 64
typedef struct {
    int count;
    size_t next_offset;   /* next available offset relative to STRING_DATA_BASE */
    struct {
        const uint8_t *bytes;
        size_t len;
        size_t wasm_offset;  /* absolute offset in WASM memory */
    } entries[MAX_STRING_CONSTANTS];
} string_constant_table;

static string_constant_table StringConstants = { 0 };

/* Register a string constant, returns its index. Deduplicates. */
static int register_string_constant(const uint8_t *bytes, size_t len) {
    /* Check for existing */
    for (int i = 0; i < StringConstants.count; i++) {
        if (StringConstants.entries[i].len == len &&
            memcmp(StringConstants.entries[i].bytes, bytes, len) == 0) {
            return i;
        }
    }
    if (StringConstants.count >= MAX_STRING_CONSTANTS) return -1;
    int idx = StringConstants.count++;
    StringConstants.entries[idx].bytes = bytes;
    StringConstants.entries[idx].len = len;
    StringConstants.entries[idx].wasm_offset = STRING_DATA_BASE + StringConstants.next_offset;
    StringConstants.next_offset += len;
    return idx;
}

/* Check if any algorithm in the program has emit rules */
static bool program_has_emit_rules(void) {
    program_context *ctx = context_root();
    if (ctx == NULL) return false;
    /* Walk all contexts recursively via a simple stack */
    program_context *stack[64];
    int sp = 0;
    stack[sp++] = ctx;
    while (sp > 0) {
        program_context *c = stack[--sp];
        for (size_t i = 0; i < c->algorithms_count; i++) {
            for (size_t j = 0; j < c->algorithms[i]->rules_count; j++) {
                if (c->algorithms[i]->rules[j]->has_emit) return true;
            }
        }
        for (size_t i = 0; i < c->content_count && sp < 64; i++) {
            stack[sp++] = c->content[i];
        }
    }
    return false;
}

/* Check if a specific algorithm has any emit rules */
static bool algorithm_has_emit_rules(algorithm_definition *alg) {
    for (size_t i = 0; i < alg->rules_count; i++) {
        if (alg->rules[i]->has_emit) return true;
    }
    return false;
}

/* Build the global letter table from the Data module's null-separated blob. */
static void build_global_letter_table(struct data *Data) {
    uint8_t *data = Data->letters_data();
    size_t total = Data->letters_count();
    size_t i = 0;
    size_t found = 0;

    GlobalLetters.count = 0;

    if (total == 0 || data == NULL) return;

    bool at_start = true;
    while (found < total && GlobalLetters.count < MAX_LETTERS) {
        if (at_start) {
            GlobalLetters.entries[GlobalLetters.count].bytes = &data[i];
            at_start = false;
        } else if (data[i] == 0x0) {
            GlobalLetters.entries[GlobalLetters.count].len =
                &data[i] - GlobalLetters.entries[GlobalLetters.count].bytes;
            GlobalLetters.count++;
            found++;
            at_start = true;
        }
        i++;
    }
}

/* ========================================================================
 * Pattern Decomposition (compile-time)
 * ======================================================================== */

/*
 * Decompose raw bytes into a sequence of letter indices using greedy
 * longest-match against the global letter table.
 *
 * Returns number of letters found, or -1 on failure.
 */
static int decompose_to_indices(
    const uint8_t *bytes,
    size_t byte_len,
    int *indices,
    int max_indices)
{
    int count = 0;
    size_t pos = 0;

    while (pos < byte_len && count < max_indices) {
        int best_index = -1;
        size_t best_len = 0;

        /* Check abstract bind map first: match abstract letter names
         * (single or multi-char) and map to concrete indices */
        if (AbstractBindMap != NULL && AbstractAlph != NULL) {
            for (size_t ai = 0; ai < AbstractAlph->size; ai++) {
                size_t nlen = AbstractAlph->names[ai].len;
                if (nlen > best_len && pos + nlen <= byte_len &&
                    memcmp(&bytes[pos], AbstractAlph->names[ai].bytes, nlen) == 0) {
                    best_index = AbstractBindMap[ai];
                    best_len = nlen;
                }
            }
        }

        /* Fall back to concrete letter matching */
        if (best_index < 0) {
            for (int i = 0; i < GlobalLetters.count; ++i) {
                size_t llen = GlobalLetters.entries[i].len;
                if (llen > best_len && pos + llen <= byte_len &&
                    memcmp(&bytes[pos], GlobalLetters.entries[i].bytes, llen) == 0) {
                    best_index = i;
                    best_len = llen;
                }
            }
        }

        if (best_index < 0) return -1;
        indices[count++] = best_index;
        pos += best_len;
    }

    return (pos == byte_len) ? count : -1;
}

/*
 * Convert a pattern AST node into a sequence of letter indices.
 * Handles both single-token and multi-token patterns.
 *
 * pattern->token_index = first IDENTIFIER token
 * pattern->size = count of ADDITIONAL IDENTIFIER tokens (0 = single token)
 */
static int pattern_to_indices(
    syntax_store *pattern,
    int *indices,
    int max_indices)
{
    int total = 0;
    size_t tok_idx = pattern->token_index;
    size_t tokens_needed = pattern->size + 1;
    size_t tokens_found = 0;

    while (tokens_found < tokens_needed) {
        lexical_store *tok = Lex.store(tok_idx);
        if (tok == NULL) return -1;

        if (tok->token == TOKEN_IDENTIFIER) {
            int n = decompose_to_indices(
                tok->begin, tok->end - tok->begin,
                &indices[total], max_indices - total);
            if (n < 0) return -1;
            total += n;
            tokens_found++;
        }
        tok_idx++;
    }

    return total;
}

/* ========================================================================
 * Forward declarations
 * ======================================================================== */

void wasm_write_memory(void);
void wasm_write_letter_data(struct data *Data);
void wasm_write_helper_functions(void);
void wasm_write_algorithms(void);
void wasm_write_algorithm(algorithm_definition *alg, size_t index,
                          alphabet_bind *bind_override);
void wasm_write_algorithm_step(algorithm_definition *alg, size_t index,
                               alphabet_bind *bind_override);
void wasm_write_rule(algorithm_definition *alg, algorithm_rule *rule, size_t rule_num);
void wasm_write_context_algorithms(program_context *ctx);

/* ========================================================================
 * Memory declaration
 * ======================================================================== */

void wasm_write_memory(void) {
    WN();
    WL("(memory (export \"memory\") 1)");
}

/* ========================================================================
 * Letter data section
 * ======================================================================== */

void wasm_write_letter_data(struct data *Data) {
    char buf[64];

    WN();
    WL(";; Letter string data (null-separated)");

    /* Write raw letter bytes to data section */
    Wat.indent();
    snprintf(buf, sizeof(buf), "(data (i32.const %d) \"", LETTER_DATA_BASE);
    Wat.str(buf);

    size_t offset = 0;
    size_t *offsets = calloc(GlobalLetters.count, sizeof(size_t));
    size_t *lengths = calloc(GlobalLetters.count, sizeof(size_t));

    for (int i = 0; i < GlobalLetters.count; ++i) {
        offsets[i] = offset;
        lengths[i] = GlobalLetters.entries[i].len;

        for (size_t k = 0; k < lengths[i]; ++k) {
            snprintf(buf, sizeof(buf), "\\%02x", GlobalLetters.entries[i].bytes[k]);
            Wat.str(buf);
        }
        Wat.str("\\00");
        offset += lengths[i] + 1;
    }
    Wat.str("\")");
    WN();

    /* Write offset table: pairs of (i32 offset, i32 length) */
    WN();
    WL(";; Letter offset table (offset, length pairs as i32 LE)");
    Wat.indent();
    snprintf(buf, sizeof(buf), "(data (i32.const %d) \"", LETTER_TABLE_BASE);
    Wat.str(buf);

    for (int i = 0; i < GlobalLetters.count; ++i) {
        uint32_t off = (uint32_t)(LETTER_DATA_BASE + offsets[i]);
        uint32_t len = (uint32_t)lengths[i];
        /* Little-endian i32 for offset */
        for (int b = 0; b < 4; ++b) {
            snprintf(buf, sizeof(buf), "\\%02x", (off >> (b * 8)) & 0xFF);
            Wat.str(buf);
        }
        /* Little-endian i32 for length */
        for (int b = 0; b < 4; ++b) {
            snprintf(buf, sizeof(buf), "\\%02x", (len >> (b * 8)) & 0xFF);
            Wat.str(buf);
        }
    }
    Wat.str("\")");
    WN();

    free(offsets);
    free(lengths);
}

/* ========================================================================
 * Helper functions ($memmove, $encode_word, $decode_word)
 * ======================================================================== */

static void wasm_write_memmove(void) {
    WN();
    WL(";; Bidirectional memory move (handles overlapping regions)");
    WL("(func $memmove (param $dest i32) (param $src i32) (param $len i32)");
    WI();
    WL("(local $i i32)");

    /* Early return if no-op */
    WL("(if (i32.or (i32.eqz (local.get $len)) (i32.eq (local.get $dest) (local.get $src))) (then (return)))");

    /* Forward copy when dest < src */
    WL("(if (i32.lt_u (local.get $dest) (local.get $src))");
    WI();
    WL("(then");
    WI();
    WL("(local.set $i (i32.const 0))");
    WL("(block $done (loop $copy");
    WI();
    WL("(br_if $done (i32.ge_u (local.get $i) (local.get $len)))");
    WL("(i32.store8 (i32.add (local.get $dest) (local.get $i)) (i32.load8_u (i32.add (local.get $src) (local.get $i))))");
    WL("(local.set $i (i32.add (local.get $i) (i32.const 1)))");
    WL("(br $copy)");
    WD();
    WL("))");
    WD();
    WL(")");

    /* Backward copy when dest > src */
    WL("(else");
    WI();
    WL("(local.set $i (local.get $len))");
    WL("(block $done (loop $copy");
    WI();
    WL("(br_if $done (i32.eqz (local.get $i)))");
    WL("(local.set $i (i32.sub (local.get $i) (i32.const 1)))");
    WL("(i32.store8 (i32.add (local.get $dest) (local.get $i)) (i32.load8_u (i32.add (local.get $src) (local.get $i))))");
    WL("(br $copy)");
    WD();
    WL("))");
    WD();
    WL(")");

    WD();
    WL(")"); /* end if */
    WD();
    WL(")"); /* end func */
}

static void wasm_write_encode_word(void) {
    char buf[256];

    WN();
    WL(";; Encode raw bytes -> letter indices in word buffer");
    WL(";; Returns number of letter indices written");
    WL("(func $encode_word (export \"encode_word\")");
    WI();
    WL("(param $src i32) (param $src_len i32) (result i32)");
    WL("(local $src_pos i32)");
    WL("(local $dst_pos i32)");
    WL("(local $i i32)");
    WL("(local $letter_off i32)");
    WL("(local $letter_len i32)");
    WL("(local $j i32)");
    WL("(local $matched i32)");
    WL("(local $best i32)");
    WL("(local $best_len i32)");

    WN();
    snprintf(buf, sizeof(buf),
        "(local.set $dst_pos (i32.const %d))", WORD_BUFFER_BASE);
    WL(buf);
    WL("(local.set $src_pos (i32.const 0))");

    WN();
    WL("(block $done");
    WI();
    WL("(loop $next_pos");
    WI();
    WL("(br_if $done (i32.ge_u (local.get $src_pos) (local.get $src_len)))");

    WN();
    WL(";; Find longest matching letter at current position");
    WL("(local.set $best (i32.const -1))");
    WL("(local.set $best_len (i32.const 0))");
    WL("(local.set $i (i32.const 0))");

    WN();
    WL("(block $found_best");
    WI();
    WL("(loop $try_letter");
    WI();
    snprintf(buf, sizeof(buf),
        "(br_if $found_best (i32.ge_u (local.get $i) (i32.const %d)))",
        GlobalLetters.count);
    WL(buf);

    WN();
    WL(";; Load letter offset and length from table");
    snprintf(buf, sizeof(buf),
        "(local.set $letter_off (i32.load (i32.add (i32.const %d) (i32.mul (local.get $i) (i32.const 8)))))",
        LETTER_TABLE_BASE);
    WL(buf);
    snprintf(buf, sizeof(buf),
        "(local.set $letter_len (i32.load (i32.add (i32.add (i32.const %d) (i32.mul (local.get $i) (i32.const 8))) (i32.const 4))))",
        LETTER_TABLE_BASE);
    WL(buf);

    WN();
    WL(";; Try this letter if it's longer than current best and fits");
    WL("(if (i32.and");
    WI();
    WL("(i32.gt_u (local.get $letter_len) (local.get $best_len))");
    WL("(i32.le_u (i32.add (local.get $src_pos) (local.get $letter_len)) (local.get $src_len)))");
    WD();
    WL("(then");
    WI();

    WL(";; Compare bytes");
    WL("(local.set $j (i32.const 0))");
    WL("(local.set $matched (i32.const 1))");
    WL("(block $mismatch");
    WI();
    WL("(loop $cmp");
    WI();
    WL("(br_if $mismatch (i32.ge_u (local.get $j) (local.get $letter_len)))");
    WL("(if (i32.ne");
    WI();
    WL("(i32.load8_u (i32.add (local.get $src) (i32.add (local.get $src_pos) (local.get $j))))");
    WL("(i32.load8_u (i32.add (local.get $letter_off) (local.get $j))))");
    WD();
    WL("(then (local.set $matched (i32.const 0)) (br $mismatch))");
    WL(")");
    WL("(local.set $j (i32.add (local.get $j) (i32.const 1)))");
    WL("(br $cmp)");
    WD();
    WL(")"); /* end loop $cmp */
    WD();
    WL(")"); /* end block $mismatch */

    WN();
    WL("(if (local.get $matched)");
    WI();
    WL("(then");
    WI();
    WL("(local.set $best (local.get $i))");
    WL("(local.set $best_len (local.get $letter_len))");
    WD();
    WL(")");
    WD();
    WL(")");

    WD();
    WL(")"); /* end then (try letter) */
    WL(")"); /* end if (try letter) */

    WN();
    WL("(local.set $i (i32.add (local.get $i) (i32.const 1)))");
    WL("(br $try_letter)");
    WD();
    WL(")"); /* end loop $try_letter */
    WD();
    WL(")"); /* end block $found_best */

    WN();
    WL(";; Write best letter index and advance");
    WL("(i32.store8 (local.get $dst_pos) (local.get $best))");
    WL("(local.set $dst_pos (i32.add (local.get $dst_pos) (i32.const 1)))");
    WL("(local.set $src_pos (i32.add (local.get $src_pos) (local.get $best_len)))");
    WL("(br $next_pos)");

    WD();
    WL(")"); /* end loop $next_pos */
    WD();
    WL(")"); /* end block $done */

    WN();
    snprintf(buf, sizeof(buf),
        "(i32.sub (local.get $dst_pos) (i32.const %d))", WORD_BUFFER_BASE);
    WL(buf);
    WD();
    WL(")"); /* end func */
}

static void wasm_write_decode_word(void) {
    char buf[256];

    WN();
    WL(";; Decode letter indices -> raw bytes");
    WL(";; Returns number of bytes written");
    WL("(func $decode_word (export \"decode_word\")");
    WI();
    WL("(param $idx_ptr i32) (param $idx_len i32) (param $out_ptr i32) (result i32)");
    WL("(local $i i32)");
    WL("(local $out_pos i32)");
    WL("(local $letter_idx i32)");
    WL("(local $letter_off i32)");
    WL("(local $letter_len i32)");
    WL("(local $j i32)");

    WN();
    WL("(local.set $i (i32.const 0))");
    WL("(local.set $out_pos (i32.const 0))");

    WN();
    WL("(block $done");
    WI();
    WL("(loop $next");
    WI();
    WL("(br_if $done (i32.ge_u (local.get $i) (local.get $idx_len)))");

    WN();
    WL("(local.set $letter_idx (i32.load8_u (i32.add (local.get $idx_ptr) (local.get $i))))");
    snprintf(buf, sizeof(buf),
        "(local.set $letter_off (i32.load (i32.add (i32.const %d) (i32.mul (local.get $letter_idx) (i32.const 8)))))",
        LETTER_TABLE_BASE);
    WL(buf);
    snprintf(buf, sizeof(buf),
        "(local.set $letter_len (i32.load (i32.add (i32.add (i32.const %d) (i32.mul (local.get $letter_idx) (i32.const 8))) (i32.const 4))))",
        LETTER_TABLE_BASE);
    WL(buf);

    WN();
    WL(";; Copy letter bytes to output");
    WL("(local.set $j (i32.const 0))");
    WL("(block $copied");
    WI();
    WL("(loop $copy");
    WI();
    WL("(br_if $copied (i32.ge_u (local.get $j) (local.get $letter_len)))");
    WL("(i32.store8");
    WI();
    WL("(i32.add (local.get $out_ptr) (i32.add (local.get $out_pos) (local.get $j)))");
    WL("(i32.load8_u (i32.add (local.get $letter_off) (local.get $j)))");
    WD();
    WL(")");
    WL("(local.set $j (i32.add (local.get $j) (i32.const 1)))");
    WL("(br $copy)");
    WD();
    WL(")"); /* end loop $copy */
    WD();
    WL(")"); /* end block $copied */

    WN();
    WL("(local.set $out_pos (i32.add (local.get $out_pos) (local.get $letter_len)))");
    WL("(local.set $i (i32.add (local.get $i) (i32.const 1)))");
    WL("(br $next)");
    WD();
    WL(")"); /* end loop $next */
    WD();
    WL(")"); /* end block $done */

    WN();
    WL("(local.get $out_pos)");
    WD();
    WL(")"); /* end func */
}

void wasm_write_helper_functions(void) {
    WN();
    WL(";; ========================================");
    WL(";; Helper Functions");
    WL(";; ========================================");

    wasm_write_memmove();
    wasm_write_encode_word();
    wasm_write_decode_word();
}

/* ========================================================================
 * Emit codegen
 * ======================================================================== */

/* Interpolation variable types */
typedef enum {
    EMIT_LIT,    /* literal text bytes */
    EMIT_WORD,   /* ~ or ~word: full word after replacement */
    EMIT_WAS,    /* ~0 or ~was: full word before replacement */
    EMIT_LEFT,   /* ~p or ~prefix: prefix before match */
    EMIT_POST,   /* ~s or ~suffix: suffix after replacement */
    EMIT_MATCH,  /* ~m or ~match: matched pattern text */
    EMIT_SUB,    /* ~r or ~sub: replacement text */
    EMIT_ALG,    /* ~a or ~alg: algorithm name */
    EMIT_NAME,   /* ~n or ~name: rule name */
} emit_segment_type;

typedef struct {
    emit_segment_type type;
    const uint8_t *bytes;  /* for EMIT_LIT only */
    size_t len;            /* for EMIT_LIT only */
} emit_segment;

#define MAX_EMIT_SEGMENTS 64

/* Parse an emit template string (between quotes) into segments.
 * Returns number of segments, or -1 on error. */
static int parse_emit_template(
    const uint8_t *str, size_t str_len,
    emit_segment *segments, int max_segments)
{
    int count = 0;
    size_t pos = 0;
    size_t lit_start = 0;
    bool in_lit = false;

    while (pos < str_len && count < max_segments) {
        if (str[pos] == '~') {
            /* Flush accumulated literal */
            if (in_lit && pos > lit_start) {
                segments[count].type = EMIT_LIT;
                segments[count].bytes = &str[lit_start];
                segments[count].len = pos - lit_start;
                count++;
                in_lit = false;
                if (count >= max_segments) return count;
            }

            pos++;
            if (pos >= str_len) {
                /* Bare ~ at end = ~word */
                segments[count].type = EMIT_WORD;
                count++;
                break;
            }

            /* Check what follows ~ */
            emit_segment_type var_type = EMIT_WORD;
            size_t advance = 0;

            /* Long forms first (longest first for disambiguation) */
            if (pos + 5 < str_len && memcmp(&str[pos], "prefix", 6) == 0) {
                var_type = EMIT_LEFT; advance = 6;
            } else if (pos + 5 < str_len && memcmp(&str[pos], "suffix", 6) == 0) {
                var_type = EMIT_POST; advance = 6;
            } else if (pos + 3 < str_len && memcmp(&str[pos], "word", 4) == 0) {
                var_type = EMIT_WORD; advance = 4;
            } else if (pos + 2 < str_len && memcmp(&str[pos], "was", 3) == 0) {
                var_type = EMIT_WAS; advance = 3;
            } else if (pos + 4 < str_len && memcmp(&str[pos], "match", 5) == 0) {
                var_type = EMIT_MATCH; advance = 5;
            } else if (pos + 2 < str_len && memcmp(&str[pos], "sub", 3) == 0) {
                var_type = EMIT_SUB; advance = 3;
            } else if (pos + 2 < str_len && memcmp(&str[pos], "alg", 3) == 0) {
                var_type = EMIT_ALG; advance = 3;
            } else if (pos + 3 < str_len && memcmp(&str[pos], "name", 4) == 0) {
                var_type = EMIT_NAME; advance = 4;
            /* Short forms */
            } else if (str[pos] == '0') {
                var_type = EMIT_WAS; advance = 1;
            } else if (str[pos] == 'p') {
                var_type = EMIT_LEFT; advance = 1;    /* ~p = prefix */
            } else if (str[pos] == 's') {
                var_type = EMIT_POST; advance = 1;    /* ~s = suffix */
            } else if (str[pos] == 'r') {
                var_type = EMIT_SUB; advance = 1;     /* ~r = replacement (sub) */
            } else if (str[pos] == 'm') {
                var_type = EMIT_MATCH; advance = 1;
            } else if (str[pos] == 'a') {
                var_type = EMIT_ALG; advance = 1;
            } else if (str[pos] == 'n') {
                var_type = EMIT_NAME; advance = 1;
            } else {
                /* Bare ~ not followed by known var = ~word */
                var_type = EMIT_WORD; advance = 0;
            }

            segments[count].type = var_type;
            segments[count].bytes = NULL;
            segments[count].len = 0;
            count++;
            pos += advance;
            lit_start = pos;
            in_lit = false;
        } else {
            if (!in_lit) {
                lit_start = pos;
                in_lit = true;
            }
            pos++;
        }
    }

    /* Flush trailing literal */
    if (in_lit && pos > lit_start && count < max_segments) {
        segments[count].type = EMIT_LIT;
        segments[count].bytes = &str[lit_start];
        segments[count].len = pos - lit_start;
        count++;
    }

    return count;
}

/* Generate WASM code to write a literal byte sequence to emit buffer.
 * Advances $emit_ptr. */
static void wasm_emit_literal(const uint8_t *bytes, size_t len) {
    char buf[256];
    for (size_t i = 0; i < len; i++) {
        snprintf(buf, sizeof(buf),
            "(i32.store8 (local.get $emit_ptr) (i32.const %d))",
            bytes[i]);
        WL(buf);
        WL("(local.set $emit_ptr (i32.add (local.get $emit_ptr) (i32.const 1)))");
    }
}

/* Generate WASM code to copy a string constant to emit buffer.
 * The string is at a known offset in WASM memory. Advances $emit_ptr. */
static void wasm_emit_string_constant(int str_idx) {
    char buf[256];
    size_t offset = StringConstants.entries[str_idx].wasm_offset;
    size_t len = StringConstants.entries[str_idx].len;
    snprintf(buf, sizeof(buf),
        "(call $memmove (local.get $emit_ptr) (i32.const %zu) (i32.const %zu))",
        offset, len);
    WL(buf);
    snprintf(buf, sizeof(buf),
        "(local.set $emit_ptr (i32.add (local.get $emit_ptr) (i32.const %zu)))", len);
    WL(buf);
}

/* Generate WASM code to decode a word slice (letter indices -> raw bytes)
 * into the emit buffer. Advances $emit_ptr by the decoded byte count.
 *
 * src_expr: WAT expression for the start pointer of the letter index slice
 * len_expr: WAT expression for the number of letter indices */
static void wasm_emit_decode_slice(const char *src_expr, const char *len_expr) {
    char buf[1024];
    /* Call $decode_word(src, len, emit_ptr) -> bytes written */
    snprintf(buf, sizeof(buf),
        "(local.set $emit_ptr (i32.add (local.get $emit_ptr) (call $decode_word %s %s (local.get $emit_ptr))))",
        src_expr, len_expr);
    WL(buf);
}

/* Generate the emit code for a rule that has has_emit=true.
 * Called inside the "then" branch after substitution/terminal handling.
 * Assumes $i = match position, $word_ptr, $word_len = post-substitution state.
 * $pre_word_len = word length before substitution (saved earlier).
 * Match region saved at MATCH_SAVE_BASE (pat_count bytes). */
static void wasm_write_emit_code(
    algorithm_definition *alg,
    algorithm_rule *rule,
    int pat_count,
    int repl_count)
{
    char buf[512];

    /* Determine the emit template */
    emit_segment segments[MAX_EMIT_SEGMENTS];
    int seg_count;

    if (rule->emit_string != NULL) {
        /* Explicit emit string — strip quotes */
        const uint8_t *str = rule->emit_string->begin + 1; /* skip opening quote */
        size_t str_len = (rule->emit_string->end - rule->emit_string->begin) - 2;
        seg_count = parse_emit_template(str, str_len, segments, MAX_EMIT_SEGMENTS);
    } else {
        /* Default: emit ~word (full word after replacement) */
        segments[0].type = EMIT_WORD;
        segments[0].bytes = NULL;
        segments[0].len = 0;
        seg_count = 1;
    }

    if (seg_count <= 0) return;

    WN();
    WL(";; Emit output");
    snprintf(buf, sizeof(buf), "(local.set $emit_ptr (i32.const %d))", EMIT_BUFFER_BASE);
    WL(buf);

    for (int s = 0; s < seg_count; s++) {
        switch (segments[s].type) {
        case EMIT_LIT:
            wasm_emit_literal(segments[s].bytes, segments[s].len);
            break;

        case EMIT_WORD:
            /* Full word after replacement: decode from word_ptr, word_len */
            wasm_emit_decode_slice(
                "(local.get $word_ptr)",
                "(local.get $word_len)");
            break;

        case EMIT_WAS:
            /* Full word before replacement: decode from word_ptr, pre_word_len.
             * NOTE: the word buffer has been modified, so for a fully correct ~was
             * we'd need to save the entire pre-substitution word. For now, decode
             * from MATCH_SAVE_BASE for the match portion and reconstruct.
             * Simpler approximation: decode left + saved_match + right(original). */
            WL(";; ~was: left + saved match + original right");
            /* Left portion (unchanged): word_ptr to word_ptr+$i */
            wasm_emit_decode_slice(
                "(local.get $word_ptr)",
                "(local.get $match_start)");
            /* Saved match: MATCH_SAVE_BASE, pat_count */
            snprintf(buf, sizeof(buf),
                "(local.set $emit_ptr (i32.add (local.get $emit_ptr) (call $decode_word (i32.const %d) (i32.const %d) (local.get $emit_ptr))))",
                MATCH_SAVE_BASE, pat_count);
            WL(buf);
            /* Right portion (after match, using post-sub buffer):
             * word_ptr + match_start + repl_count, word_len - match_start - repl_count
             * But we need original right = pre_word_len - match_start - pat_count indices
             * starting at word_ptr + match_start + repl_count in the post-sub buffer.
             * Since memmove preserved the tail, this is correct. */
            snprintf(buf, sizeof(buf),
                "(i32.add (local.get $word_ptr) (i32.add (local.get $match_start) (i32.const %d)))",
                repl_count);
            {
                char len_buf[256];
                snprintf(len_buf, sizeof(len_buf),
                    "(i32.sub (local.get $pre_word_len) (i32.add (local.get $match_start) (i32.const %d)))",
                    pat_count);
                wasm_emit_decode_slice(buf, len_buf);
            }
            break;

        case EMIT_LEFT:
            /* Prefix: word_ptr, $match_start indices */
            wasm_emit_decode_slice(
                "(local.get $word_ptr)",
                "(local.get $match_start)");
            break;

        case EMIT_POST:
            /* Postfix after replacement: word_ptr + match_start + repl_count */
            snprintf(buf, sizeof(buf),
                "(i32.add (local.get $word_ptr) (i32.add (local.get $match_start) (i32.const %d)))",
                repl_count);
            {
                char len_buf[256];
                snprintf(len_buf, sizeof(len_buf),
                    "(i32.sub (local.get $word_len) (i32.add (local.get $match_start) (i32.const %d)))",
                    repl_count);
                wasm_emit_decode_slice(buf, len_buf);
            }
            break;

        case EMIT_MATCH:
            /* Saved match at MATCH_SAVE_BASE, pat_count bytes */
            snprintf(buf, sizeof(buf),
                "(i32.const %d)", MATCH_SAVE_BASE);
            {
                char len_buf[32];
                snprintf(len_buf, sizeof(len_buf), "(i32.const %d)", pat_count);
                wasm_emit_decode_slice(buf, len_buf);
            }
            break;

        case EMIT_SUB:
            /* Replacement text: word_ptr + match_start, repl_count */
            snprintf(buf, sizeof(buf),
                "(i32.add (local.get $word_ptr) (local.get $match_start))");
            {
                char len_buf[32];
                snprintf(len_buf, sizeof(len_buf), "(i32.const %d)", repl_count);
                wasm_emit_decode_slice(buf, len_buf);
            }
            break;

        case EMIT_ALG: {
            int idx = register_string_constant(alg->name->begin,
                (size_t)(alg->name->end - alg->name->begin));
            if (idx >= 0) wasm_emit_string_constant(idx);
            break;
        }

        case EMIT_NAME: {
            if (rule->rule_name != NULL) {
                int idx = register_string_constant(rule->rule_name->begin,
                    (size_t)(rule->rule_name->end - rule->rule_name->begin));
                if (idx >= 0) wasm_emit_string_constant(idx);
            }
            break;
        }
        }
    }

    /* Call $emit(EMIT_BUFFER_BASE, length) */
    snprintf(buf, sizeof(buf),
        "(call $emit (i32.const %d) (i32.sub (local.get $emit_ptr) (i32.const %d)))",
        EMIT_BUFFER_BASE, EMIT_BUFFER_BASE);
    WL(buf);
}

/* ========================================================================
 * Algorithm codegen
 * ======================================================================== */

/* Write pattern matching and substitution code for a single rule,
 * using letter-index comparisons instead of raw byte comparisons. */
void wasm_write_rule(algorithm_definition *alg, algorithm_rule *rule, size_t rule_num) {
    if (rule == NULL || rule->pattern == NULL) return;

    /* Decompose pattern into letter indices */
    int pat_indices[256];
    int pat_count = pattern_to_indices(rule->pattern, pat_indices, 256);
    if (pat_count < 0) {
        fprintf(stderr, "Error: failed to decompose pattern into letter indices\n");
        return;
    }

    /* Decompose replacement into letter indices (if not terminal) */
    int repl_indices[256];
    int repl_count = 0;
    if (!rule->is_terminal && rule->replacement != NULL) {
        repl_count = pattern_to_indices(rule->replacement, repl_indices, 256);
        if (repl_count < 0) {
            fprintf(stderr, "Error: failed to decompose replacement into letter indices\n");
            return;
        }
    }

    char buf[512];
    WN();

    /* Comment: show the rule with letter indices */
    Wat.indent();
    snprintf(buf, sizeof(buf), ";; Rule %zu: [", rule_num);
    Wat.str(buf);
    for (int c = 0; c < pat_count; ++c) {
        snprintf(buf, sizeof(buf), "%s%d", c > 0 ? "," : "", pat_indices[c]);
        Wat.str(buf);
    }
    Wat.str("]");
    if (rule->is_terminal) {
        Wat.str(" -> HALT");
    } else {
        Wat.str(" -> [");
        for (int c = 0; c < repl_count; ++c) {
            snprintf(buf, sizeof(buf), "%s%d", c > 0 ? "," : "", repl_indices[c]);
            Wat.str(buf);
        }
        Wat.str("]");
    }
    WN();

    /* Also show the human-readable form */
    Wat.indent();
    Wat.str(";; ");
    /* Pattern letters */
    for (int c = 0; c < pat_count; ++c) {
        int idx = pat_indices[c];
        if (idx >= 0 && idx < GlobalLetters.count) {
            Wat.s((const char *)GlobalLetters.entries[idx].bytes,
                  GlobalLetters.entries[idx].len);
        }
    }
    if (rule->is_terminal) {
        Wat.str(" -.");
    } else {
        Wat.str(" -> ");
        for (int c = 0; c < repl_count; ++c) {
            int idx = repl_indices[c];
            if (idx >= 0 && idx < GlobalLetters.count) {
                Wat.s((const char *)GlobalLetters.entries[idx].bytes,
                      GlobalLetters.entries[idx].len);
            }
        }
    }
    WN();

    /* Reset scan position */
    WL("(local.set $i (i32.const 0))");

    /* Block/loop for scanning */
    snprintf(buf, sizeof(buf), "(block $rule%zu_done", rule_num);
    WL(buf);
    WI();

    snprintf(buf, sizeof(buf), "(loop $rule%zu_scan", rule_num);
    WL(buf);
    WI();

    /* Bounds check: i + pat_count <= word_len */
    snprintf(buf, sizeof(buf),
        "(br_if $rule%zu_done (i32.gt_s (i32.add (local.get $i) (i32.const %d)) (local.get $word_len)))",
        rule_num, pat_count);
    WL(buf);

    /* Match condition */
    Wat.indent();
    Wat.str("(if");
    WN();
    WI();

    /* Build AND chain of letter-index comparisons */
    Wat.indent();
    if (pat_count > 1) {
        Wat.str("(i32.and");
        WN();
        WI();
    }

    for (int c = 0; c < pat_count; ++c) {
        if (c > 0 && c < pat_count - 1) {
            Wat.indent();
            Wat.str("(i32.and");
            WN();
            WI();
        }
        Wat.indent();
        snprintf(buf, sizeof(buf),
            "(i32.eq (i32.load8_u (i32.add (local.get $word_ptr) (i32.add (local.get $i) (i32.const %d)))) (i32.const %d))",
            c, pat_indices[c]);
        Wat.str(buf);
        WN();
    }

    /* Close nested i32.and expressions */
    for (int c = 1; c < pat_count; ++c) {
        WD();
        Wat.indent();
        Wat.str(")");
        WN();
    }

    /* Then branch: pattern matched */
    WL("(then");
    WI();

    /* Save pre-substitution state for emit */
    if (rule->has_emit) {
        WL(";; Save pre-substitution state for emit");
        WL("(local.set $match_start (local.get $i))");
        WL("(local.set $pre_word_len (local.get $word_len))");
        /* Copy matched letter indices to MATCH_SAVE_BASE */
        for (int c = 0; c < pat_count; ++c) {
            snprintf(buf, sizeof(buf),
                "(i32.store8 (i32.const %d) (i32.load8_u (i32.add (local.get $word_ptr) (i32.add (local.get $i) (i32.const %d)))))",
                MATCH_SAVE_BASE + c, c);
            WL(buf);
        }
    }

    if (rule->is_terminal) {
        WL("(local.set $terminated (i32.const 1))");
        WL("(local.set $matched (i32.const 1))");

        if (rule->has_emit) {
            wasm_write_emit_code(alg, rule, pat_count, 0);
        }

        snprintf(buf, sizeof(buf), "(br $rule%zu_done)", rule_num);
        WL(buf);
    } else {
        WL(";; Matched! Perform substitution");

        int len_diff = repl_count - pat_count;

        /* For growing substitutions, shift tail RIGHT first (before writing) */
        if (len_diff != 0) {
            WL(";; Shift remaining letters and adjust length");
            Wat.indent();
            Wat.str("(call $memmove");
            WN();
            WI();
            /* dest = word_ptr + i + repl_count */
            snprintf(buf, sizeof(buf),
                "(i32.add (local.get $word_ptr) (i32.add (local.get $i) (i32.const %d)))",
                repl_count);
            WL(buf);
            /* src = word_ptr + i + pat_count */
            snprintf(buf, sizeof(buf),
                "(i32.add (local.get $word_ptr) (i32.add (local.get $i) (i32.const %d)))",
                pat_count);
            WL(buf);
            /* len = word_len - i - pat_count */
            snprintf(buf, sizeof(buf),
                "(i32.sub (local.get $word_len) (i32.add (local.get $i) (i32.const %d))))",
                pat_count);
            WL(buf);
            WD();
        }

        /* Write replacement letter indices */
        for (int c = 0; c < repl_count; ++c) {
            snprintf(buf, sizeof(buf),
                "(i32.store8 (i32.add (local.get $word_ptr) (i32.add (local.get $i) (i32.const %d))) (i32.const %d))",
                c, repl_indices[c]);
            WL(buf);
        }

        /* Update word length */
        if (len_diff != 0) {
            snprintf(buf, sizeof(buf),
                "(local.set $word_len (i32.add (local.get $word_len) (i32.const %d)))",
                len_diff);
            WL(buf);
        }

        WL("(local.set $matched (i32.const 1))");

        if (rule->has_emit) {
            wasm_write_emit_code(alg, rule, pat_count, repl_count);
        }

        snprintf(buf, sizeof(buf), "(br $rule%zu_done)", rule_num);
        WL(buf);
    }

    WD();
    WL(")"); /* end then */
    WD();
    WL(")"); /* end if */

    /* Advance scan position by 1 letter (not 1 byte!) */
    WL("(local.set $i (i32.add (local.get $i) (i32.const 1)))");
    snprintf(buf, sizeof(buf), "(br $rule%zu_scan)", rule_num);
    WL(buf);

    WD();
    WL(")"); /* end loop */
    WD();
    WL(")"); /* end block */
}

/* Set up the abstract-to-concrete bind map for an abstract algorithm.
 * For a positional bind, maps abstract position i to GlobalLetters index i.
 * If bind_override is non-NULL, it is used directly instead of auto-selecting. */
static void setup_abstract_bind_map(algorithm_definition *alg,
                                    alphabet_bind *bind_override) {
    if (alg->abstract_alph == NULL) return;

    /* Look for a bind targeting this algorithm's abstract alphabet */
    program_context *ctx = context_root();
    if (ctx == NULL) return;

    /* For now, use positional mapping: abstract position i → GlobalLetters index i.
     * A specified bind would permute these indices. */
    size_t n = alg->abstract_alph->size;

    /* Find a bind for this algorithm's alphabet size.
     * Also handle late resolution: if a bind's source is a variable
     * matching this algorithm's name, use it. */
    alphabet_bind *bind = bind_override;
    if (bind == NULL) {
        int name_len_alg = (int)(alg->name->end - alg->name->begin);
        for (size_t i = 0; i < ctx->binds_count; i++) {
            syntax_store *src = ctx->binds[i]->source_alph;
            if (src == NULL) continue;

            /* Direct match: source is already resolved to abstract */
            if (src->type == ast_abstract_size || src->type == ast_abstract_named) {
                bind = ctx->binds[i];
                break;
            }

            /* Late resolution: source is a variable matching this algorithm's name */
            if (src->type == ast_variable) {
                lexical_store *src_tok = Lex.store(src->token_index);
                size_t src_len = src_tok->end - src_tok->begin;
                if ((int)src_len == name_len_alg &&
                    memcmp(src_tok->begin, alg->name->begin, src_len) == 0) {
                    /* This bind references our algorithm by name — use it */
                    bind = ctx->binds[i];
                    break;
                }
            }
        }
    }

    static int bind_map[MAX_ABSTRACT_LETTERS];
    if (bind != NULL && !bind->is_universal && bind->rules_count > 0) {
        /* Specified bind: use the rules to build the mapping */
        for (size_t i = 0; i < n; i++) {
            bind_map[i] = (int)i; /* default positional */
        }
        for (size_t i = 0; i < bind->rules_count; i++) {
            if (bind->rules[i].type == BIND_MAP &&
                bind->rules[i].source != NULL &&
                bind->rules[i].target != NULL) {
                /* Find which abstract position this source name matches */
                size_t src_len = bind->rules[i].source->end - bind->rules[i].source->begin;
                int abstract_pos = -1;
                for (size_t ap = 0; ap < n; ap++) {
                    if (alg->abstract_alph->names[ap].len == src_len &&
                        memcmp(alg->abstract_alph->names[ap].bytes,
                               bind->rules[i].source->begin, src_len) == 0) {
                        abstract_pos = (int)ap;
                        break;
                    }
                }
                if (abstract_pos >= 0) {
                    /* Find which GlobalLetters index the target letter is */
                    for (int g = 0; g < GlobalLetters.count; g++) {
                        size_t tgt_len = bind->rules[i].target->end - bind->rules[i].target->begin;
                        if (GlobalLetters.entries[g].len == tgt_len &&
                            memcmp(GlobalLetters.entries[g].bytes,
                                   bind->rules[i].target->begin, tgt_len) == 0) {
                            bind_map[abstract_pos] = g;
                            break;
                        }
                    }
                }
            }
        }
    } else {
        /* Universal/positional bind: position i → GlobalLetters index i */
        for (size_t i = 0; i < n; i++) {
            bind_map[i] = (int)i;
        }
    }

    AbstractBindMap = bind_map;
    AbstractBindMapSize = n;
    AbstractAlph = alg->abstract_alph;
}

static void clear_abstract_bind_map(void) {
    AbstractBindMap = NULL;
    AbstractBindMapSize = 0;
    AbstractAlph = NULL;
}

/* Write a single algorithm as a WASM function.
 * If bind_override is non-NULL, the exported name is suffixed with "$bindname"
 * and that bind's mapping is used in place of auto-selection. */
void wasm_write_algorithm(algorithm_definition *alg, size_t index,
                          alphabet_bind *bind_override) {
    if (alg == NULL || alg->name == NULL) return;

    int name_len = (int)(alg->name->end - alg->name->begin);
    const char *bind_name = NULL;
    int bind_name_len = 0;
    if (bind_override != NULL && bind_override->name != NULL) {
        bind_name = (const char *)bind_override->name->begin;
        bind_name_len = (int)(bind_override->name->end - bind_override->name->begin);
    }

    WN();
    Wat.indent();
    Wat.str(";; Algorithm: ");
    Wat.s((const char *)alg->name->begin, name_len);
    if (bind_name != NULL) {
        Wat.str(" :: ");
        Wat.s(bind_name, bind_name_len);
    }
    WN();

    /* Function signature */
    Wat.indent();
    Wat.str("(func $");
    Wat.s((const char *)alg->name->begin, name_len);
    if (bind_name != NULL) {
        Wat.str("$");
        Wat.s(bind_name, bind_name_len);
    }
    Wat.str(" (export \"");
    Wat.s((const char *)alg->name->begin, name_len);
    if (bind_name != NULL) {
        Wat.str("$");
        Wat.s(bind_name, bind_name_len);
    }
    Wat.str("\")");
    WN();

    WI();
    WL("(param $word_ptr i32)  ;; Pointer to letter-index encoded word");
    WL("(param $word_len i32)  ;; Number of letters (not bytes)");
    WL("(result i32)           ;; Returns new letter count");

    /* Local variables */
    WN();
    WL("(local $i i32)         ;; Scan position");
    WL("(local $matched i32)   ;; Flag: did we match a rule?");
    WL("(local $terminated i32) ;; Flag: hit terminal rule?");
    WL("(local $steps i32)     ;; Step counter for termination detection");

    if (algorithm_has_emit_rules(alg)) {
        WL("(local $emit_ptr i32)      ;; Emit buffer write position");
        WL("(local $match_start i32)   ;; Saved match position for emit");
        WL("(local $pre_word_len i32)  ;; Word length before substitution");
    }

    /* Set up abstract bind map if this is an abstract algorithm */
    setup_abstract_bind_map(alg, bind_override);

    /* Main Markov loop */
    WN();
    WL(";; Main loop: keep applying rules until terminated, no match, or max steps");
    WL("(block $done");
    WI();
    WL("(loop $apply_rules");
    WI();

    /* Check step limit (10000 iterations) */
    WL("(br_if $done (i32.ge_u (local.get $steps) (i32.const 10000)))");
    WL("(local.set $steps (i32.add (local.get $steps) (i32.const 1)))");

    /* Reset flags each iteration */
    WL("(local.set $matched (i32.const 0))");
    WL("(local.set $terminated (i32.const 0))");

    /* Generate code for each rule */
    for (size_t r = 0; r < alg->rules_count; ++r) {
        algorithm_rule *rule = alg->rules[r];
        if (rule == NULL) continue;

        /* After first rule, skip remaining if already matched */
        if (r > 0) {
            WN();
            WL(";; Skip if already matched");
            WL("(br_if $apply_rules (local.get $matched))");
        }

        wasm_write_rule(alg, rule, r + 1);
    }

    /* Termination conditions */
    WN();
    WL(";; Check termination conditions");
    WL("(br_if $done (local.get $terminated))");
    WL("(br_if $apply_rules (local.get $matched))");

    WD();
    WL(")  ;; end loop");
    WD();
    WL(")  ;; end block");

    /* Return letter count */
    WN();
    WL("(local.get $word_len)");

    WD();
    WL(")");

    clear_abstract_bind_map();
}

/* Write a step function for an algorithm — applies one Markov iteration.
 * If bind_override is non-NULL, the exported name is suffixed with "$bindname". */
void wasm_write_algorithm_step(algorithm_definition *alg, size_t index,
                               alphabet_bind *bind_override) {
    if (alg == NULL || alg->name == NULL) return;

    char buf[512];
    int name_len = (int)(alg->name->end - alg->name->begin);
    const char *bind_name = NULL;
    int bind_name_len = 0;
    if (bind_override != NULL && bind_override->name != NULL) {
        bind_name = (const char *)bind_override->name->begin;
        bind_name_len = (int)(bind_override->name->end - bind_override->name->begin);
    }

    WN();
    Wat.indent();
    Wat.str(";; Step function: ");
    Wat.s((const char *)alg->name->begin, name_len);
    if (bind_name != NULL) {
        Wat.str(" :: ");
        Wat.s(bind_name, bind_name_len);
    }
    WN();

    /* Function signature */
    Wat.indent();
    Wat.str("(func $");
    Wat.s((const char *)alg->name->begin, name_len);
    if (bind_name != NULL) {
        Wat.str("$");
        Wat.s(bind_name, bind_name_len);
    }
    Wat.str("_step (export \"");
    Wat.s((const char *)alg->name->begin, name_len);
    if (bind_name != NULL) {
        Wat.str("$");
        Wat.s(bind_name, bind_name_len);
    }
    Wat.str("_step\")");
    WN();

    WI();
    WL("(param $word_ptr i32)  ;; Pointer to letter-index encoded word");
    WL("(param $word_len i32)  ;; Number of letters (not bytes)");
    WL("(result i32)           ;; Returns new letter count (status in global)");

    /* Local variables */
    WN();
    WL("(local $i i32)         ;; Scan position");
    WL("(local $matched i32)   ;; Flag: did we match a rule?");
    WL("(local $terminated i32) ;; Flag: hit terminal rule?");

    if (algorithm_has_emit_rules(alg)) {
        WL("(local $emit_ptr i32)      ;; Emit buffer write position");
        WL("(local $match_start i32)   ;; Saved match position for emit");
        WL("(local $pre_word_len i32)  ;; Word length before substitution");
    }

    /* Set up abstract bind map if this is an abstract algorithm */
    setup_abstract_bind_map(alg, bind_override);

    /* No outer loop — just one pass through all rules */
    WN();
    WL(";; Single iteration: try all rules, fire first match");
    WL("(local.set $matched (i32.const 0))");
    WL("(local.set $terminated (i32.const 0))");

    /* Generate code for each rule (same as full algorithm) */
    for (size_t r = 0; r < alg->rules_count; ++r) {
        algorithm_rule *rule = alg->rules[r];
        if (rule == NULL) continue;

        if (r > 0) {
            WN();
            WL(";; Skip if already matched");
            WL("(if (i32.eqz (local.get $matched)) (then");
            WI();
        }

        wasm_write_rule(alg, rule, r + 1);
    }

    /* Close the nested if blocks for rules 2..N */
    for (size_t r = 1; r < alg->rules_count; ++r) {
        WD();
        WL("))");
    }

    /* Set step_status global: 0=matched, 1=terminated, 2=no_match */
    WN();
    WL(";; Set step status");
    WL("(if (local.get $terminated)");
    WI();
    WL("(then (global.set $step_status (i32.const 1)))");
    WD();
    WL("(else (if (local.get $matched)");
    WI();
    WL("(then (global.set $step_status (i32.const 0)))");
    WD();
    WL("(else (global.set $step_status (i32.const 2)))");
    WL("))");
    WL(")");

    /* Return word length */
    WN();
    WL("(local.get $word_len)");

    WD();
    WL(")");

    clear_abstract_bind_map();
}

/* Find a bind in ctx by its assigned variable name. */
static alphabet_bind *find_bind_by_name(program_context *ctx,
                                        lexical_store *name) {
    if (ctx == NULL || name == NULL) return NULL;
    size_t name_len = name->end - name->begin;
    for (size_t i = 0; i < ctx->binds_count; i++) {
        alphabet_bind *b = ctx->binds[i];
        if (b->name == NULL) continue;
        size_t bn_len = b->name->end - b->name->begin;
        if (bn_len == name_len &&
            memcmp(b->name->begin, name->begin, name_len) == 0) {
            return b;
        }
    }
    return NULL;
}

/* Find an algorithm in ctx by name. */
static algorithm_definition *find_algorithm_by_name(program_context *ctx,
                                                    lexical_store *name) {
    if (ctx == NULL || name == NULL) return NULL;
    size_t name_len = name->end - name->begin;
    for (size_t i = 0; i < ctx->algorithms_count; i++) {
        algorithm_definition *a = ctx->algorithms[i];
        if (a->name == NULL) continue;
        size_t an_len = a->name->end - a->name->begin;
        if (an_len == name_len &&
            memcmp(a->name->begin, name->begin, name_len) == 0) {
            return a;
        }
    }
    return NULL;
}

/* Has a specialized (alg, bind) pair already been emitted in this context? */
static bool pair_already_emitted(algorithm_call **seen, size_t seen_count,
                                 lexical_store *alg_name,
                                 lexical_store *bind_name) {
    for (size_t i = 0; i < seen_count; i++) {
        algorithm_call *c = seen[i];
        if (c->algorithm_name == NULL || c->selected_bind == NULL) continue;
        size_t an = c->algorithm_name->end - c->algorithm_name->begin;
        size_t bn = c->selected_bind->end - c->selected_bind->begin;
        size_t an2 = alg_name->end - alg_name->begin;
        size_t bn2 = bind_name->end - bind_name->begin;
        if (an == an2 && bn == bn2 &&
            memcmp(c->algorithm_name->begin, alg_name->begin, an) == 0 &&
            memcmp(c->selected_bind->begin, bind_name->begin, bn) == 0) {
            return true;
        }
    }
    return false;
}

/* Recursively write algorithms from a context and all nested contexts */
void wasm_write_context_algorithms(program_context *ctx) {
    if (ctx == NULL) return;

    for (size_t i = 0; i < ctx->algorithms_count; ++i) {
        wasm_write_algorithm(ctx->algorithms[i], i, NULL);
        wasm_write_algorithm_step(ctx->algorithms[i], i, NULL);
    }

    /* Emit a specialized variant for each unique (algorithm, selected_bind) pair
     * referenced by calls in this scope. */
    algorithm_call **seen = NULL;
    size_t seen_count = 0;
    size_t seen_cap = 0;
    for (size_t c = 0; c < ctx->calls_count; c++) {
        algorithm_call *call = ctx->calls[c];
        if (call == NULL || call->selected_bind == NULL ||
            call->algorithm_name == NULL) continue;

        if (pair_already_emitted(seen, seen_count,
                                 call->algorithm_name, call->selected_bind)) {
            continue;
        }

        algorithm_definition *alg =
            find_algorithm_by_name(ctx, call->algorithm_name);
        alphabet_bind *bind = find_bind_by_name(ctx, call->selected_bind);
        if (alg == NULL || bind == NULL) continue;

        wasm_write_algorithm(alg, 0, bind);
        wasm_write_algorithm_step(alg, 0, bind);

        if (seen_count == seen_cap) {
            seen_cap = seen_cap ? seen_cap * 2 : 8;
            seen = realloc(seen, sizeof(algorithm_call *) * seen_cap);
        }
        seen[seen_count++] = call;
    }
    free(seen);

    for (size_t i = 0; i < ctx->content_count; ++i) {
        wasm_write_context_algorithms(ctx->content[i]);
    }
}

/* Write all algorithms as WASM functions (recursive traversal) */
void wasm_write_algorithms(void) {
    program_context *ctx = context_root();
    if (ctx == NULL) return;

    WN();
    WL(";; ========================================");
    WL(";; Algorithm Functions");
    WL(";; ========================================");

    wasm_write_context_algorithms(ctx);
}

/* Check if any algorithm call uses stdin (~) */
static bool program_has_stdin_calls(void) {
    program_context *ctx = context_root();
    if (ctx == NULL) return false;
    for (size_t i = 0; i < ctx->calls_count; i++) {
        if (ctx->calls[i]->input_type == CALL_STDIN) return true;
    }
    return false;
}

/* Collect all algorithm calls from root context */
static size_t program_call_count(void) {
    program_context *ctx = context_root();
    return ctx ? ctx->calls_count : 0;
}

#define CALL_SCRATCH_BASE 20480

/* Generate the $_start function that sequences all algorithm calls */
void wasm_write_start_function(void) {
    program_context *ctx = context_root();
    if (ctx == NULL || ctx->calls_count == 0) return;

    char buf[1024];

    WN();
    WL(";; ========================================");
    WL(";; Entry Point");
    WL(";; ========================================");

    WN();
    WL("(func $_start (export \"_start\")");
    WI();
    WL("(local $scratch i32)");
    WL("(local $byte_len i32)");
    WL("(local $index_count i32)");
    WL("(local $result_len i32)");
    WN();

    for (size_t c = 0; c < ctx->calls_count; c++) {
        algorithm_call *call = ctx->calls[c];
        if (call->algorithm_name == NULL) continue;

        int name_len = (int)(call->algorithm_name->end - call->algorithm_name->begin);

        /* Build the decorated call name "sort" or "sort$bind1" */
        char call_fn[256];
        if (call->selected_bind != NULL) {
            int bl = (int)(call->selected_bind->end - call->selected_bind->begin);
            snprintf(call_fn, sizeof(call_fn), "%.*s$%.*s",
                name_len, call->algorithm_name->begin,
                bl, call->selected_bind->begin);
        } else {
            snprintf(call_fn, sizeof(call_fn), "%.*s",
                name_len, call->algorithm_name->begin);
        }

        snprintf(buf, sizeof(buf), ";; Call: %s(...)", call_fn);
        WL(buf);

        switch (call->input_type) {
        case CALL_LITERAL: {
            /* Write literal bytes to scratch area, call encode_word, call algorithm */
            if (call->input_token == NULL) break;
            /* Strip quotes from string literal */
            const uint8_t *str = call->input_token->begin + 1;
            size_t str_len = (call->input_token->end - call->input_token->begin) - 2;

            /* Write literal bytes to CALL_SCRATCH_BASE */
            snprintf(buf, sizeof(buf), "(local.set $scratch (i32.const %d))", CALL_SCRATCH_BASE);
            WL(buf);
            for (size_t i = 0; i < str_len; i++) {
                /* Handle multi-byte UTF-8 */
                snprintf(buf, sizeof(buf),
                    "(i32.store8 (i32.add (local.get $scratch) (i32.const %zu)) (i32.const %d))",
                    i, str[i]);
                WL(buf);
            }
            snprintf(buf, sizeof(buf),
                "(local.set $byte_len (i32.const %zu))", str_len);
            WL(buf);

            /* Encode raw bytes -> letter indices */
            WL("(local.set $index_count (call $encode_word (local.get $scratch) (local.get $byte_len)))");

            /* Call the algorithm */
            snprintf(buf, sizeof(buf),
                "(local.set $result_len (call $%s (i32.const %d) (local.get $index_count)))",
                call_fn, WORD_BUFFER_BASE);
            WL(buf);
            break;
        }
        case CALL_VARIABLE: {
            /* Resolve variable to its word literal value at compile time.
             * Search the AST for an assignment to this variable name. */
            if (call->input_token == NULL) break;
            size_t var_len = call->input_token->end - call->input_token->begin;

            /* Find the assignment's right-hand side */
            const uint8_t *word_str = NULL;
            size_t word_str_len = 0;
            syntax_store *tree = Syntax.tree();
            for (size_t si = 0; si < Syntax.info->count; si++) {
                syntax_store *node = &tree[-((int)si)];
                if (node->type == ast_assignment_statement &&
                    node->size >= 2 &&
                    node->content[0] != NULL &&
                    node->content[1] != NULL) {
                    lexical_store *assign_var = Lex.store(node->content[0]->token_index);
                    size_t assign_len = assign_var->end - assign_var->begin;
                    if (assign_len == var_len &&
                        memcmp(assign_var->begin, call->input_token->begin, var_len) == 0) {
                        /* Found — get the string literal value */
                        syntax_store *rhs = node->content[1];
                        lexical_store *rhs_tok = NULL;
                        /* Handle word_in_expression: content[0] is word_literal */
                        if (rhs->type == ast_word_in_expression && rhs->size >= 1) {
                            rhs = rhs->content[0];
                        }
                        if (rhs->type == ast_word_literal) {
                            rhs_tok = Lex.store(rhs->token_index);
                        } else {
                            /* Try using the rhs token directly as string */
                            rhs_tok = Lex.store(rhs->token_index);
                        }
                        if (rhs_tok != NULL && rhs_tok->token == TOKEN_STRING_LITERAL) {
                            word_str = rhs_tok->begin + 1; /* skip opening quote */
                            word_str_len = (rhs_tok->end - rhs_tok->begin) - 2;
                        }
                        break;
                    }
                }
            }

            if (word_str != NULL) {
                /* Same as CALL_LITERAL */
                snprintf(buf, sizeof(buf), "(local.set $scratch (i32.const %d))", CALL_SCRATCH_BASE);
                WL(buf);
                for (size_t i = 0; i < word_str_len; i++) {
                    snprintf(buf, sizeof(buf),
                        "(i32.store8 (i32.add (local.get $scratch) (i32.const %zu)) (i32.const %d))",
                        i, word_str[i]);
                    WL(buf);
                }
                snprintf(buf, sizeof(buf),
                    "(local.set $byte_len (i32.const %zu))", word_str_len);
                WL(buf);
                WL("(local.set $index_count (call $encode_word (local.get $scratch) (local.get $byte_len)))");
                snprintf(buf, sizeof(buf),
                    "(local.set $result_len (call $%s (i32.const %d) (local.get $index_count)))",
                    call_fn, WORD_BUFFER_BASE);
                WL(buf);
            } else {
                WL(";; Warning: could not resolve variable word input");
            }
            break;
        }
        case CALL_STDIN: {
            /* Call $read host function to get input, then encode and run */
            snprintf(buf, sizeof(buf), "(local.set $scratch (i32.const %d))", CALL_SCRATCH_BASE);
            WL(buf);
            snprintf(buf, sizeof(buf),
                "(local.set $byte_len (call $read (local.get $scratch) (i32.const %d)))",
                4096); /* max read size */
            WL(buf);

            /* Encode raw bytes -> letter indices */
            WL("(local.set $index_count (call $encode_word (local.get $scratch) (local.get $byte_len)))");

            /* Call the algorithm */
            snprintf(buf, sizeof(buf),
                "(local.set $result_len (call $%s (i32.const %d) (local.get $index_count)))",
                call_fn, WORD_BUFFER_BASE);
            WL(buf);
            break;
        }
        case CALL_COMPOSED: {
            /* Composed call: sort(reverse("..."))
             * Run the inner call first, then use its result as input */
            if (call->inner_call == NULL) break;
            algorithm_call *inner = call->inner_call;

            if (inner->algorithm_name == NULL) break;
            int inner_name_len = (int)(inner->algorithm_name->end - inner->algorithm_name->begin);

            /* Generate code for inner call's input */
            if (inner->input_type == CALL_LITERAL && inner->input_token != NULL) {
                const uint8_t *str = inner->input_token->begin + 1;
                size_t str_len = (inner->input_token->end - inner->input_token->begin) - 2;
                snprintf(buf, sizeof(buf), "(local.set $scratch (i32.const %d))", CALL_SCRATCH_BASE);
                WL(buf);
                for (size_t i = 0; i < str_len; i++) {
                    snprintf(buf, sizeof(buf),
                        "(i32.store8 (i32.add (local.get $scratch) (i32.const %zu)) (i32.const %d))",
                        i, str[i]);
                    WL(buf);
                }
                snprintf(buf, sizeof(buf), "(local.set $byte_len (i32.const %zu))", str_len);
                WL(buf);
                WL("(local.set $index_count (call $encode_word (local.get $scratch) (local.get $byte_len)))");
            } else if (inner->input_type == CALL_STDIN) {
                snprintf(buf, sizeof(buf), "(local.set $scratch (i32.const %d))", CALL_SCRATCH_BASE);
                WL(buf);
                snprintf(buf, sizeof(buf),
                    "(local.set $byte_len (call $read (local.get $scratch) (i32.const %d)))", 4096);
                WL(buf);
                WL("(local.set $index_count (call $encode_word (local.get $scratch) (local.get $byte_len)))");
            }

            /* Run inner algorithm */
            snprintf(buf, sizeof(buf),
                "(local.set $index_count (call $%.*s (i32.const %d) (local.get $index_count)))",
                inner_name_len, inner->algorithm_name->begin, WORD_BUFFER_BASE);
            WL(buf);

            /* Run outer algorithm on the result (already in word buffer as indices) */
            snprintf(buf, sizeof(buf),
                "(local.set $result_len (call $%s (i32.const %d) (local.get $index_count)))",
                call_fn, WORD_BUFFER_BASE);
            WL(buf);
            break;
        }
        }

        WN();
    }

    WD();
    WL(")");
}

/* ========================================================================
 * Entry points
 * ======================================================================== */

void wasm_use_stdout(void) {
    init_wat();
    Wat.file = stdout;
}

void wm_generate_s_statements(struct data *Data) {
    bool file_open = false;
    bool using_stdout = (Wat.file == stdout);

    init_wat();

    /* Build the global letter table from Data module */
    build_global_letter_table(Data);

    /* Reset string constants table */
    StringConstants.count = 0;
    StringConstants.next_offset = 0;

    /* Pre-register string constants for emit expressions.
     * This must happen before WAT emission so we know data section content. */
    bool has_emit = program_has_emit_rules();
    if (has_emit) {
        /* Walk all contexts and register algorithm/rule names referenced by emit rules */
        program_context *stack[64];
        int sp = 0;
        program_context *root = context_root();
        if (root) stack[sp++] = root;
        while (sp > 0) {
            program_context *c = stack[--sp];
            for (size_t i = 0; i < c->algorithms_count; i++) {
                algorithm_definition *a = c->algorithms[i];
                if (a->name) {
                    register_string_constant(a->name->begin,
                        (size_t)(a->name->end - a->name->begin));
                }
                for (size_t j = 0; j < a->rules_count; j++) {
                    if (a->rules[j]->rule_name) {
                        register_string_constant(a->rules[j]->rule_name->begin,
                            (size_t)(a->rules[j]->rule_name->end - a->rules[j]->rule_name->begin));
                    }
                }
            }
            for (size_t i = 0; i < c->content_count && sp < 64; i++) {
                stack[sp++] = c->content[i];
            }
        }
    }

    /* Build output filename */
    char name[256] = "./bin/";
    strcat(name, (char *)Lex.file->name);
    strcat(name, ".wat");

    if (Wat.file == NULL) {
        Wat.file = fopen(name, "w");
        if (Wat.file == NULL) {
            fprintf(stderr, "Error: Could not open %s for writing\n", name);
            return;
        }
        file_open = true;
    }

    if (!using_stdout) {
        printf("Generating WAT output: %s\n", name);
    }

    /* Module header */
    Wat.str("(module");
    WN();
    WI();

    /* Host imports */
    bool has_stdin = program_has_stdin_calls();
    bool has_calls = program_call_count() > 0;

    if (has_emit || has_stdin || has_calls) {
        WN();
        WL(";; Host imports");
    }
    if (has_emit) {
        WL("(import \"env\" \"emit\" (func $emit (param i32 i32)))");
    }
    if (has_stdin) {
        WL("(import \"env\" \"read\" (func $read (param i32 i32) (result i32)))");
    }

    /* Memory declaration */
    wasm_write_memory();

    /* Step status global for streaming execution */
    WN();
    WL(";; Step status: 0=matched, 1=terminated, 2=no_match");
    WL("(global $step_status (export \"step_status\") (mut i32) (i32.const 2))");

    /* Letter data sections (string data + offset table) */
    wasm_write_letter_data(Data);

    /* String constant data section for emit expressions */
    if (StringConstants.count > 0) {
        char buf[512];
        WN();
        WL(";; String constants for emit expressions");
        for (int i = 0; i < StringConstants.count; i++) {
            Wat.indent();
            snprintf(buf, sizeof(buf), "(data (i32.const %zu) \"",
                StringConstants.entries[i].wasm_offset);
            Wat.str(buf);
            for (size_t j = 0; j < StringConstants.entries[i].len; j++) {
                snprintf(buf, sizeof(buf), "\\%02x",
                    StringConstants.entries[i].bytes[j]);
                Wat.str(buf);
            }
            snprintf(buf, sizeof(buf), "\")  ;; \"%.*s\"",
                (int)StringConstants.entries[i].len,
                StringConstants.entries[i].bytes);
            Wat.str(buf);
            WN();
        }
    }

    /* Helper functions */
    wasm_write_helper_functions();

    /* Algorithm functions (recursive traversal of all contexts) */
    wasm_write_algorithms();

    /* Entry point ($_start) if there are algorithm calls */
    if (has_calls) {
        wasm_write_start_function();
    }

    /* Close module */
    WD();
    Wat.str(")");
    WN();

    if (file_open) {
        fclose(Wat.file);
        Wat.file = NULL;
    }

    if (!using_stdout) {
        printf("WAT generation complete.\n");
    }
}

const struct webassembly WebAssembly = {
    .use_stdout = wasm_use_stdout,
    .generate   = wm_generate_s_statements
};
