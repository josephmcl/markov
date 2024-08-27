#include "io.h"

#define LINE_SIZE 64

static void _append_byte(file_info *file_info, uint8_t byte) {
    if (file_info->length == file_info->capacity) {
        file_info->capacity += LINE_SIZE;
        file_info->content = realloc(
            file_info->content, 
            file_info->capacity * sizeof(uint8_t)
        );       
    }
    file_info->content[file_info->length] = byte;
    file_info->length += 1; // TODO: we'll need to specify a max here
}

file_info read_file(const char *path, const char *options) {
    FILE *f;
    uint8_t b;
    file_info rv = {0};
    
    rv.length = 0;
    rv.capacity = LINE_SIZE;

    f = NULL;
    f = fopen(path, options);
    if (f == NULL)
        exit(-1); //TODO: we'll do exceptions later...

    rv.content = malloc(LINE_SIZE * sizeof(uint8_t));
    
    while ((b = (uint8_t) fgetc(f)) != EOF) {
        if (feof(f)) 
            break;
        _append_byte(&rv, b);
    }    
    
    fclose(f); 
    
    rv.name = (uint8_t *) calloc(strlen(path) + 1, sizeof(uint8_t));
    memcpy(rv.name, path, strlen(path) * sizeof(uint8_t));

    rv.end = &rv.content[rv.length];
    return rv;
} 

file_info read_string(const char *s) {
    uint8_t b;
    file_info rv = {0};
    
    rv.length = strlen(s);
    rv.capacity = strlen(s);

    rv.content = malloc(rv.length * sizeof(uint8_t));
    
    strcpy(rv.content, s);
        
    rv.name = (uint8_t *) calloc(2, sizeof(uint8_t));
    rv.name[0] = '.'; rv.name[1] = 0x0;
    rv.end = &rv.content[rv.length];
    return rv;
} 


char bleach(char c) {
    if (c == '\r')
        return ' '; 
    else if (c == '\n')
        return ' ';
    else 
        return c;
}

char peroxide(char c) {
    if (c == '\r')
        return '\0'; 
    else if (c == '\n')
        return '\0';
    else 
        return c;
}
