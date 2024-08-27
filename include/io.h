#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define error_s "\033[31;1merror\033[0m:"

#define notice_c(f_, ...) fprintf(stderr,\
    ("(\033[32;1;4mnotice\033[0m) " f_), __VA_ARGS__);

#define warning_c(f_, ...) fprintf(stderr,\
    ("(\033[33;1;4mwarning\033[0m) " f_), __VA_ARGS__);

#define error_c(f_, ...) fprintf(stderr,\
    ("(\033[31;1;4merror\033[0m) " f_), __VA_ARGS__);

typedef struct file_info_s file_info;

struct file_info_s {
    int length; 
    int capacity;
    uint8_t *content;
    uint8_t *end;
    uint8_t *name; 
};

file_info read_file(const char *path, const char *options);

file_info read_string(const char *s);

/* please be careful */
char bleach(char c);
char peroxide(char c);
