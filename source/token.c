#include "token.h"

const char *token_names[] = {
    "END_OF_CONTENT",
    "ESCAPE",
    "LINE_END",
    "LINE_COMMENT",
    "IDENTIFIER",
    "STRING_LITERAL",
    "NUMBER"
};

/*static*/ int ustrncmp(const uint8_t *s1, const uint8_t *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        ++s1;
        ++s2;
        --n;
    }
    if (n == 0)
        return 0;
    else
        return (*(uint8_t *)s1 - *(uint8_t *)s2);
}

int is_escape(uint8_t c) {
    switch (c) {
    default: 
        return 1;
    case 'a':
    case 'b':
    case 'f':
    case 'n':
    case 'r':
    case 't':
    case 'v':
    case '\\':
    case '\'':
    case '\"':
        return 0;
    }
} 

uint8_t *identifier(uint8_t *head) {
    while (utf8_whitespace(head) != 0
        && single_byte_token(*head) == TOKEN_UNKNOWN
        && *head != '\n'
        && *head != ':'   /* stop before :: */
        && *head != '-')  /* stop before -> and -. */
    {
        int sp = utf8_code_point_length(*head);
        if (sp == 0) {
            return head;
        }
        head += sp;
    }
    return head;
}

uint8_t *consume_whitespace(uint8_t *head) {
    while (utf8_whitespace(head) == 0) {
        head += utf8_code_point_length(*head);
    }
    return head; 
}

uint8_t *line_comment(uint8_t *head, uint8_t *end) {
    uint8_t *rv;

    rv = head;
    if (*rv == '#') {     
        while (end - rv > 0 && *rv != '\n') 
            rv += utf8_code_point_length(*rv);
    }
    return rv;
}

uint8_t *string_literal(uint8_t *head, uint8_t *end) {
    uint8_t state;
    uint8_t *rv;
    int escaped;

    escaped = 1;
    rv = head;
    if (*rv != '\"' && *rv != '\'')
        return head;
    state = *rv;
    rv += 1;
    while (*rv != state || escaped == 0) {
        if (end - rv <= 0 || *rv == '\n')
            return head;
        escaped = (*rv == '\\')? 0: 1;
        rv += utf8_code_point_length(*rv);
    }
    rv += 1;
    return rv;
}

#define MB_TOKS 8
#define MB_TOKS_OFFSET 4

static uint8_t multi_byte_tokens[MB_TOKS * MB_TOKS_OFFSET] = {
    "∈\t"
    "¬\t "
    "⊂\t"
    "::\t "
    "∪\t"
    "∩\t"
    "->\t "
    "-.\t "
};

uint16_t multi_byte_token(uint8_t *s, uint8_t *end) {
    int i, j;
    uint8_t *temp, *stamp;

    stamp = multi_byte_tokens;
    for (i = 0; i < MB_TOKS; ++i) {
        j = 0;
        temp = s;
        while (end - temp > 0 && *(temp + j) == *(stamp + j))
            j += 1;

        if (*(stamp + j) == '\t') {
            return 
                ((i + MULTI_BYTE_TOKENS) << 0x8) + j;
        }
        stamp += MB_TOKS_OFFSET;
    }
    return 0;
}

#define KY_TOKS 9
#define KY_TOKS_OFFSET 12

static uint8_t keyword_tokens[KY_TOKS * KY_TOKS_OFFSET] = {
    "intersect\t  "
    "in\t         "
    "not\t        "
    "extends\t    "
    "module\t     "
    "import\t     "
    "export\t     "
    "union\t      "
    "difference\t "
};

uint16_t keyword_token(uint8_t *s, uint8_t *end) {
    int i, j;
    uint8_t *temp, *stamp;

    stamp = keyword_tokens;
    for (i = 0; i < KY_TOKS; ++i) {
        j = 0;
        temp = s;
        while (end - temp > 0 && *(temp + j) == *(stamp + j))
            j += 1;

        if (*(stamp + j) == '\t') {
            return 
                ((i + KEYWORD_TOKENS) << 0x8) + j;
        }
        stamp += KY_TOKS_OFFSET;
    }
    return 0;
}

lexical_token single_byte_token(uint8_t c) {
    switch (c) {
    case '=': return TOKEN_EQUAL;
    case ',': return TOKEN_COMMA;
    case '{': return TOKEN_LCURL;
    case '}': return TOKEN_RCURL;
    case ';': return TOKEN_SEMICOLON;
    case '.': return TOKEN_PERIOD;
    case '<': return TOKEN_LANGLE;
    case '>': return TOKEN_RANGLE;
    case '[': return TOKEN_LBRACKET;
    case ']': return TOKEN_RBRACKET;
    case '@': return TOKEN_ATSIGN;
    case '\\': return TOKEN_BACKSLASH;
    case '+': return TOKEN_PLUS;
    case '(': return TOKEN_LPAREN;
    case ')': return TOKEN_RPAREN;
    default: return TOKEN_UNKNOWN;
    }
}

/* Match an integer literal (sequence of digits). Returns head if no match. */
uint8_t *number_literal(uint8_t *head) {
    uint8_t *rv = head;
    while (*rv >= '0' && *rv <= '9') {
        rv += 1;
    }
    return rv;
}
