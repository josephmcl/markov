#include "lex.h"
#include "syntax.h"
#include "context.h"
#include "data.h"
#include "webassembly.h"

int main(int argc, char **argv) {

    if (argc < 2)
        exit(-1);
        
    /* Read in file from stdin.        */
    Lex.read(argv[1]);
    
    /* Lex the file into tokens.       */
    Lex.analyze();

    // Lex.print();

    /* Parse the tokens into an AST.   */
    Syntax.parse();

    if (Syntax.errors()) {
        Syntax.free();
        Lex.free();
        return 1;
    } 

    Syntax.print();

    Context.validate();

    Data.generate(&Lex, &Syntax);    

    //WebAssembly.generate();

    /* Free all of the memory we used. */
    Syntax.free();
    Lex.free();

    return 0;
}
