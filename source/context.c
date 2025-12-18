#include "context.h"
#include "codepoint.h"

#define PROGRAM_CONTEXT_SIZE 64

program_context *TheContext = NULL;

static program_context_info TheInfo = { 0 };

program_context *context_push(void) {

    size_t bytes;
    if (TheInfo.count == TheInfo.capacity) {
        TheInfo.capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(program_context) * TheInfo.capacity;
        TheContext = (program_context *) realloc(TheContext, bytes); 
    }

    TheInfo.count += 1;
    return TheContext + TheInfo.count - 1;
}

program_context *context_root(void) {
    return TheContext;
}

program_context *context_current(void) {
    return &TheContext[TheInfo.count];
}

syntax_store *_context_syntax_push(syntax_store *s) {

    size_t bytes;
    if (TheInfo.syntax_count == TheInfo.syntax_capacity) {
        TheInfo.syntax_capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(syntax_store *) * TheInfo.syntax_capacity;
        TheInfo.syntax_stack = (syntax_store **) realloc(TheInfo.syntax_stack, bytes); 
    }

    TheInfo.syntax_stack[TheInfo.syntax_count] = s;

    TheInfo.syntax_count += 1;
    return *(TheInfo.syntax_stack + TheInfo.syntax_count);
}

syntax_store *_context_syntax_pop(void) {

    size_t bytes;
    if (TheInfo.syntax_count == TheInfo.syntax_capacity) {
        TheInfo.syntax_capacity += PROGRAM_CONTEXT_SIZE;

        bytes = sizeof(syntax_store *) * TheInfo.syntax_capacity;
        TheInfo.syntax_stack = (syntax_store **) realloc(TheInfo.syntax_stack, bytes); 
    }
    
    TheInfo.syntax_count -= 1;
    return *(TheInfo.syntax_stack + TheInfo.syntax_count);
}

program_context *_context_push_content(
    program_context *context, 
    program_context *content) {

    size_t bytes;
    if (context->content_count == context->content_capacity) {
        context->content_capacity += 64;

        bytes = sizeof(program_context *) * context->content_capacity;
        context->content = 
            (program_context **) realloc(context->content, bytes); 
    }
    
    context->content[context->content_count] = content;
    context->content_count += 1;
    return *(context->content + context->content_count - 1);
}


syntax_store *_update_context_scope(syntax_store *store) {
    
    /* Add a new program context to the stack. */
    program_context *current = context_push();
    current->syntax = store;
    current->letters_count = 0;
    current->letters_capacity = 0;
    current->letters = NULL;
    current->content_count = 0;
    current->content_capacity = 0;
    current->content = NULL;
    current->variables_count = 0;
    current->variables_capacity = 0;
    current->variables = NULL;
    current->alphabet_literals_count = 0;
    current->alphabet_literals_capacity = 0;
    current->alphabet_literals = NULL;
    current->algorithms_count = 0;
    current->algorithms_capacity = 0;
    current->algorithms = NULL;
    bool found = false;
    lexical_store *letter, *capture_lstore;
    syntax_store *capture_store;

    /* Determine the topic context. */
    program_context *topic;

    /* If there is only one context, it's the global context. We're
       done. */
    if (TheInfo.count <= 1) { 
        topic = NULL;
    }
    /* Otherwise check prior contexts for the nearest context that  
       contains the current context. */
    else {

        syntax_store *statements;
        for (size_t i = 1; i != TheInfo.count; ++i) {
        
            /* Get the next context. */
            topic = current - i;     

            /* NOTE: This probably shouldn't be possible, but let's 
                     check anyways. */
            if (topic->syntax->size == 0) continue; 

            /* Look through the syntax tree held by the current 
               context. */
            statements = topic->syntax->content[0];

            /* Continue if the tree is empty. */
            if (statements->size == 0) continue;

            /* Otherwise check if any of the syntax tree nodes in this 
               context are the current current context. */
            for (size_t j = 0; j != statements->size; ++j) {
                if (statements->content[j] == store) {
                    
                    /* Trigger the success variable for the outer 
                       loop. */
                    found = true;
                    break;
                }
            }

            /* Halt if we found a match. */
            if (found) break;
        }

        // TODO: Maybe this can be improved in the future with better 
        //       parsing infrastructure. 
    }


    // TODO: Will need better error propagation at some point. 
    if (TheInfo.count > 1 && !found) {
        printf("Error. Topic program context could not be found in "
               "AST.\n");
    }
    else {
        printf("Found parent context.\n");
    }

    /* Set the topic of the current context. */
    current->topic = topic;

     /* Set the capture of the current context. */
     // TODO: This should be its own function.
    capture_store = current->syntax->content[2];
    if (capture_store == NULL) {
        current->capture = capture_pure;
    } 
    else {
        if (capture_store->type == ast_scope_context_names_literal) {
            capture_lstore = Lex.store(capture_store->token_index);
            if (capture_lstore->token == TOKEN_ATSIGN) {
                current->capture = capture_letters;
            }
            if (capture_lstore->token == TOKEN_EQUAL) {
                current->capture = capture_parent;
            }
        }
    }

    printf("Captured current context.\n");

    if (topic != NULL) {

        _context_push_content(topic, current);

        if (current->capture != capture_pure) {

            for (size_t i = 0; i < topic->letters_count; ++i) {
                letter = topic->letters[i];
                _update_known_context_letter(letter, current);
            }
        }
    }

    printf("Updated current context.\n");

    // TODO: Propagate variable information
    

    return NULL;
}

