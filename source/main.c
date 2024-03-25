#include "lex.h"

int main(int argc, char **argv) {

    if (argc < 2)
        exit(-1);
    
    Lex.read(argv[1]);
    
    Lex.analyze();

    Lex.print();

    Lex.free();

    return 0;
}
