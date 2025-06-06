#include "grammar/scope.h"

syntax_store *scope_scope(
    syntax_store *statements,
    syntax_store *scope_name) {

    syntax_store *scope = Syntax.push();
        scope->type = ast_scope;
        scope->size = 2;
        scope->content = malloc(sizeof(syntax_store *) * scope->size);
        scope->content[0] = statements;
        scope->content[1] = scope_name;
        scope->content[0]->topic = scope;
        return scope;
}

syntax_store *scope_scope_context_scope(
    syntax_store *scope_context,
    syntax_store *statements,
    syntax_store *scope_name) {
    
    syntax_store *scope = Syntax.push();
    scope->type = ast_scope;
    scope->size = 3;
    scope->content = malloc(sizeof(syntax_store *) * scope->size);
    scope->content[0] = statements;
    scope->content[1] = scope_name;
    scope->content[2] = scope_context;
    scope->content[0]->topic = scope;
    return scope;
}