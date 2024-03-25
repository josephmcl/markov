#include "codepoint.h"

#define WHITESPACE_LENGTH 25
const int utf8_whitespace_integers[] = {
    9,
    //10, //newline
    11,
    12,
    13,
    32,
    133,
    160,
    5760,
    8192,
    8193,
    8194,
    8195,
    8195,
    8196,
    8197,
    8198,
    8199,
    8200,
    8201,
    8202,
    8232,
    8233,
    8239,
    8287,
    12288
}; 

int utf8_code_point_cmp(uint8_t *a, uint8_t *b) {
    int i, lengtha, lengthb;

    lengtha = utf8_code_point_length(*a);
    lengthb = utf8_code_point_length(*b);
    if (lengtha != lengthb)
        return 1;
    for(i = 0; i < lengtha; ++i)
        if (*(a + i) != *(b + i))
            return i + 1; 
    return 0;

}

int utf8_whitespace(uint8_t *code_point) {
    int i;
    int val;

    val = utf8_code_point_to_int(code_point);

    for (i = 0; i < WHITESPACE_LENGTH; ++i) 
        if (val == utf8_whitespace_integers[i])
            return 0;
    return 1;
}



int utf8_code_point_length(uint8_t c) {
    if ((c & 0x80) == 0) 
        return 1;
    if ((c & 0x20) == 0)
        return 2;
    if ((c & 0x10) == 0)
        return 3;
    if ((c & 0x8) == 0)
        return 4;
    return 0; //TODO: this won't do
}

int utf8_code_point_to_int(uint8_t *code_point) {
    if ((*code_point & 0x80) == 0) 
        return (*code_point);
    if ((*code_point & 0x20) == 0)
        return 
            (((*code_point) - 0xC0) << 6)
            + (*(code_point + 1) - 0x80);
    if ((*code_point & 0x10) == 0)
        return 
            (((*code_point) - 0xE0) << 12)
            + ((*(code_point + 1) - 0x80) << 6)
            + (*(code_point + 2) - 0x80);
    if ((*code_point & 0x8) == 0)
        return 
            (((*code_point) - 0xF0) << 18)
            + ((*(code_point + 1) - 0x80) << 12)
            + ((*(code_point + 2) - 0x80) << 6)
            + (*(code_point + 2) - 0x80);
    else 
        return 0;

}

int utf8_code_point_alpha(uint8_t code_point) {
    if ((('A' <= code_point) && (code_point <= 'Z')) ||
       (('a' <= code_point) && (code_point <= 'z'))) 
        return 0;
    return 1;
}

int is_alpha(uint8_t c) {
    if (('A' <= c) && (c <= 'z')) 
        return 0;
    return 1;
}

int utf8_code_point_digit(uint8_t code_point) {
    if ((('A' <= code_point) && (code_point <= 'Z')) ||
       (('a' <= code_point) && (code_point <= 'z'))) 
        return 0;
    return 1;
}

int is_digit(uint8_t c) {
    if (('0' <= c) && (c <= '9')) 
        return 0;
    return 1;
}

int is_digit19(uint8_t c) {
    if (('1' <= c) && (c <= '9')) 
        return 0;
    return 1;
}

uint8_t *consume_digits(uint8_t *head) {
    while(is_digit(*(head++)) == 0)
        ;
    return head;
}

int is_alphanum(uint8_t c) {
    if ((utf8_code_point_alpha(c) == 0) || 
        (utf8_code_point_digit(c) == 0))
        return 0;
    return 1;
}