syntax_store *update_program_context(syntax_store *store) {
    switch (store->type) {
    case ast_statements:
        return NULL;
    case ast_program:
        return _update_context_scope(store);
    case ast_scope:
        return _update_context_scope(store);
    case ast_letter:
        return _update_context_letter(store, &TheInfo, TheContext);
    case ast_alphabet_body:
        return _update_context_alphabet_literal(store, &TheInfo, TheContext);
    case ast_variable:
        return _update_context_variable(store, &TheInfo, TheContext);
    case ast_algorithm:
        return _update_context_algorithm(store, &TheInfo, TheContext);
    default:
        return NULL; }
}

/* Find the index of a letter in the context's letters array.
   Returns the index if found, or context->letters_count if not found. */
size_t _context_find_letter_index(
    program_context *context,
    lexical_store   *letter) {

    lexical_store *current;
    size_t size = letter->end - letter->begin;

    for (size_t i = 0; i < context->letters_count; ++i) {
        current = context->letters[i];
        size_t current_size = current->end - current->begin;
        if (size == current_size &&
            memcmp(letter->begin, current->begin, size) == 0) {
            return i;
        }
    }
    return context->letters_count;
}

void update_program_context_letter_data(program_context *context) {

    alphabet_literal *alphabet;
    syntax_store *alphabet_body, *letters, *letter_node;
    lexical_store *letter;
    size_t letter_index;

    for (size_t i = 0; i < context->alphabet_literals_count; ++i) {
        alphabet = context->alphabet_literals[i];
        alphabet->letter_mask = 0;

        /* alphabet->store is the ast_alphabet_body node */
        alphabet_body = alphabet->store;
        if (alphabet_body == NULL || alphabet_body->size == 0) continue;

        /* content[0] is the ast_letters node */
        letters = alphabet_body->content[0];
        if (letters == NULL) continue;

        /* Iterate through each letter in this alphabet */
        for (size_t j = 0; j < letters->size; ++j) {
            letter_node = letters->content[j];
            if (letter_node == NULL) continue;

            letter = Lex.store(letter_node->token_index);
            letter_index = _context_find_letter_index(context, letter);

            if (letter_index < context->letters_count && letter_index < 64) {
                alphabet->letter_mask |= (1ULL << letter_index);
            }
        }
    }
}

/* Find the alphabet_literal associated with an ast_alphabet_body node. */
alphabet_literal *_find_alphabet_literal_for_syntax(
    program_context *context,
    syntax_store    *syntax) {

    for (size_t i = 0; i < context->alphabet_literals_count; ++i) {
        if (context->alphabet_literals[i]->store == syntax) {
            return context->alphabet_literals[i];
        }
    }
    return NULL;
}

/* Compare two variable names by their lexical tokens. */
static bool _variables_match(syntax_store *var1, syntax_store *var2) {
    if (var1 == NULL || var2 == NULL) return false;
    if (var1->type != ast_variable || var2->type != ast_variable) return false;

    lexical_store *lex1 = Lex.store(var1->token_index);
    lexical_store *lex2 = Lex.store(var2->token_index);

    size_t size1 = lex1->end - lex1->begin;
    size_t size2 = lex2->end - lex2->begin;

    if (size1 != size2) return false;
    return memcmp(lex1->begin, lex2->begin, size1) == 0;
}

/* Find the r_expression assigned to a variable by searching the AST.
   Searches for ast_assignment_statement nodes where content[0] matches
   the variable name. Returns the r_expression (content[1]) if found. */
