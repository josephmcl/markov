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
        rv.token = END_OF_CONTENT;
        rv.begin = TheInfo.current;
        rv.end = TheFile.end;
        return rv;
    }

    /* consume any whitespace */
    while (utf8_whitespace(head) == 0) 
        head += utf8_code_point_length(*head);

    /* END_OF_CONTENT */
    if (TheFile.end - head <= 0) {
        rv.token = END_OF_CONTENT;
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
    else if ((token = single_byte_token(*head)) != UNKNOWN) {
        rv.end = head + 1;
        rv.token = token;
    }
    /* LINE_COMMENT 
    else if ((offset = line_comment(head, TheFile.end)) != head) {
        rv.end = offset;
        rv.token = LINE_COMMENT;
    }
    */
    
    /* IDENTIFIER */
    else if ((offset = identifier(head)) != head) {
        rv.end = offset;
        rv.token = IDENTIFIER;
    }
    else if (*head == '\\') {
        rv.end = head + 1;
        rv.token = ESCAPE;
    }
    /* LINE END */
    else if (*head == '\n') {
        rv.token = LINE_END;
        rv.end = head + 1;
    }
    else {
        rv.token = UNKNOWN;
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
    while ((next = lexer_next()).token != END_OF_CONTENT) {
        token_length = next.end - next.begin;
        TheInfo.line_length += token_length + next.begin - TheInfo.current;

        next.row = TheInfo.line_length;
        next.column = TheInfo.columns;

        switch (next.token) {
        case LINE_END: {
            TheInfo.rows += 1;
            if (TheInfo.line_length > TheInfo.rows)
                TheInfo.columns = TheInfo.line_length;
            TheInfo.line_length = 0;
            TheInfo.prior_newline = next.end;
            push_token(next);
        } break;

        case UNKNOWN: {
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
    int i, j, token_length;
    lexical_store *token;
    uint8_t *temp;
    
    for (i = 0; i < TheInfo.count; i++) {
        token = (TheTokens + i);
        token_length = token->end - token->begin;
        if(token->token < 10)
            printf("%s ", token_names[token->token]);
        else 
            printf("<%d> ", token->token);
        temp = (uint8_t *) calloc(token_length + 1, sizeof(uint8_t));

        memcpy(temp, token->begin, token_length);

        for (j = 0; j < token_length; ++j)
            temp[j] = bleach(temp[j]);

        printf("[%s] [%d]\n", temp, token_length);

        free(temp);
    }
    printf("\n");
    return;
}

lexical_token lexer_get_token(size_t index) {

    if (index > TheInfo.count)
        return UNKNOWN;

    return TheTokens[index].token; 
}

lexical_store *lexer_get_store(size_t index) {

    if (index > TheInfo.count)
        return NULL;

    return (TheTokens + index); 
}

const struct lex Lex = {
    .file = &TheFile,
    .info = &TheInfo,

    .token = lexer_get_token,
    .store = lexer_get_store,
    
    .read = lexer_read,
    .free = lexer_free,
    .analyze = analyze,
    .print = lexer_print
};
