#include "lex.h"
#include "bison.h"

extern int yyparse (void);

int main(int argc, char **argv) {

    if (argc < 2)
        exit(-1);
    
    Lex.read(argv[1]);
    
    Lex.analyze();

    // Lex.print();

    // Lex.free();

    int error = yyparse();

    printf("%d\n", error);

    return 0;
}
