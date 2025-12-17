#include "lex.h"
#include "syntax.h"
#include "context.h"
#include "data.h"
//#include "webassembly.h"

int main(int argc, char **argv) {

    if (argc < 2)
        exit(-1);

    printf("HELLO, WORLD!\n");
        
    /* Read in file from stdin.        */
    Lex.read(argv[1]);
    
    /* Lex the file into tokens.       */
    Lex.analyze();

    Lex.print();

    /* Parse the tokens into an AST.   */
    Syntax.parse();

    printf("Syntax parsed.\n");

    if (Syntax.errors()) {
        Syntax.free();
        Lex.free();
        return 1;
    } 

    Syntax.print();

    printf("Syntax printed.\n");

    Context.validate();

    printf("Context validated.\n");

    Data.generate(&Lex, &Syntax);    

    //WebAssembly.generate(&Data);

    /* Free all of the memory we used. */
    Syntax.free();
    Lex.free();

    return 0;
}
