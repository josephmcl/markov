#include "lex.h"
#include "syntax.h"
#include "context.h"
#include "data.h"
#include "webassembly.h"
#include "markov_record.h"
#include "context/definitions.h"

extern program_context *context_root(void);

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

    /* Encode each registered algorithm into the portable bit-packed record
     * and print it for verification. Skips algorithms without an abstract
     * alphabet; those aren't representable in this version of the format. */
    {
        program_context *root = context_root();
        if (root != NULL) {
            for (size_t i = 0; i < root->algorithms_count; i++) {
                algorithm_definition *alg = root->algorithms[i];
                if (alg == NULL || alg->abstract_alph == NULL) continue;
                MarkovAlgorithm rec;
                if (markov_encode_algorithm(alg, &rec)) {
                    int nlen = (int)(alg->name->end - alg->name->begin);
                    printf("Encoded algorithm '%.*s':\n",
                        nlen, alg->name->begin);
                    markov_print_algorithm(&rec);
                    markov_canonicalize_letters(&rec);
                    printf("Canonicalised:\n");
                    markov_print_algorithm(&rec);
                } else {
                    int nlen = (int)(alg->name->end - alg->name->begin);
                    printf("Failed to encode algorithm '%.*s'\n",
                        nlen, alg->name->begin);
                }
            }
        }
    }

    Data.generate(&Lex, &Syntax);

    WebAssembly.generate(&Data);

    /* Free all of the memory we used. */
    Context.free();
    Data.free();
    Syntax.free();
    Lex.free();

    return 0;
}