syntax_store *_find_variable_assignment(syntax_store *var) {
    if (var == NULL || var->type != ast_variable) return NULL;

    syntax_store *tree = Syntax.tree();

    for (size_t i = 0; i < Syntax.info->count; ++i) {
        syntax_store *node = tree - i;
        if (node == NULL) continue;

        if (node->type == ast_assignment_statement && node->size >= 2) {
            /* content[0] is l_expression (variable), content[1] is r_expression */
            syntax_store *l_expr = node->content[0];

            /* l_expression wraps variable, check if it matches */
            if (l_expr != NULL && _variables_match(l_expr, var)) {
                return node->content[1];
            }
        }
    }
    return NULL;
}

/* Lazily compute the letter mask for any r_expression.
   This supports nested expressions like: A extends B union C */
uint64_t _compute_expression_mask(
    program_context *context,
    syntax_store    *expr) {

    if (expr == NULL) return 0;

    /* Base case: alphabet literal */
    if (expr->type == ast_alphabet_body) {
        alphabet_literal *alph = _find_alphabet_literal_for_syntax(context, expr);
        return alph ? alph->letter_mask : 0;
    }

    /* Binary operations: recursively compute both sides */
    if (expr->type == ast_union_expression) {
        uint64_t left = _compute_expression_mask(context, expr->content[0]);
        uint64_t right = _compute_expression_mask(context, expr->content[1]);
        return left | right;
    }

    if (expr->type == ast_intersect_expression) {
        uint64_t left = _compute_expression_mask(context, expr->content[0]);
        uint64_t right = _compute_expression_mask(context, expr->content[1]);
        return left & right;
    }

    if (expr->type == ast_difference_expression) {
        uint64_t left = _compute_expression_mask(context, expr->content[0]);
        uint64_t right = _compute_expression_mask(context, expr->content[1]);
        return left & ~right;
    }

    /* Extends expression: returns the left operand's mask after validation */
    if (expr->type == ast_extends_expression) {
        return _compute_expression_mask(context, expr->content[0]);
    }

    /* Variable lookup: find the variable's assigned r_expression */
    if (expr->type == ast_variable) {
        syntax_store *assigned = _find_variable_assignment(expr);
        if (assigned != NULL) {
            return _compute_expression_mask(context, assigned);
        }
        return 0;
    }

    return 0;
}

/* Recursively get the alphabet_literal for an r_expression.
   For binary expressions, returns the left operand's alphabet. */
alphabet_literal *_get_alphabet_for_expression(
    program_context *context,
    syntax_store    *expr) {

    if (expr == NULL) return NULL;

    if (expr->type == ast_alphabet_body) {
        return _find_alphabet_literal_for_syntax(context, expr);
    }
    else if (expr->type == ast_extends_expression ||
             expr->type == ast_union_expression ||
             expr->type == ast_intersect_expression ||
             expr->type == ast_difference_expression) {
        /* For binary ops, return the left operand's alphabet */
        return _get_alphabet_for_expression(context, expr->content[0]);
    }
    return NULL;
}

/* Validate extends expressions. B ⊂ A checks that B contains all letters of A.
   Validation: (A.mask & B.mask) == A.mask
   Uses lazy mask computation to support nested expressions like: A extends B union C */
bool _validate_extends_expression(
    program_context *context,
    syntax_store    *expr) {

    if (expr == NULL || expr->type != ast_extends_expression) return true;

    syntax_store *left_expr = expr->content[0];   /* B (the superset) */
    syntax_store *right_expr = expr->content[1];  /* A (the subset) */

    /* Use lazy computation for both sides */
    uint64_t left_mask = _compute_expression_mask(context, left_expr);
    uint64_t right_mask = _compute_expression_mask(context, right_expr);

    /* Check if right (A) is a subset of left (B): (A & B) == A */
    if ((right_mask & left_mask) != right_mask) {
        printf("Error: alphabet extension validation failed.\n");
        printf("       Left expression (mask 0x%llx) does not contain all letters of right expression (mask 0x%llx).\n",
               left_mask, right_mask);
        return false;
    }

    printf("Extends validation passed: 0x%llx ⊃ 0x%llx\n", left_mask, right_mask);
    return true;
}

/* Compute and print set operation results using lazy evaluation.
   Union: A ∪ B = A.mask | B.mask
   Intersection: A ∩ B = A.mask & B.mask
   Difference: A \ B = A.mask & ~B.mask
   Supports nested expressions like: (A union B) intersect C */
