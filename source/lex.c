#include "lex.h"

#define TOKENS_SIZE 25

/* The struct in which the lexer operates on. */
static lexical_store *TheTokens;
static file_info TheFile = {0}; 
static lexical_info TheInfo = {0}; 

static int validate_escape(uint8_t *head, uint8_t *end) {
    uint8_t *temp;
    int escaped;

    temp = head;
    escaped = 1;
    while(end - temp >= 0) {
        if (escaped == 0 && is_escape(*temp) != 0) 
            fprintf(stderr, "%s:%d:%ld: " error_s
                " unknown escape sequence:\n\\%c"
                "\n\n"
                , TheFile.name, TheInfo.rows
                , TheInfo.line_length + temp - head, *temp
            );
        escaped = (*temp == '\\')? 0: 1;
        temp++;
    }
    return 0;
}

int lexer_read(const char *path) {
    TheFile = read_file(path, "rb");
    return 0;
}

int lexer_read_string(const char *s) {
    TheFile = read_string(s);
    return 0;
}

void lexer_free() {
    free(TheFile.content);
    free(TheFile.name);
    free(TheTokens);
    TheFile = (file_info) {0};
    TheInfo = (lexical_info) {0}; 
}

lexical_store lexer_next() {
    lexical_store rv;
    uint8_t *offset, *head;
    lexical_token token;
    uint16_t pack;

    head = TheInfo.current;
    /* END_OF_CONTENT */
    if (TheFile.end - head <= 0) {
        rv.token = TOKEN_END_OF_CONTENT;
        rv.begin = TheInfo.current;
        rv.end = TheFile.end;
        return rv;
    }

    /* consume any whitespace */
    while (utf8_whitespace(head) == 0) {
        head += utf8_code_point_length(*head);
        rv.space_prior += 1;
    }

    /* END_OF_CONTENT */
    if (TheFile.end - head <= 0) {
        rv.token = TOKEN_END_OF_CONTENT;
        rv.begin = TheInfo.current;
        rv.end = TheFile.end;
        return rv;
    }

    rv.begin = head;

    /* MULTI BYTE TOKENS */
    if ((pack = multi_byte_token(head, TheFile.end)) != 0) {
        rv.end = head + (pack & 0x00FF);
        rv.token = (pack & 0xFF00) >> 0x8;

    }
    /* KEYWORD TOKENS */
    else if ((pack = keyword_token(head, TheFile.end)) != 0) {
        rv.end = head + (pack & 0x00FF);
        rv.token = (pack & 0xFF00) >> 0x8;

    }
    /* SINGLE BYTE TOKENS */
    else if ((token = single_byte_token(*head)) != TOKEN_UNKNOWN) {
        rv.end = head + 1;
        rv.token = token;
    }
    /* LINE_COMMENT 
    else if ((offset = line_comment(head, TheFile.end)) != head) {
        rv.end = offset;
        rv.token = LINE_COMMENT;
    }
    */
    
    /* STRING LITERAL */
    else if ((offset = string_literal(head, TheFile.end)) != head) {
        rv.end = offset;
        rv.token = TOKEN_STRING_LITERAL;
    }
    /* IDENTIFIER or NUMBER - identifier takes priority for mixed alphanumeric */
    else if ((offset = identifier(head)) != head) {
        /* Check if it's a pure number (identifier matches exactly number_literal) */
        uint8_t *num_end = number_literal(head);
        if (num_end == offset) {
            rv.end = offset;
            rv.token = TOKEN_NUMBER;
        } else {
            rv.end = offset;
            rv.token = TOKEN_IDENTIFIER;
        }
    }
    else if (*head == '\\') {
        rv.end = head + 1;
        rv.token = TOKEN_ESCAPE;
    }
    /* LINE END */
    else if (*head == '\n') {
        rv.token = TOKEN_LINE_END;
        rv.end = head + 1;
    }
    else {
        rv.token = TOKEN_UNKNOWN;
        rv.end = head + utf8_code_point_length(*head);
    }
 
    return rv;
}

