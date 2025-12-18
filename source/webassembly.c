#include "webassembly.h"

/*
 * WASM Generation Strategy:
 *
 * 1. Memory Layout:
 *    - Offset 0-255: Reserved for letter data (null-terminated strings)
 *    - Offset 256+: Working memory for algorithm execution
 *
 * 2. Each algorithm becomes a WASM function that:
 *    - Takes a pointer to input word in memory
 *    - Applies rules in order until terminal or no match
 *    - Returns pointer to result
 *
 * 3. Pattern matching uses linear memory scans
 */

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

/* Forward declarations */
void wasm_write_memory(void);
void wasm_write_letter_data(struct data *Data);
void wasm_write_algorithms(void);
void wasm_write_algorithm(algorithm_definition *alg, size_t index);
void wasm_write_helper_functions(void);

/* Writer functions */
void write_left(void) {
    fwrite(&Wat.left, sizeof(uint8_t), 1, Wat.file);
}

void write_right(void) {
    fwrite(&Wat.right, sizeof(uint8_t), 1, Wat.file);
}

void write_space(void) {
    fwrite(&Wat.blank, sizeof(uint8_t), 1, Wat.file);
}

void write_newline(void) {
    fwrite(&Wat.newline, sizeof(uint8_t), 1, Wat.file);
}

void write_character(char c) {
    fwrite(&c, sizeof(char), 1, Wat.file);
}

void write_string(const char *s, size_t n) {
    fwrite(s, sizeof(char), n, Wat.file);
}

void write_str(const char *s) {
    fwrite(s, sizeof(char), strlen(s), Wat.file);
}

void write_indent(void) {
    for (size_t i = 0; i < Wat.depth * 2; ++i) {
        fwrite(&Wat.blank, sizeof(uint8_t), 1, Wat.file);
    }
}

void increase_indent(void) {
    Wat.depth += 1;
}

