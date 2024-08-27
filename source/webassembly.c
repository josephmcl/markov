#include "webassembly.h"

size_t depth = 0;

const uint8_t wmleft = '(';
const uint8_t wmright = ')';
const uint8_t wmnewline = '\n';

typedef struct {
    FILE *file;
    const uint8_t left, right, newline, blank;
    size_t depth;
    void (*c) (char c);
    void (*s) (const char *s, size_t n);
    void (*l) (void);
    void (*r) (void);
    void (*space) (void);
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

void write_left(void) {
    fwrite(&Wat.left, sizeof(uint8_t), 1, Wat.file);
}

void write_right(void) {
    fwrite(&Wat.right, sizeof(uint8_t), 1, Wat.file);
}

void write_space(void) {
    fwrite(&Wat.blank, sizeof(uint8_t), 1, Wat.file);
}

void write_character(char c) {
    fwrite(&c, sizeof(char), 1, Wat.file);
}

void write_string(const char *s, size_t n) {
    fwrite(s, sizeof(char), n, Wat.file);
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

void wasm_generate_assignment(syntax_store *statement) {
    Wat.l();
    // fwrite(b, sizeof(char), 6, Wat.file);
    Wat.r();
    return;
}

void wasm_generate(syntax_store *statement) {

    switch (statement->type) {
        case ast_assignment_statement:
            wasm_generate_assignment(statement);
        case ast_scope:
            // wasm_generate_scope(statement);
            ;
        default: 
            ;
    }
    return;
}

void wasm_generate_program(syntax_store *program) {

    syntax_store* statements = program->content[0];
    if (statements == NULL) {
        return;
    }
    for (size_t i = 0; i < statements->size; ++i) {
        // _print_node_string(statements->content[i]->type);
        wasm_generate(program->content[i]);
    }
    return; 
}

void wasm_write_letter_data(struct data *Data) {
    // (data (i32.const 0) "< alphabet data here >")
    char data[4] = "data";
    char offset[11] = "i32.const 0";
    
    Wat.c('\n');
    Wat.increase_indent();
    Wat.indent();
    Wat.l();    
    Wat.s(data, 4);
    Wat.space();
    Wat.l();
    Wat.s(offset, 11);
    Wat.r();
    Wat.space();
    Wat.c('"');

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
            Wat.s(letter, i - j);
        }
        i += 1;
    }
    Wat.c('"');
    Wat.r();
    Wat.c('\n');
    Wat.decrease_indent();
    return;
}

void wasm_use_stdout(void) {
    Wat.file = stderr;
}

void wm_generate_s_statements(struct data *Data) {

    // lexical_store *lstore;
    bool file_open = false;
    syntax_store *tree;
    // program_context *topic = NULL;

    if (Wat.l == NULL) {
        Wat.c = write_character;
        Wat.s = write_string;
        Wat.l = write_left;
        Wat.r = write_right;
        Wat.space = write_space;
        Wat.indent = write_indent;
        Wat.increase_indent = increase_indent;
        Wat.decrease_indent = decrease_indent;
    }
    
    char name[256] = "./bin/";
    char b[256] = "module";
    strcat(name, Lex.file->name);

    if (Wat.file == NULL) {
        Wat.file = fopen(name,"w");  
        file_open = true;
    }
    Wat.l();
    Wat.s(b, 6);
    Wat.space();

    wasm_write_letter_data(Data);

    Wat.s(")\n",2);

    
    tree = Syntax.tree();
    if (tree->type == ast_program) {
        // wasm_generate_program(tree);
    }

    if (file_open) fclose(Wat.file);
}

const struct webassembly WebAssembly = {
    .use_stdout   = wasm_use_stdout,
    .generate = &wm_generate_s_statements
};