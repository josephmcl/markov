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

        for (int i = 0; i < GlobalLetters.count; ++i) {
            size_t llen = GlobalLetters.entries[i].len;
            if (llen > best_len && pos + llen <= byte_len &&
                memcmp(&bytes[pos], GlobalLetters.entries[i].bytes, llen) == 0) {
                best_index = i;
                best_len = llen;
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

extern program_context *context_root(void);

void wasm_write_memory(void);
void wasm_write_letter_data(struct data *Data);
void wasm_write_helper_functions(void);
void wasm_write_algorithms(void);
void wasm_write_algorithm(algorithm_definition *alg, size_t index);
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

    if (rule->is_terminal) {
        WL("(local.set $terminated (i32.const 1))");
        WL("(local.set $matched (i32.const 1))");
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

/* Write a single algorithm as a WASM function */
void wasm_write_algorithm(algorithm_definition *alg, size_t index) {
    if (alg == NULL || alg->name == NULL) return;

    int name_len = (int)(alg->name->end - alg->name->begin);

    WN();
    Wat.indent();
    Wat.str(";; Algorithm: ");
    Wat.s((const char *)alg->name->begin, name_len);
    WN();

    /* Function signature */
    Wat.indent();
    Wat.str("(func $");
    Wat.s((const char *)alg->name->begin, name_len);
    Wat.str(" (export \"");
    Wat.s((const char *)alg->name->begin, name_len);
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

    /* Main Markov loop */
    WN();
    WL(";; Main loop: keep applying rules until terminated or no match");
    WL("(block $done");
    WI();
    WL("(loop $apply_rules");
    WI();

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
}

/* Recursively write algorithms from a context and all nested contexts */
void wasm_write_context_algorithms(program_context *ctx) {
    if (ctx == NULL) return;

    for (size_t i = 0; i < ctx->algorithms_count; ++i) {
        wasm_write_algorithm(ctx->algorithms[i], i);
    }

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

    /* Memory declaration */
    wasm_write_memory();

    /* Letter data sections (string data + offset table) */
    wasm_write_letter_data(Data);

    /* Helper functions */
    wasm_write_helper_functions();

    /* Algorithm functions (recursive traversal of all contexts) */
    wasm_write_algorithms();

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