static void push_token(lexical_store token) {  

    if (TheInfo.count == TheInfo.capacity) {
        TheInfo.capacity += TOKENS_SIZE;
        //TODO: make this safe
        
        TheTokens = (lexical_store *) realloc(
            TheTokens, 
            TheInfo.capacity * sizeof(lexical_store)
        );       
    }
    TheTokens[TheInfo.count] = token;
    TheInfo.count += 1; // TODO: we'll need to specify a max here    
} 

#define BLURB_SIZE 60

static uint8_t *line_blurb(uint8_t *end) { //TODO not UTF-8 safe
    int i, size;
    uint8_t *begin, *rv;
    begin = TheInfo.prior_newline;
    if (TheInfo.line_length > BLURB_SIZE)
        begin += TheInfo.line_length - BLURB_SIZE;
    if (begin + BLURB_SIZE > TheFile.end)
        size = TheFile.end - begin;
    else 
        size = BLURB_SIZE;
    rv = (uint8_t *) calloc(size + 1, sizeof(uint8_t));

    memcpy(rv, begin, size);
    for (i = 0; i < size; ++i)
        rv[i] = peroxide(rv[i]);
    return rv;

}

/* static */ uint8_t *line_blurb_with_caret(uint8_t *end) { //TODO WIP
    printf("hello");
    int i, offset, size, cp_count;
    uint8_t *begin, *rv;
    offset = 0;
    cp_count = 0;
    begin = TheInfo.prior_newline;
    if (TheInfo.line_length > BLURB_SIZE)
        begin += TheInfo.line_length - BLURB_SIZE;

    while (utf8_code_point_length(*begin) == 0) {
        begin += 1;
        offset += 1;
    }
    size = BLURB_SIZE + 1 - offset;
    rv = (uint8_t *) calloc(size * 2, sizeof(uint8_t));
    memcpy(rv, begin, size);
    rv[size] = '\n';

    for (i = 0; i < size; ++i)
        rv[i] = peroxide(rv[i]);
    for (; i < size * 2 - 1; ++i)
        rv[i] = ' ';
    i = 0;
    
    while (i < size)
        cp_count += utf8_code_point_length(*(begin + i));
    rv[cp_count] = '^';
    
    return rv;

}

int analyze() {
    int token_length;
    lexical_store next;    
    uint8_t *temp;

    TheInfo.capacity = 0;
    TheInfo.count = 0;
    TheInfo.columns = 0;
    TheInfo.rows = 1;
    TheInfo.index = 0;
    TheInfo.current = TheFile.content;
    TheInfo.prior_newline = TheFile.content;
    TheInfo.line_length = 0;

    token_length = 0;
    while ((next = lexer_next()).token != TOKEN_END_OF_CONTENT) {
        token_length = next.end - next.begin;
        TheInfo.line_length += token_length + next.begin - TheInfo.current;

        next.row = TheInfo.line_length;
        next.column = TheInfo.columns + 1;

        switch (next.token) {
        case TOKEN_LINE_END: {
            TheInfo.rows += 1;
            if (TheInfo.line_length > TheInfo.rows)
                TheInfo.columns += 1;
                //TheInfo.columns = TheInfo.line_length;
            TheInfo.line_length = 0;
            TheInfo.prior_newline = next.end;
            push_token(next);
        } break;

        case TOKEN_UNKNOWN: {
            temp = line_blurb(next.end);
            fprintf(stderr, "%s:%d:%d: "error_s
                " unknown symbol" "\n"
                "%s" "\n\n"
                , TheFile.name, TheInfo.rows
                , TheInfo.line_length, temp
            );
            free(temp);
            TheInfo.state = -1;
        } break;
        
        default: {
            push_token(next);
        } break;
        }

        /* move the head up to the end of the current token */
        TheInfo.current += next.end - TheInfo.current;
    }

    push_token(next);

    return 0;
} 

