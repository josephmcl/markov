#include "lex.h"
#include "syntax.h"
#include "context.h"
#include "data.h"
#include "webassembly.h"
#include "emscripten.h"

EMSCRIPTEN_KEEPALIVE
int main(int argc, char **argv) {

    Lex.read_string(argv[2]);
    Lex.analyze();
    Syntax.parse();
    Context.validate();
    Data.generate(&Lex, &Syntax);
    WebAssembly.use_stdout();
    WebAssembly.generate(&Data);

    return 0;
}
