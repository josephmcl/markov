#include "context.h"

#define ALGORITHM_DEFINITIONS_SIZE 16
#define ALGORITHM_RULES_SIZE 32
#define CONTEXT_ALGORITHMS_SIZE 16

algorithm_definition *TheAlgorithmDefinition = NULL;
algorithm_rule *TheAlgorithmRule = NULL;

static algorithm_definition_info TheInfo = {0};
static size_t TheRuleCount = 0;
static size_t TheRuleCapacity = 0;

algorithm_definition *algorithm_definition_push(void) {
    size_t bytes;
    if (TheInfo.count == TheInfo.capacity) {
        TheInfo.capacity += ALGORITHM_DEFINITIONS_SIZE;
        bytes = sizeof(algorithm_definition) * TheInfo.capacity;
        TheAlgorithmDefinition =
            (algorithm_definition *) realloc(TheAlgorithmDefinition, bytes);
    }

    TheInfo.count += 1;
    return TheAlgorithmDefinition + TheInfo.count - 1;
}

algorithm_definition *algorithm_definition_current(void) {
    return &TheAlgorithmDefinition[TheInfo.count - 1];
}

algorithm_rule *algorithm_rule_push(algorithm_definition *alg) {
    size_t bytes;

    /* Grow global rule storage if needed */
    if (TheRuleCount == TheRuleCapacity) {
        TheRuleCapacity += ALGORITHM_RULES_SIZE;
        bytes = sizeof(algorithm_rule) * TheRuleCapacity;
        TheAlgorithmRule = (algorithm_rule *) realloc(TheAlgorithmRule, bytes);
    }

    algorithm_rule *rule = &TheAlgorithmRule[TheRuleCount];
    TheRuleCount += 1;

    /* Add to algorithm's rule list */
    if (alg->rules_count == alg->rules_capacity) {
        alg->rules_capacity += ALGORITHM_RULES_SIZE;
        bytes = sizeof(algorithm_rule *) * alg->rules_capacity;
        alg->rules = (algorithm_rule **) realloc(alg->rules, bytes);
    }

    alg->rules[alg->rules_count] = rule;
    alg->rules_count += 1;

    return rule;
}

algorithm_definition *_context_push_algorithm(
    program_context      *context,
    algorithm_definition *alg) {

    size_t bytes;
    if (context->algorithms_count == context->algorithms_capacity) {
        context->algorithms_capacity += CONTEXT_ALGORITHMS_SIZE;
        bytes = sizeof(algorithm_definition *) * context->algorithms_capacity;
        context->algorithms =
            (algorithm_definition **) realloc(context->algorithms, bytes);
    }

    context->algorithms[context->algorithms_count] = alg;
    context->algorithms_count += 1;
    return alg;
}

bool _context_has_algorithm(
    program_context      *context,
    algorithm_definition *alg) {

    for (size_t i = 0; i < context->algorithms_count; ++i) {
        if (context->algorithms[i] == alg) {
            return true;
        }
    }
    return false;
}

syntax_store *_update_context_algorithm(
    syntax_store         *store,
    program_context_info *info,
    program_context      *context) {

    syntax_store *topic = store;
    algorithm_definition *alg;
    size_t index;

    /* Navigate to the parent scope or program node */
    while (topic != NULL
           && topic->type != ast_scope
           && topic->type != ast_program) {
        topic = topic->topic;
    }

    if (topic == NULL) {
        printf("Error. Could not determine algorithm's topic scope.\n");
        return NULL;
    }

    /* Find the matching context */
    index = info->count;
    for (size_t i = 0; i < info->count; ++i) {
        if (topic == context[i].syntax) {
            index = i;
            break;
        }
    }

    if (index == info->count) {
        printf("Error. Could not find matching context for algorithm.\n");
        return NULL;
    }

    /* Create new algorithm definition */
    alg = algorithm_definition_push();
    alg->store = store;
    alg->context = &context[index];
    alg->rules_count = 0;
    alg->rules_capacity = 0;
    alg->rules = NULL;
    alg->alphabet = NULL;

    /* Extract algorithm components from AST:
       content[0] = algorithm name (ast_variable)
       content[1] = alphabet variable (ast_variable)
       content[2] = word parameter (ast_variable)
       content[3] = rules (ast_algorithm_rules) */

    if (store->size >= 1 && store->content[0] != NULL) {
        alg->name = Lex.store(store->content[0]->token_index);
    } else {
        alg->name = NULL;
    }

    if (store->size >= 2 && store->content[1] != NULL) {
        alg->alphabet_ref = store->content[1];
    } else {
        alg->alphabet_ref = NULL;
    }

    if (store->size >= 3 && store->content[2] != NULL) {
        alg->word_param = Lex.store(store->content[2]->token_index);
    } else {
        alg->word_param = NULL;
    }

    /* Process rules */
    if (store->size >= 4 && store->content[3] != NULL) {
        syntax_store *rules_node = store->content[3];

        for (size_t i = 0; i < rules_node->size; ++i) {
            syntax_store *rule_node = rules_node->content[i];
            if (rule_node == NULL || rule_node->type != ast_algorithm_rule) {
                continue;
            }

            algorithm_rule *rule = algorithm_rule_push(alg);
            rule->store = rule_node;

            /* Rule content[0] is always the pattern (left side)
               Rule content[1] is the replacement (right side), if present */
            if (rule_node->size >= 1) {
                rule->pattern = rule_node->content[0];
            } else {
                rule->pattern = NULL;
            }

            if (rule_node->size >= 2) {
                rule->replacement = rule_node->content[1];
                rule->is_terminal = false;
            } else {
                rule->replacement = NULL;
                rule->is_terminal = true;
            }
        }
    }

    /* Add algorithm to context */
    if (!_context_has_algorithm(&context[index], alg)) {
        _context_push_algorithm(&context[index], alg);
    }

    return NULL;
}