void decrease_indent(void) {
    if (Wat.depth > 0) {
        Wat.depth -= 1;
    }
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

/* Write (memory 1) declaration */
void wasm_write_memory(void) {
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(memory (export \"memory\") 1)");
    Wat.newline_fn();
}

/* Write letter data to data section */
void wasm_write_letter_data(struct data *Data) {
    Wat.newline_fn();
    Wat.indent();
    Wat.str(";; Letter data (null-separated)");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(data (i32.const 0) \"");

    size_t i, j, count, letters_count;
    uint8_t *letters, *letter;

    i = 0; j = 0; count = 0;
    letters_count = Data->letters_count();
    letters = Data->letters_data();
    bool clean = true;

    while (count < letters_count) {
        if (clean) {
            letter = &letters[i];
            clean = false;
            j = i;
        }
        else if (letters[i] == 0x0) {
            clean = true;
            count += 1;
            /* Write letter, escaping special chars */
            for (size_t k = j; k < i; ++k) {
                uint8_t c = letters[k];
                if (c == '"' || c == '\\') {
                    Wat.c('\\');
                }
                Wat.c(c);
            }
            Wat.str("\\00");  /* null terminator */
        }
        i += 1;
    }

    Wat.str("\")");
    Wat.newline_fn();
}

/* Write pattern data for an algorithm rule */
void wasm_write_pattern_data(algorithm_rule *rule, size_t *offset) {
    if (rule->pattern == NULL) return;

    lexical_store *lex = Lex.store(rule->pattern->token_index);
    size_t len = lex->end - lex->begin;

    Wat.indent();
    Wat.str(";; Pattern at offset ");
    char buf[32];
    snprintf(buf, sizeof(buf), "%zu", *offset);
    Wat.str(buf);
    Wat.str(": \"");
    Wat.s((const char *)lex->begin, len);
    Wat.str("\"");
    Wat.newline_fn();

    Wat.indent();
    Wat.str("(data (i32.const ");
    Wat.str(buf);
    Wat.str(") \"");
    Wat.s((const char *)lex->begin, len);
    Wat.str("\\00\")");
    Wat.newline_fn();

    *offset += len + 1;
}

/* Get the context that holds algorithms */
extern program_context *context_root(void);

/* Write all algorithms as WASM functions */
void wasm_write_algorithms(void) {
    program_context *ctx = context_root();
    if (ctx == NULL) return;

    Wat.newline_fn();
    Wat.indent();
    Wat.str(";; ========================================");
    Wat.newline_fn();
    Wat.indent();
    Wat.str(";; Algorithm Functions");
    Wat.newline_fn();
    Wat.indent();
    Wat.str(";; ========================================");
    Wat.newline_fn();

    /* Write each algorithm */
    for (size_t i = 0; i < ctx->algorithms_count; ++i) {
        wasm_write_algorithm(ctx->algorithms[i], i);
    }
}

/* Write pattern matching and substitution code for a single rule */
void wasm_write_rule(algorithm_rule *rule, size_t rule_num) {
    if (rule == NULL || rule->pattern == NULL) return;

    lexical_store *pat_lex = Lex.store(rule->pattern->token_index);
    int pat_len = (int)(pat_lex->end - pat_lex->begin);

    int repl_len = 0;
    lexical_store *repl_lex = NULL;
    if (!rule->is_terminal && rule->replacement != NULL) {
        repl_lex = Lex.store(rule->replacement->token_index);
        repl_len = (int)(repl_lex->end - repl_lex->begin);
    }

    char buf[256];

    Wat.newline_fn();
    Wat.indent();
    Wat.str(";; Rule ");
    snprintf(buf, sizeof(buf), "%zu", rule_num);
    Wat.str(buf);
    Wat.str(": \"");
    Wat.s((const char *)pat_lex->begin, pat_len);
    Wat.str("\"");
    if (rule->is_terminal) {
        Wat.str(" -> HALT");
    } else if (repl_lex) {
        Wat.str(" -> \"");
        Wat.s((const char *)repl_lex->begin, repl_len);
        Wat.str("\"");
    }
    Wat.newline_fn();

    /* Pattern matching loop: scan for pattern at each position */
    Wat.indent();
    Wat.str("(local.set $i (i32.const 0))");
    Wat.newline_fn();

    snprintf(buf, sizeof(buf), "(block $rule%zu_done", rule_num);
    Wat.indent();
    Wat.str(buf);
    Wat.newline_fn();
    Wat.increase_indent();

    snprintf(buf, sizeof(buf), "(loop $rule%zu_scan", rule_num);
    Wat.indent();
    Wat.str(buf);
    Wat.newline_fn();
    Wat.increase_indent();

    /* Check if we have enough chars left for the pattern */
    Wat.indent();
    snprintf(buf, sizeof(buf), "(br_if $rule%zu_done", rule_num);
    Wat.str(buf);
    Wat.newline_fn();
    Wat.increase_indent();
    Wat.indent();
    Wat.str("(i32.gt_s");
    Wat.newline_fn();
    Wat.increase_indent();
    Wat.indent();
    snprintf(buf, sizeof(buf), "(i32.add (local.get $i) (i32.const %d))", pat_len);
    Wat.str(buf);
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(local.get $word_len)))");
    Wat.newline_fn();
    Wat.decrease_indent();
    Wat.decrease_indent();

    /* Check each character of the pattern */
    Wat.indent();
    Wat.str("(if");
    Wat.newline_fn();
    Wat.increase_indent();

    /* Build the match condition: all pattern chars must match */
    Wat.indent();
    if (pat_len > 1) {
        Wat.str("(i32.and");
        Wat.newline_fn();
        Wat.increase_indent();
    }

    for (int c = 0; c < pat_len; ++c) {
        if (c > 0 && c < pat_len - 1) {
            Wat.indent();
            Wat.str("(i32.and");
            Wat.newline_fn();
            Wat.increase_indent();
        }

        Wat.indent();
        snprintf(buf, sizeof(buf), "(i32.eq (i32.load8_u (i32.add (local.get $word_ptr) (i32.add (local.get $i) (i32.const %d)))) (i32.const %d))",
            c, (unsigned char)pat_lex->begin[c]);
        Wat.str(buf);
        Wat.newline_fn();
    }

    /* Close the nested i32.and expressions */
    for (int c = 1; c < pat_len; ++c) {
        Wat.decrease_indent();
        Wat.indent();
        Wat.str(")");
        Wat.newline_fn();
    }

    /* Then branch: pattern matched */
    Wat.indent();
    Wat.str("(then");
    Wat.newline_fn();
    Wat.increase_indent();

    if (rule->is_terminal) {
        /* Terminal rule: set terminated flag and exit */
        Wat.indent();
        Wat.str("(local.set $terminated (i32.const 1))");
        Wat.newline_fn();
        Wat.indent();
        Wat.str("(local.set $matched (i32.const 1))");
        Wat.newline_fn();
        snprintf(buf, sizeof(buf), "(br $rule%zu_done)", rule_num);
        Wat.indent();
        Wat.str(buf);
        Wat.newline_fn();
    } else {
        /* Substitution rule: replace pattern with replacement */
        Wat.indent();
        Wat.str(";; Matched! Perform substitution");
        Wat.newline_fn();

        /* Write replacement bytes */
        for (int c = 0; c < repl_len; ++c) {
            Wat.indent();
            snprintf(buf, sizeof(buf), "(i32.store8 (i32.add (local.get $word_ptr) (i32.add (local.get $i) (i32.const %d))) (i32.const %d))",
                c, (unsigned char)repl_lex->begin[c]);
            Wat.str(buf);
            Wat.newline_fn();
        }

        /* Adjust word length if replacement is different size */
        int len_diff = repl_len - pat_len;
        if (len_diff != 0) {
            Wat.indent();
            Wat.str(";; Shift remaining chars and adjust length");
            Wat.newline_fn();

            /* For simplicity, we'll use the memcpy helper for shifting */
            if (len_diff < 0) {
                /* Replacement shorter: shift left */
                Wat.indent();
                Wat.str("(call $memcpy");
                Wat.newline_fn();
                Wat.increase_indent();
                Wat.indent();
                snprintf(buf, sizeof(buf), "(i32.add (local.get $word_ptr) (i32.add (local.get $i) (i32.const %d)))", repl_len);
                Wat.str(buf);
                Wat.newline_fn();
                Wat.indent();
                snprintf(buf, sizeof(buf), "(i32.add (local.get $word_ptr) (i32.add (local.get $i) (i32.const %d)))", pat_len);
                Wat.str(buf);
                Wat.newline_fn();
                Wat.indent();
                snprintf(buf, sizeof(buf), "(i32.sub (local.get $word_len) (i32.add (local.get $i) (i32.const %d))))", pat_len);
                Wat.str(buf);
                Wat.newline_fn();
                Wat.decrease_indent();
            } else {
                /* Replacement longer: shift right (copy backwards) */
                Wat.indent();
                Wat.str(";; TODO: Handle longer replacement (requires backward copy)");
                Wat.newline_fn();
            }

            Wat.indent();
            snprintf(buf, sizeof(buf), "(local.set $word_len (i32.add (local.get $word_len) (i32.const %d)))", len_diff);
            Wat.str(buf);
            Wat.newline_fn();
        }

        Wat.indent();
        Wat.str("(local.set $matched (i32.const 1))");
        Wat.newline_fn();
        snprintf(buf, sizeof(buf), "(br $rule%zu_done)", rule_num);
        Wat.indent();
        Wat.str(buf);
        Wat.newline_fn();
    }

    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")");  /* end then */
    Wat.newline_fn();

    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")");  /* end if */
    Wat.newline_fn();

    /* Increment position and continue scanning */
    Wat.indent();
    Wat.str("(local.set $i (i32.add (local.get $i) (i32.const 1)))");
    Wat.newline_fn();
    snprintf(buf, sizeof(buf), "(br $rule%zu_scan)", rule_num);
    Wat.indent();
    Wat.str(buf);
    Wat.newline_fn();

    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")");  /* end loop */
    Wat.newline_fn();
    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")");  /* end block */
    Wat.newline_fn();
}

