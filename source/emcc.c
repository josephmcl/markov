#include "lex.h"
#include "syntax.h"
#include "context.h"
#include "data.h"
#include "webassembly.h"
#include "emscripten.h"

EMSCRIPTEN_KEEPALIVE
int main(int argc, char **argv) {

    printf("Hello, World!\n");

    // if (argc < 2)
    //    exit(-1);

    /*    
    Lex.read(argv[1]);
    Lex.analyze();
    Syntax.parse();

    if (Syntax.errors()) {
        Syntax.free();
        Lex.free();
        return 1;
    } 

    Syntax.print();

    Context.validate();

    Data.generate(&Lex, &Syntax);    

    WebAssembly.generate(&Data);

    Syntax.free();
    Lex.free();
    */

    return 0;
}
