#include "grammar/program.h"

syntax_store *program_statements(syntax_store* statements) {

    /* Create and setup the program node */
    syntax_store *program = Syntax.push();
    program->type = ast_program;
    program->size = 1;

    /* Allocate space for the content nodes. */
    // TODO: Implement a single array for all content pointers in all 
    //       nodes to point to. Mallocing this way is not the best. 
    program->content = malloc(sizeof(syntax_store *) * 2);

    /* Set the topic / content pointers */
    program->content[0] = statements;
    program->content[0]->topic = program;

    /* Index 1 is reserved for the context name. Root is NULL */
    program->content[1] = NULL;
    return program;
}