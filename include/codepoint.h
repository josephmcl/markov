#pragma once 

#include <stdint.h>
#include <stdio.h>

int code_point_cmp(uint8_t *a, uint8_t *b);

int utf8_whitespace(uint8_t *code_point);

int utf8_code_point_length(uint8_t c);

int utf8_code_point_to_int(uint8_t *code_point);

int utf8_code_point_alpha(uint8_t code_point); //TODO remove this?

int is_alpha(uint8_t c);

int utf8_code_point_digit(uint8_t code_point); //TODO remove this?

int is_digit(uint8_t c);

int is_digit19(uint8_t c);

int is_alphanum(uint8_t c);

uint8_t *consume_digit(uint8_t *head);
