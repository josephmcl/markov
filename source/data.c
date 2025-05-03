#include "data.h"

#define DEFAULT_REALLOC_SIZE 64

typedef struct {
    size_t   LettersCount;
    size_t   LettersByteCount;
    size_t   LettersByteCapacity;
    uint8_t *Letters;
} data_Data;

data_Data TheData = { 0 };

size_t data_letters_count(void) {
    return TheData.LettersCount;
}

size_t data_letters_bytes(void) {
    return TheData.LettersByteCount;
}

uint8_t *data_letters_data(void) {
    return &TheData.Letters[0];
}

bool data_letter_in_letters(lexical_store *letter) { 

    bool candidate, match;
    size_t i, count, letter_bytes, res;

    i = 0; count = 0;
    match = false; candidate = true;
    letter_bytes = letter->end - letter->begin;

    if (TheData.LettersByteCount < letter_bytes) 
        return false;

    while (count < TheData.LettersCount && match == false) {
        if (candidate) {
            
            candidate = false;
            
            res = (size_t) memcmp(
                (void *) &TheData.Letters[i], 
                (void *) letter->begin, 
                letter_bytes);
            
            if(res == 0) match = true;            
        }
        else if (TheData.Letters[i] == 0x0) {
            candidate = true;
            count += 1;
        }
        i += 1;
    }


    return match;
}

void data_push_letter(lexical_store *letter) {

    size_t old_count, bytes, letter_bytes;
    
    letter_bytes = letter->end - letter->begin;
    old_count = TheData.LettersByteCount;

    if (data_letter_in_letters(letter)) {
        return;
    }

    /* Resize the Letters array in a for loop. A Letter can be any
       arbitrary number of bytes so this is a safer way to ensure 
       that we always allocate enough space. Plus one to account for 
       the terminating NULL character. */
    for (size_t i = 0; i < letter_bytes + 1; ++i) {
        if (TheData.LettersByteCount == TheData.LettersByteCapacity) {
            TheData.LettersByteCapacity += DEFAULT_REALLOC_SIZE;
            bytes = sizeof(uint8_t) * TheData.LettersByteCapacity;
            TheData.Letters = (uint8_t *) realloc(
                TheData.Letters, bytes); 
        }
        TheData.LettersByteCount += 1;
    }

    /* Copy the letter data over and add a NULL terminating character 
       after. */
    for (size_t i = 0; i < letter_bytes; ++i) {
        TheData.Letters[old_count + i] = letter->begin[i];
    }
    TheData.Letters[old_count + letter_bytes] = 0x0;

    /* Increase the letter count by one. */
    TheData.LettersCount += 1;

    return;
    
}

void data_generate(
    const struct lex    *Lex,
    const struct syntax *Syntax) {

    lexical_store *letter;
    syntax_store *tree, *current;
    
    tree = Syntax->tree();
    for (size_t i = 0; i < Syntax->info->count; ++i) {
        current = tree - i;
        if (current->type == ast_letter) {
            letter = Lex->store(current->token_index);
            data_push_letter(letter);
        }
    }

    printf("Letter Data \n{");

    size_t i = 0;
    size_t count = 0;
    bool clean = true;
    while (count < TheData.LettersCount) {
        if (clean) {
            printf("%s", &TheData.Letters[i]);
            clean = false;
        }
        else if (TheData.Letters[i] == 0x0) {
            clean = true;
            count += 1;
        }
        i += 1;
    }

    printf("}\n");
    
    return;
}

const struct data Data = {
    .letters_count = data_letters_count,
    .letters_bytes = data_letters_bytes,
    .letters_data  = data_letters_data,
    .generate      = data_generate
};

/*
program_context *context_push(void) {

    size_t bytes;
    if (TheInfo.count == TheInfo.capacity) {
        TheInfo.capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(program_context) * TheInfo.capacity;
        TheContext = (program_context *) realloc(TheContext, bytes); 
    }

    TheInfo.count += 1;
    return TheContext + TheInfo.count - 1;
}

*/