/* Write a single algorithm as a WASM function */
void wasm_write_algorithm(algorithm_definition *alg, size_t index) {
    if (alg == NULL || alg->name == NULL) return;

    int name_len = (int)(alg->name->end - alg->name->begin);

    Wat.newline_fn();
    Wat.indent();
    Wat.str(";; Algorithm: ");
    Wat.s((const char *)alg->name->begin, name_len);
    Wat.newline_fn();

    /* Function signature: (func $alg_name (param $word_ptr i32) (param $word_len i32) (result i32)) */
    Wat.indent();
    Wat.str("(func $");
    Wat.s((const char *)alg->name->begin, name_len);
    Wat.str(" (export \"");
    Wat.s((const char *)alg->name->begin, name_len);
    Wat.str("\")");
    Wat.newline_fn();

    Wat.increase_indent();
    Wat.indent();
    Wat.str("(param $word_ptr i32)  ;; Pointer to input word in memory");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(param $word_len i32)  ;; Length of input word");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(result i32)           ;; Returns new length (word modified in place)");
    Wat.newline_fn();

    /* Local variables */
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(local $i i32)         ;; Scan position");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(local $matched i32)   ;; Flag: did we match a rule?");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(local $terminated i32) ;; Flag: hit terminal rule?");
    Wat.newline_fn();

    /* Algorithm body - main loop */
    Wat.newline_fn();
    Wat.indent();
    Wat.str(";; Main loop: keep applying rules until terminated or no match");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(block $done");
    Wat.newline_fn();
    Wat.increase_indent();
    Wat.indent();
    Wat.str("(loop $apply_rules");
    Wat.newline_fn();
    Wat.increase_indent();

    /* Reset matched flag each iteration */
    Wat.indent();
    Wat.str("(local.set $matched (i32.const 0))");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(local.set $terminated (i32.const 0))");
    Wat.newline_fn();

    /* Generate code for each rule */
    for (size_t r = 0; r < alg->rules_count; ++r) {
        algorithm_rule *rule = alg->rules[r];
        if (rule == NULL) continue;

        /* Skip remaining rules if we already matched */
        if (r > 0) {
            Wat.newline_fn();
            Wat.indent();
            Wat.str(";; Skip if already matched");
            Wat.newline_fn();
            Wat.indent();
            Wat.str("(br_if $apply_rules (local.get $matched))");
            Wat.newline_fn();
        }

        wasm_write_rule(rule, r + 1);
    }

    /* End of rule processing - check if we should continue */
    Wat.newline_fn();
    Wat.indent();
    Wat.str(";; Check termination conditions");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(br_if $done (local.get $terminated))");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(br_if $apply_rules (local.get $matched))");
    Wat.newline_fn();

    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")  ;; end loop");
    Wat.newline_fn();
    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")  ;; end block");
    Wat.newline_fn();

    /* Return the word length */
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(local.get $word_len)");
    Wat.newline_fn();

    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")");
    Wat.newline_fn();
}