void _compute_set_operation(
    program_context *context,
    syntax_store    *expr) {

    if (expr == NULL) return;

    syntax_store *left_expr = expr->content[0];
    syntax_store *right_expr = expr->content[1];

    /* Use lazy computation for both sides */
    uint64_t left_mask = _compute_expression_mask(context, left_expr);
    uint64_t right_mask = _compute_expression_mask(context, right_expr);
    uint64_t result_mask = 0;
    const char *op_name = "";

    switch (expr->type) {
    case ast_union_expression:
        result_mask = left_mask | right_mask;
        op_name = "∪";
        break;
    case ast_intersect_expression:
        result_mask = left_mask & right_mask;
        op_name = "∩";
        break;
    case ast_difference_expression:
        result_mask = left_mask & ~right_mask;
        op_name = "\\";
        break;
    default:
        return;
    }

    printf("Set operation: 0x%llx %s 0x%llx = 0x%llx\n",
           left_mask, op_name, right_mask, result_mask);
}

/* Validate word_in_expression: check that every letter in the word
   exists in the alphabet.
   P = "abc" in A checks that a, b, c are all letters of A. */
bool _validate_word_in_expression(
    program_context *context,
    syntax_store    *expr) {

    if (expr == NULL || expr->type != ast_word_in_expression) return true;
    if (expr->size < 2) return false;

    syntax_store *word_node = expr->content[0];      /* word_literal */
    syntax_store *alphabet_expr = expr->content[1];  /* r_expression (alphabet) */

    if (word_node == NULL || word_node->type != ast_word_literal) {
        printf("Error: word_in_expression missing word literal.\n");
        return false;
    }

    /* Get the word string (includes quotes) */
    lexical_store *word_lex = Lex.store(word_node->token_index);
    uint8_t *word_begin = word_lex->begin + 1;  /* skip opening quote */
    uint8_t *word_end = word_lex->end - 1;      /* skip closing quote */
    size_t word_len = word_end - word_begin;

    /* Get the alphabet's mask */
    uint64_t alphabet_mask = _compute_expression_mask(context, alphabet_expr);

    printf("Word validation: \"");
    for (uint8_t *p = word_begin; p < word_end; ) {
        int cp_len = utf8_code_point_length(*p);
        printf("%.*s", cp_len, p);
        p += cp_len;
    }
    printf("\" in alphabet (mask 0x%llx)\n", alphabet_mask);

    /* Check each letter in the word against the context's letters */
    bool valid = true;
    uint8_t *p = word_begin;
    while (p < word_end) {
        int cp_len = utf8_code_point_length(*p);

        /* Find this letter in the context's letters array */
        bool found = false;
        size_t letter_index = 0;
        for (size_t i = 0; i < context->letters_count; ++i) {
            lexical_store *letter = context->letters[i];
            size_t letter_len = letter->end - letter->begin;
            if ((size_t)cp_len == letter_len &&
                memcmp(p, letter->begin, letter_len) == 0) {
                found = true;
                letter_index = i;
                break;
            }
        }

        if (!found) {
            printf("  Error: letter '%.*s' not found in context.\n", cp_len, p);
            valid = false;
        } else {
            /* Check if this letter is in the alphabet */
            if (letter_index < 64 && !(alphabet_mask & (1ULL << letter_index))) {
                printf("  Error: letter '%.*s' (index %zu) not in alphabet.\n",
                       cp_len, p, letter_index);
                valid = false;
            } else {
                printf("  Letter '%.*s' (index %zu) OK.\n", cp_len, p, letter_index);
            }
        }

        p += cp_len;
    }

    return valid;
}

/* Validate all expressions in a context. */
void _validate_context_expressions(program_context *context) {
    syntax_store *tree = Syntax.tree();

    for (size_t i = 0; i < Syntax.info->count; ++i) {
        syntax_store *current = tree - i;
        if (current == NULL) continue;

        if (current->type == ast_extends_expression) {
            _validate_extends_expression(context, current);
        }
        else if (current->type == ast_union_expression ||
                 current->type == ast_intersect_expression ||
                 current->type == ast_difference_expression) {
            _compute_set_operation(context, current);
        }
        else if (current->type == ast_word_in_expression) {
            _validate_word_in_expression(context, current);
        }
    }
}