void lexer_print() {
    int i, j;
    lexical_store *token;
    uint8_t *token_string;
    size_t token_length;
    
    for (i = 0; i < TheInfo.count; i++) {

        /* Get current token */
        token = (TheTokens + i);

        /* Calculate number of bytes that comprises the token. */
        token_length = token->end - token->begin;

        if(token->token < 32)
            /* Print the string identifier of the token. */
            printf("%s ", token_names[token->token]);
        else 
            /* Some tokens do not have a string identifer so print 
               their integer identifier. */
            printf("<%d> ", token->token);

        /* Copy the bytes that comprise the token. */     
        token_string = (uint8_t *) 
            calloc(token_length + 1, sizeof(uint8_t));
        memcpy(token_string, token->begin, token_length);
        
        /* Replace any nasty stuff with whitespace. */
        for (j = 0; j < token_length; ++j)
            token_string[j] = bleach(token_string[j]);
        
        /* Print the bytes that comprise the token, and the number 
           of bytes. Due to UTF-8 this may be greater than the number
           of columns printed. */
        printf("[%s] [%lu]\n", token_string, token_length);

        /* Free up copied token bytes. */
        free(token_string);
    }

    printf("\n");
    return;
}

lexical_token lexer_get_token(size_t index) {

    if (index > TheInfo.count)
        return TOKEN_UNKNOWN;

    return TheTokens[index].token; 
}

lexical_store *lexer_get_store(size_t index) {

    if (index > TheInfo.count)
        return NULL;

    return (TheTokens + index); 
}


int lexer_get_token_bison_compat(size_t index) {
    if (index > TheInfo.count)
        return TOKEN_UNKNOWN;
    int token = TheTokens[index].token;
    switch (token) {
        case TOKEN_END_OF_CONTENT: return TOKEN_END_OF_CONTENT;
        case TOKEN_LINE_END: return TOKEN_UNUSED_BY_PARSER;
        case TOKEN_IDENTIFIER: return IDENTIFIER;
        case TOKEN_STRING_LITERAL: return STRING_LITERAL;
        case TOKEN_IN: return IN;
        case TOKEN_NOT: return NOT;
        case TOKEN_EXTENDS: return EXTENDS;
        case TOKEN_DOUBLE_COLON: return DOUBLE_COLON;
        case TOKEN_EQUAL: return EQUAL;
        case TOKEN_COMMA: return COMMA;
        case TOKEN_LCURL: return LCURL;
        case TOKEN_RCURL: return RCURL;
        case TOKEN_LANGLE: return LANGLE;
        case TOKEN_RANGLE: return RANGLE;
        case TOKEN_LBRACKET: return LBRACKET;
        case TOKEN_RBRACKET: return RBRACKET;
        case TOKEN_ATSIGN: return ATSIGN;
        case TOKEN_SEMICOLON: return SEMICOLON;
        case TOKEN_PERIOD: return PERIOD;
        case TOKEN_EN_IN: return EN_IN;
        case TOKEN_EN_NOT: return EN_NOT;
        case TOKEN_EN_EXTENDS: return EN_EXTENDS;
        case TOKEN_EN_MODULE: return EN_MODULE;
        case TOKEN_EN_IMPORT: return EN_IMPORT;
        case TOKEN_EN_EXPORT: return EN_EXPORT;
        case TOKEN_UNION: return UNION;
        case TOKEN_INTERSECT: return INTERSECT;
        case TOKEN_BACKSLASH: return BACKSLASH;
        case TOKEN_EN_UNION: return EN_UNION;
        case TOKEN_EN_INTERSECT: return EN_INTERSECT;
        case TOKEN_EN_DIFFERENCE: return EN_DIFFERENCE;
        case TOKEN_PLUS: return PLUS;
        case TOKEN_NUMBER: return NUMBER;
        case TOKEN_ARROW: return ARROW;
        case TOKEN_TERMINAL: return TERMINAL;
        case TOKEN_LPAREN: return LPAREN;
        case TOKEN_RPAREN: return RPAREN;
        default: {
            printf("stray token, %d\n", token);
            return TOKEN_UNSUPPORTED_BY_PARSER;
        }
    }
}

const struct lex Lex = {
    .file = &TheFile,
    .info = &TheInfo,

    .token = lexer_get_token,
    .store = lexer_get_store,
    
    .read_string = lexer_read_string,
    .read = lexer_read,
    .free = lexer_free,
    .analyze = analyze,
    .print = lexer_print,

    .bison_token = lexer_get_token_bison_compat
};
