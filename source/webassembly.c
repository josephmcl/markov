#include "webassembly.h"

size_t depth = 0;

const uint8_t wmleft = '(';
const uint8_t wmright = ')';
const uint8_t wmnewline = '\n';

typedef struct {
    FILE *file;
    const uint8_t left, right, newline, blank;
    size_t depth;
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


void wm_generate_s_statements(void) {

    if (Wat.l == NULL) {
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
    Wat.file = fopen(name,"w");  
    Wat.l();
    fwrite(b, sizeof(char), 6, Wat.file);
    Wat.r();
    fclose(Wat.file);
}

const struct webassembly WebAssembly = {
    .generate = &wm_generate_s_statements
};