void validate_program_context (void) {

    lexical_store *lstore;
    syntax_store *tree, *current;
    program_context *topic = NULL;
    tree = Syntax.tree();

    for (size_t i = 0; i < Syntax.info->count; ++i) {
        current = tree - i;
        if (current != NULL) {
            // _print_node_string(current->type);
            update_program_context(current);
        }
    }

    for (size_t i = 0; i < TheInfo.count; ++i) {
        update_program_context_letter_data(context_root() + i);
    }

    /* Validate expressions after letter masks are computed */
    for (size_t i = 0; i < TheInfo.count; ++i) {
        _validate_context_expressions(context_root() + i);
    }


    // TODO: Remove.
    printf("\nContext report.\n");

    for (size_t i = 0; i < TheInfo.count; ++i) {
        printf("Context %lu\n| address   (%p)\n", i, (void *) TheContext[i].syntax);
        topic = TheContext[i].topic;
        if (topic == NULL) {
            printf("| parent   (NULL, root context) \n");
        }
        else {      
            printf("| parent    (%p)\n", (void *) topic->syntax);
            current = TheContext[i].syntax->content[1];
            if (current == NULL) {
                printf("| name      (anonymous)\n");
            }
            else {
                lstore = Lex.store(current->token_index);
                int size = (int) (lstore->end - lstore->begin);                        
                printf("| name      (%.*s)\n", size, lstore->begin);
            }
        }
        
        current = TheContext[i].syntax->content[2];
        // lstore = Lex.store(current->token_index);

        if (TheContext[i].capture == capture_pure) {
            printf("| capture   (none)\n");
        }
        else if (TheContext[i].capture == capture_letters) {
            printf("| capture   (letters)\n");
        }
        else if (TheContext[i].capture == capture_parent) {
            printf("| capture   (parent)\n");
        }

        if (current == NULL) {
            printf("| capture   (none)\n");
        }
        else if (current->type == ast_scope_context_names_literal) {
            lstore = Lex.store(current->token_index);
            if (lstore->token == TOKEN_ATSIGN) {
                printf("| capture   (letters)\n");
            }
            if (lstore->token == TOKEN_EQUAL) {
                printf("| capture   (parent)\n");
            }
        }

        printf("| Alphabets (%lu)\n", TheContext[i].alphabet_literals_count);
        for (size_t j = 0; j < TheContext[i].alphabet_literals_count; ++j) {
            alphabet_literal *alph = TheContext[i].alphabet_literals[j];
            printf("| | mask (0x%016llx)\n", alph->letter_mask);
        }

        printf("| letters   (%lu)\n", TheContext[i].letters_count);
        for (size_t j = 0; j < TheContext[i].letters_count; ++j) {
            printf("| | size (%d) ", 
                TheContext[i].letters[j]->end - 
                TheContext[i].letters[j]->begin);
            printf("value (%.*s) ", 
                TheContext[i].letters[j]->end - 
                TheContext[i].letters[j]->begin, 
                TheContext[i].letters[j]->begin);
            printf("address (%p) \n",  
                TheContext[i].letters[j]->begin);
        }
        printf("| variables (%lu)\n", TheContext[i].variables_count);
        for (size_t j = 0; j < TheContext[i].variables_count; ++j) {
            printf("| | size (%d) ",
                TheContext[i].variables[j]->end -
                TheContext[i].variables[j]->begin);
            printf("value (%.*s) ",
                TheContext[i].variables[j]->end -
                TheContext[i].variables[j]->begin,
                TheContext[i].variables[j]->begin);
            printf("address (%p) \n",
                TheContext[i].variables[j]->begin);
        }

        printf("| algorithms (%lu)\n", TheContext[i].algorithms_count);
        for (size_t j = 0; j < TheContext[i].algorithms_count; ++j) {
            algorithm_definition *alg = TheContext[i].algorithms[j];
            if (alg->name != NULL) {
                int name_size = (int)(alg->name->end - alg->name->begin);
                printf("| | name (%.*s)\n", name_size, alg->name->begin);
            }
            if (alg->alphabet_ref != NULL) {
                lexical_store *alph_lex = Lex.store(alg->alphabet_ref->token_index);
                int alph_size = (int)(alph_lex->end - alph_lex->begin);
                printf("| | alphabet (%.*s)\n", alph_size, alph_lex->begin);
            }
            if (alg->word_param != NULL) {
                int param_size = (int)(alg->word_param->end - alg->word_param->begin);
                printf("| | word_param (%.*s)\n", param_size, alg->word_param->begin);
            }
            printf("| | rules (%lu)\n", alg->rules_count);
            for (size_t k = 0; k < alg->rules_count; ++k) {
                algorithm_rule *rule = alg->rules[k];
                if (rule->is_terminal) {
                    printf("| | | [terminal] pattern -> halt\n");
                } else {
                    printf("| | | [substitution] pattern -> replacement\n");
                }
            }
        }
        printf("\n");
    }
    return;
}

const struct context Context = {
    .validate = validate_program_context
};