/* Write helper functions for string operations */
void wasm_write_helper_functions(void) {
    Wat.newline_fn();
    Wat.indent();
    Wat.str(";; ========================================");
    Wat.newline_fn();
    Wat.indent();
    Wat.str(";; Helper Functions");
    Wat.newline_fn();
    Wat.indent();
    Wat.str(";; ========================================");
    Wat.newline_fn();

    /* String length function */
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(func $strlen (param $ptr i32) (result i32)");
    Wat.newline_fn();
    Wat.increase_indent();
    Wat.indent();
    Wat.str("(local $len i32)");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(local.set $len (i32.const 0))");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(block $done");
    Wat.newline_fn();
    Wat.increase_indent();
    Wat.indent();
    Wat.str("(loop $count");
    Wat.newline_fn();
    Wat.increase_indent();
    Wat.indent();
    Wat.str("(br_if $done (i32.eqz (i32.load8_u (i32.add (local.get $ptr) (local.get $len)))))");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(local.set $len (i32.add (local.get $len) (i32.const 1)))");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(br $count)");
    Wat.newline_fn();
    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")");
    Wat.newline_fn();
    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(local.get $len)");
    Wat.newline_fn();
    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")");
    Wat.newline_fn();

    /* Memory copy function */
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(func $memcpy (param $dest i32) (param $src i32) (param $len i32)");
    Wat.newline_fn();
    Wat.increase_indent();
    Wat.indent();
    Wat.str("(local $i i32)");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(local.set $i (i32.const 0))");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(block $done");
    Wat.newline_fn();
    Wat.increase_indent();
    Wat.indent();
    Wat.str("(loop $copy");
    Wat.newline_fn();
    Wat.increase_indent();
    Wat.indent();
    Wat.str("(br_if $done (i32.ge_u (local.get $i) (local.get $len)))");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(i32.store8");
    Wat.newline_fn();
    Wat.increase_indent();
    Wat.indent();
    Wat.str("(i32.add (local.get $dest) (local.get $i))");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(i32.load8_u (i32.add (local.get $src) (local.get $i)))");
    Wat.newline_fn();
    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(local.set $i (i32.add (local.get $i) (i32.const 1)))");
    Wat.newline_fn();
    Wat.indent();
    Wat.str("(br $copy)");
    Wat.newline_fn();
    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")");
    Wat.newline_fn();
    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")");
    Wat.newline_fn();
    Wat.decrease_indent();
    Wat.indent();
    Wat.str(")");
    Wat.newline_fn();
}

void wasm_use_stdout(void) {
    init_wat();
    Wat.file = stdout;
}

void wm_generate_s_statements(struct data *Data) {
    bool file_open = false;
    bool using_stdout = (Wat.file == stdout);

    init_wat();

    /* Build output filename (only used when not using stdout) */
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
    Wat.newline_fn();
    Wat.increase_indent();

    /* Memory declaration */
    wasm_write_memory();

    /* Letter data section */
    wasm_write_letter_data(Data);

    /* Helper functions */
    wasm_write_helper_functions();

    /* Algorithm functions */
    wasm_write_algorithms();

    /* Close module */
    Wat.decrease_indent();
    Wat.str(")");
    Wat.newline_fn();

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
