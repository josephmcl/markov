#include "syntax.h"

const size_t TreeMallocSize = 64;
static size_t TreeDataSize = 0;
static size_t TreeSize = 0;
static syntax_store *TheTree;

syntax_store *syntax_append(void) {
    
    TreeSize += 1;
    size_t bytes;
    if (TreeDataSize == 0) {
        TreeDataSize = TreeMallocSize;
        bytes = sizeof(syntax_store) * TreeDataSize;
        TheTree = (syntax_store *) malloc(bytes);
    }
    else if (TreeSize >= TreeDataSize) {
        TreeDataSize += TreeMallocSize;
        bytes = sizeof(syntax_store) * TreeDataSize;
        TheTree = (syntax_store *) realloc(TheTree, bytes); 
        printf("Realloc syntax append \n");
    }
    return TheTree + TreeSize;

}

const struct syntax Syntax = {
    .append = syntax_append
};