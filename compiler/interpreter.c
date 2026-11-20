#include "interpreter.h"
#include "value.h"
#include "diagnostics.h"
#include "builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <errno.h>

typedef struct {
    char *name;
    Value value;
} Variable;

typedef struct {
    char *name;
    char **parameters;
    int parameter_count;
    AstNode *body;
} Function;

typedef struct {
    int returned;
    int broke;
    int continued;
    Value value;
} ExecutionResult;

typedef struct Environment Environment;

struct Environment {
    Variable *variables;
    int count;
    int capacity;
    Environment *parent;
    Function *functions;
    int function_count;
    int function_scope;
    Environment *next_free;
};

/*
 * Variable and function names are interned by the AST (see ast_intern), so
 * every lookup below compares names by pointer instead of with strcmp.
 *
 * Environments are created for every function call and every executed
 * block, so finished ones go on a free list and are reused along with their
 * variable arrays instead of being malloc'd and freed each time.
 */

static Environment *global_environment = NULL;
static Environment *free_environments = NULL;
static int runtime_argument_count = 0;
static char **runtime_arguments = NULL;

void interpreter_set_arguments(int count, char **arguments)
{
    runtime_argument_count = count;
    runtime_arguments = arguments;
}

static Environment *environment_create(Environment *parent)
{
    Environment *environment = free_environments;

    if (environment != NULL)
    {
        free_environments = environment->next_free;
    }
    else
    {
        environment = malloc(sizeof(Environment));

        if (environment == NULL)
        {
            diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
        }

        environment->variables = NULL;
        environment->capacity = 0;
    }

    environment->functions = NULL;
    environment->function_count = 0;
    environment->count = 0;
    environment->parent = parent;
    environment->function_scope = 0;
    environment->next_free = NULL;

    return environment;
}

static Environment *environment_create_function(Environment *parent)
{
    Environment *environment = environment_create(parent);
    environment->function_scope = 1;
    return environment;
}

static Variable *find_local_variable(Environment *environment, const char *name)
{
    Variable *variables = environment->variables;

    for (int i = 0; i < environment->count; i++)
    {
        if (variables[i].name == name)
        {
            return &variables[i];
        }
    }

    return NULL;
}

static Variable *find_variable(Environment *environment, const char *name)
{
    for (Environment *current = environment;
         current != NULL;
         current = current->parent)
    {
        Variable *variable = find_local_variable(current, name);

        if (variable != NULL)
        {
            return variable;
        }
    }

    return NULL;
}

static Function *find_function(
    Environment *environment,
    const char *name
)
{
    for (Environment *current = environment;
         current != NULL;
         current = current->parent)
    {
        for (int i = 0; i < current->function_count; i++)
        {
            if (current->functions[i].name == name)
            {
                return &current->functions[i];
            }
        }
    }

    return NULL;
}

/* The variable an assignment would update: searched up to the nearest
   function scope, like set_variable. */
static Variable *find_assignable_variable(
    Environment *environment,
    const char *name
)
{
    for (Environment *current = environment;
         current != NULL;
         current = current->parent)
    {
        Variable *variable = find_local_variable(current, name);

        if (variable != NULL)
        {
            return variable;
        }

        if (current->function_scope)
        {
            break;
        }
    }

    return NULL;
}

/* Adds a new variable to this environment without searching first. */
static void define_variable(
    Environment *environment,
    char *name,
    Value value
)
{
    if (environment->count == environment->capacity)
    {
        int capacity = environment->capacity ? environment->capacity * 2 : 4;
        Variable *variables = realloc(
            environment->variables,
            sizeof(Variable) * (size_t)capacity
        );

        if (variables == NULL)
        {
            diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
        }

        environment->variables = variables;
        environment->capacity = capacity;
    }

    environment->variables[environment->count].name = name;
    environment->variables[environment->count].value = value;
    environment->count++;
}

static void set_variable(
    Environment *environment,
    char *name,
    Value value
)
{
    Variable *variable = find_assignable_variable(environment, name);

    if (variable != NULL)
    {
        value_free(&variable->value);
        variable->value = value;
        return;
    }

    define_variable(environment, name, value);
}

static void environment_free(Environment *environment)
{
    for (int i = 0; i < environment->count; i++)
    {
        value_free(&environment->variables[i].value);
    }

    if (environment->functions != NULL)
    {
        free(environment->functions);
    }

    environment->count = 0;
    environment->functions = NULL;
    environment->function_count = 0;
    environment->next_free = free_environments;
    free_environments = environment;
}

static void define_function(
    Environment *environment,
    char *name,
    char **parameters,
    int parameter_count,
    AstNode *body
)
{
    Function *function = find_function(environment, name);

    if (function != NULL && function->body != body)
    {
        diagnostics_runtime_errorf("E3006", "rename one of the functions", "function '%s' is defined more than once", name);
    }

    if (function != NULL)
    {
        function->parameters = parameters;
        function->parameter_count = parameter_count;
        function->body = body;
        return;
    }

    Function *functions = realloc(
        environment->functions,
        sizeof(Function) * (environment->function_count + 1)
    );

    if (functions == NULL)
    {
        diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
    }

    environment->functions = functions;

    /* Names and parameter lists are borrowed from the AST, which outlives
       the interpreter run. */
    Function *new_function =
        &environment->functions[environment->function_count];
    new_function->name = name;
    new_function->parameters = parameters;
    new_function->parameter_count = parameter_count;
    new_function->body = body;
    environment->function_count++;
}


static const char *operator_symbol(TokenType operator)
{
    switch (operator)
    {
        case TOKEN_PLUS: return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_STAR: return "*";
        case TOKEN_SLASH: return "/";
        case TOKEN_PERCENT: return "%";
        case TOKEN_LESS: return "<";
        case TOKEN_LESS_EQUAL: return "<=";
        case TOKEN_GREATER: return ">";
        case TOKEN_GREATER_EQUAL: return ">=";
        case TOKEN_EQUAL_EQUAL: return "==";
        case TOKEN_BANG_EQUAL: return "!=";
        default: return "?";
    }
}

_Noreturn static void operand_type_error(
    const char *code,
    TokenType operator,
    ValueType left,
    ValueType right
)
{
    const char *help = "arithmetic needs two numbers; < > <= >= compare two numbers or two strings";

    if (operator == TOKEN_PLUS && (left == VALUE_STRING || right == VALUE_STRING))
    {
        help = left == right
            ? "+ joins two strings"
            : "convert the other value first, e.g. \"total: \" + to_string(count)";
    }
    else if (operator == TOKEN_PLUS)
    {
        help = "+ adds numbers, or joins two strings or two arrays";
    }

    diagnostics_runtime_errorf(
        code,
        help,
        "cannot use '%s' on %s and %s",
        operator_symbol(operator),
        value_type_name(left),
        value_type_name(right)
    );
}

_Noreturn static void index_error(long index, int count)
{
    char help[128];

    if (count == 0)
    {
        snprintf(help, sizeof(help), "the array is empty");
    }
    else
    {
        snprintf(help, sizeof(help), "valid indexes are 0 to %d", count - 1);
    }

    diagnostics_runtime_errorf(
        "E3007",
        help,
        "index %ld is out of bounds for an array of length %d",
        index,
        count
    );
}

/* Usage strings for builtins, shown when they get the wrong arguments. */
static const char *const builtin_usages[][2] = {
    {"print", "print(value, ...)"}, {"eprint", "eprint(value, ...)"},
    {"length", "length(text_array_or_map)"}, {"append", "append(array, value)"},
    {"pop", "pop(array)"}, {"insert", "insert(array, index, value)"},
    {"remove", "remove(array_or_map, index_or_key)"}, {"clear", "clear(array)"},
    {"reverse", "reverse(array)"}, {"sort", "sort(array)"},
    {"slice", "slice(array, start, end)"}, {"join", "join(array, separator)"},
    {"first", "first(array)"}, {"last", "last(array)"}, {"args", "args()"},
    {"token_at", "token_at(array, index)"},
    {"fail", "fail(message)"}, {"to_string", "to_string(value)"},
    {"chr", "chr(code)"}, {"read_file", "read_file(path)"},
    {"write_file", "write_file(path, text)"}, {"char_at", "char_at(text, index)"},
    {"substring", "substring(text, start, end)"}, {"exit", "exit(code)"}
};

static const char *builtin_usage(const char *name)
{
    for (size_t i = 0; i < sizeof(builtin_usages) / sizeof(builtin_usages[0]); i++)
    {
        if (strcmp(builtin_usages[i][0], name) == 0)
        {
            return builtin_usages[i][1];
        }
    }

    const char *usage = builtin_usage_text(name);
    return usage ? usage : name;
}

static void check_builtin_arity(AstNode *node, int expected)
{
    if (node->call.argument_count != expected)
    {
        char help[160];
        snprintf(help, sizeof(help), "usage: %s", builtin_usage(node->call.name));
        diagnostics_runtime_errorf(
            "E3003",
            help,
            "%s expects %d argument(s), got %d",
            node->call.name,
            expected,
            node->call.argument_count
        );
    }
}

/* ---- error help texts ----------------------------------------------------- */

static const char *const builtin_function_names[] = {
    "print", "length", "append", "pop", "insert", "remove", "contains",
    "index_of", "clear", "reverse", "sort", "slice", "join", "sum", "min",
    "max", "first", "last", "args", "token_at", "type_of", "fail",
    "to_string", "chr", "read_file", "write_file", "char_at", "substring",
    "eprint", "exit"
};

#define MAX_SUGGESTION_CANDIDATES 1024

static char help_buffer[512];

static const char *suggestion_help(
    const char *name,
    const char **candidates,
    int count,
    const char *fallback
)
{
    const char *closest = diagnostics_closest_name(name, candidates, count);

    if (closest == NULL)
    {
        return fallback;
    }

    snprintf(help_buffer, sizeof(help_buffer), "did you mean '%s'?", closest);
    return help_buffer;
}

static const char *undefined_variable_help(
    Environment *environment,
    const char *name
)
{
    const char *candidates[MAX_SUGGESTION_CANDIDATES];
    int count = 0;

    for (Environment *current = environment;
         current != NULL && count < MAX_SUGGESTION_CANDIDATES;
         current = current->parent)
    {
        for (int i = 0; i < current->count && count < MAX_SUGGESTION_CANDIDATES; i++)
        {
            candidates[count++] = current->variables[i].name;
        }
    }

    return suggestion_help(
        name,
        candidates,
        count,
        "assign a value to it first, e.g. name = 0"
    );
}

static const char *undefined_function_help(
    Environment *environment,
    const char *name
)
{
    const char *candidates[MAX_SUGGESTION_CANDIDATES];
    int count = 0;

    for (Environment *current = environment;
         current != NULL && count < MAX_SUGGESTION_CANDIDATES;
         current = current->parent)
    {
        for (int i = 0; i < current->function_count && count < MAX_SUGGESTION_CANDIDATES; i++)
        {
            candidates[count++] = current->functions[i].name;
        }
    }

    for (size_t i = 0;
         i < sizeof(builtin_function_names) / sizeof(builtin_function_names[0]) &&
         count < MAX_SUGGESTION_CANDIDATES;
         i++)
    {
        candidates[count++] = builtin_function_names[i];
    }

    return suggestion_help(
        name,
        candidates,
        count,
        "define it with: fun name(parameters)"
    );
}

static const char *function_signature_help(const Function *function)
{
    size_t used = (size_t)snprintf(help_buffer, sizeof(help_buffer), "'%s' is defined as %s(", function->name, function->name);

    for (int i = 0; i < function->parameter_count && used < sizeof(help_buffer); i++)
    {
        used += (size_t)snprintf(help_buffer + used, sizeof(help_buffer) - used, "%s%s", i ? ", " : "", function->parameters[i]);
    }

    if (used < sizeof(help_buffer))
    {
        snprintf(help_buffer + used, sizeof(help_buffer) - used, ")");
    }

    return help_buffer;
}

static ExecutionResult execute(
    AstNode *node,
    Environment *environment
);

static Value evaluate(
    AstNode *node,
    Environment *environment
);

_Noreturn static void key_error(const Value *map, const Value *key)
{
    if (key->type == VALUE_STRING)
    {
        const char *candidates[256];
        int count = 0;

        for (int i = 0; i < value_map_count(map) && count < 256; i++)
        {
            const Value *candidate = value_map_key_at(map, i);

            if (candidate->type == VALUE_STRING)
            {
                candidates[count++] = candidate->string;
            }
        }

        const char *closest = diagnostics_closest_name(key->string, candidates, count);
        char help[320];

        if (closest != NULL)
        {
            snprintf(help, sizeof(help), "did you mean '%s'?", closest);
        }
        else
        {
            snprintf(help, sizeof(help), "use contains(map, key) to check first, or get(map, key, default)");
        }

        diagnostics_runtime_errorf("E3023", help, "key '%s' not found in map", key->string);
    }

    diagnostics_runtime_errorf("E3023", "use contains(map, key) to check first, or get(map, key, default)", "key %ld not found in map", key->integer);
}

/*
 * Finds container[index] for arrays and maps. With mutable the container
 * is made unique first (copy-on-write); with insert a missing map key is
 * added (for m[key] = value). Consumes index.
 */
static Value *index_place(Value *container, Value index, int mutable, int insert)
{
    if (container->type == VALUE_MAP)
    {
        if (!value_is_map_key(&index))
        {
            diagnostics_runtime_errorf("E3024", NULL, "map keys must be strings or integers (got %s)", value_type_name(index.type));
        }

        if (insert)
        {
            return value_map_slot(container, index);
        }

        if (value_map_find(container, &index) == NULL)
        {
            key_error(container, &index);
        }

        if (mutable)
        {
            value_map_make_unique(container);
        }

        Value *found = value_map_find(container, &index);
        value_free(&index);
        return found;
    }

    if (container->type != VALUE_ARRAY)
    {
        diagnostics_runtime_errorf("E3008", NULL, "indexing requires an array or map (got %s)", value_type_name(container->type));
    }

    if (index.type != VALUE_INT)
    {
        diagnostics_runtime_errorf("E3009", NULL, "array index must be an integer (got %s)", value_type_name(index.type));
    }

    if (index.integer < 0 || index.integer >= container->array.count)
    {
        index_error(index.integer, container->array.count);
    }

    if (mutable)
    {
        value_array_make_unique(container);
    }

    return &container->array.elements[index.integer];
}

static Value *resolve_array_element(
    AstNode *node,
    Environment *environment,
    int mutable
)
{
    if (node->type == AST_VARIABLE_REFERENCE)
    {
        Variable *variable = find_variable(
            environment,
            node->variable_reference.name
        );

        if (variable == NULL)
        {
            diagnostics_runtime_errorf("E3001", undefined_variable_help(environment, node->variable_reference.name), "variable '%s' is not defined", node->variable_reference.name);
        }

        return &variable->value;
    }

    if (node->type == AST_INDEX)
    {
        Value index = evaluate(node->index.index, environment);

        Value *container = resolve_array_element(
            node->index.array,
            environment,
            mutable
        );

        return index_place(container, index, mutable, 0);
    }

    diagnostics_runtime_errorf("E3011", "Pass an array value.", "array assignment requires an array expression.\n");
}


static Value *require_array_argument(
    AstNode *node,
    Environment *environment,
    const char *name
)
{
    if (node->type != AST_VARIABLE_REFERENCE &&
        node->type != AST_INDEX)
    {
        diagnostics_runtime_errorf("E3010", "store the array in a variable first, then pass the variable", "%s needs a variable or indexed variable as its first argument", name);
    }

    Value *value = resolve_array_element(node, environment, 1);

    if (value->type != VALUE_ARRAY)
    {
        diagnostics_runtime_errorf("E3011", NULL, "%s requires an array (got %s)", name, value_type_name(value->type));
    }

    value_array_make_unique(value);

    return value;
}

static long require_integer_value(
    Value *value,
    const char *name
)
{
    if (value->type != VALUE_INT)
    {
        diagnostics_runtime_errorf("E3011", "Pass an integer index.", "%s requires an integer index.", name
        );
        value_free(value);
    }

    return value->integer;
}

static void check_array_index(
    const Value *array,
    long index,
    const char *name,
    int allow_end
)
{
    long upper = allow_end ? array->array.count : array->array.count - 1;

    if (index < 0 || index > upper)
    {
        diagnostics_runtime_errorf("E3007", "Use an index within the valid range.", "%s index out of bounds.", name);
    }
}

static Value evaluate_array_builtin(
    AstNode *node,
    Environment *environment
)
{
    const char *name = node->call.name;

    if (strcmp(name, "length") == 0)
    {
        check_builtin_arity(node, 1);

        AstNode *argument = node->call.arguments[0];
        if (argument->type == AST_VARIABLE_REFERENCE || argument->type == AST_INDEX)
        {
            Value *value = resolve_array_element(argument, environment, 0);
            if (value->type == VALUE_ARRAY) return value_int(value->array.count);
            if (value->type == VALUE_STRING) return value_int((long)value_string_length(value));
            if (value->type == VALUE_MAP) return value_int(value_map_count(value));
            diagnostics_runtime_errorf("E3011", NULL, "length requires a string, array or map (got %s)", value_type_name(value->type));
        }

        Value value = evaluate(argument, environment);
        if (value.type == VALUE_ARRAY)
        {
            int count = value.array.count;
            value_free(&value);
            return value_int(count);
        }
        if (value.type == VALUE_STRING || value.type == VALUE_MAP)
        {
            long count = value.type == VALUE_MAP ? value_map_count(&value) : (long)value_string_length(&value);
            value_free(&value);
            return value_int(count);
        }
        value_free(&value);
        diagnostics_runtime_errorf("E3011", NULL, "length requires a string, array or map (got %s)", value_type_name(value.type));
    }

    if (strcmp(name, "append") == 0)
    {
        check_builtin_arity(node, 2);

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "append"
        );

        Value value = evaluate(node->call.arguments[1], environment);

        value_array_reserve(array, array->array.count + 1);
        array->array.elements[array->array.count] = value;
        array->array.count++;

        return value_int(0);
    }

    if (strcmp(name, "pop") == 0)
    {
        check_builtin_arity(node, 1);

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "pop"
        );

        if (array->array.count == 0)
        {
            diagnostics_runtime_errorf("E3012", "check length(array) > 0 first", "pop cannot remove from an empty array.\n");
        }

        value_array_make_unique(array);
        int index = array->array.count - 1;
        Value result = array->array.elements[index];
        array->array.count--;

        return result;
    }

    if (strcmp(name, "insert") == 0)
    {
        check_builtin_arity(node, 3);

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "insert"
        );

        Value index_value = evaluate(node->call.arguments[1], environment);
        long index = require_integer_value(&index_value, "insert");
        check_array_index(array, index, "insert", 1);

        Value value = evaluate(node->call.arguments[2], environment);

        value_array_reserve(array, array->array.count + 1);

        for (int i = array->array.count; i > index; i--)
        {
            array->array.elements[i] = array->array.elements[i - 1];
        }

        array->array.elements[index] = value;
        array->array.count++;

        return value_int(0);
    }

    if (strcmp(name, "remove") == 0)
    {
        check_builtin_arity(node, 2);

        AstNode *target_node = node->call.arguments[0];

        if ((target_node->type == AST_VARIABLE_REFERENCE || target_node->type == AST_INDEX) &&
            resolve_array_element(target_node, environment, 0)->type == VALUE_MAP)
        {
            Value key = evaluate(node->call.arguments[1], environment);
            Value *map = resolve_array_element(target_node, environment, 1);
            Value removed;

            if (!value_map_remove(map, &key, &removed))
            {
                key_error(map, &key);
            }

            value_free(&key);
            return removed;
        }

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "remove"
        );

        Value index_value = evaluate(node->call.arguments[1], environment);
        long index = require_integer_value(&index_value, "remove");
        check_array_index(array, index, "remove", 0);
        value_array_make_unique(array);

        Value result = array->array.elements[index];

        for (int i = (int)index; i < array->array.count - 1; i++)
        {
            array->array.elements[i] = array->array.elements[i + 1];
        }

        array->array.count--;

        return result;
    }

    if (strcmp(name, "clear") == 0)
    {
        check_builtin_arity(node, 1);

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "clear"
        );

        value_array_make_unique(array);

        for (int i = 0; i < array->array.count; i++)
        {
            value_free(&array->array.elements[i]);
        }

        array->array.count = 0;

        return value_int(0);
    }

    if (strcmp(name, "reverse") == 0)
    {
        check_builtin_arity(node, 1);

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "reverse"
        );

        for (int left = 0, right = array->array.count - 1;
             left < right;
             left++, right--)
        {
            Value temporary = array->array.elements[left];
            array->array.elements[left] = array->array.elements[right];
            array->array.elements[right] = temporary;
        }

        return value_int(0);
    }

    if (strcmp(name, "slice") == 0)
    {
        check_builtin_arity(node, 3);

        Value array = evaluate(node->call.arguments[0], environment);
        Value start_value = evaluate(node->call.arguments[1], environment);
        Value end_value = evaluate(node->call.arguments[2], environment);

        if (array.type != VALUE_ARRAY)
        {
            value_free(&array);
            value_free(&start_value);
            value_free(&end_value);
            diagnostics_runtime_errorf("E3011", "Pass an array value.", "slice requires an array.\n");
        }

        long start = require_integer_value(&start_value, "slice");
        long end = require_integer_value(&end_value, "slice");

        if (start < 0 || end < start || end > array.array.count)
        {
            value_free(&array);
            diagnostics_runtime_errorf("E3007", "Use indices within the array bounds.", "slice indices are out of bounds.");
        }

        Value result = value_array((int)(end - start));

        for (long i = start; i < end; i++)
        {
            result.array.elements[i - start] =
                value_copy(&array.array.elements[i]);
        }

        value_free(&array);
        return result;
    }

    if (strcmp(name, "join") == 0)
    {
        check_builtin_arity(node, 2);

        Value array = evaluate(node->call.arguments[0], environment);
        Value separator = evaluate(node->call.arguments[1], environment);

        if (array.type != VALUE_ARRAY || separator.type != VALUE_STRING)
        {
            value_free(&array);
            value_free(&separator);
            diagnostics_runtime_errorf("E3011", "Pass an array value.", "join requires an array and a string separator.\n");
        }

        size_t total = 1;
        size_t separator_length = value_string_length(&separator);

        for (int i = 0; i < array.array.count; i++)
        {
            if (array.array.elements[i].type != VALUE_STRING)
            {
                value_free(&array);
                value_free(&separator);
                diagnostics_runtime_errorf("E3011", "Pass an array value.", "join requires an array of strings.\n");
            }

            total += value_string_length(&array.array.elements[i]);

            if (i > 0)
            {
                total += separator_length;
            }
        }

        char *result = malloc(total);

        if (result == NULL)
        {
            value_free(&array);
            value_free(&separator);
            diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
        }

        result[0] = '\0';

        for (int i = 0; i < array.array.count; i++)
        {
            if (i > 0)
            {
                strcat(result, separator.string);
            }

            strcat(result, array.array.elements[i].string);
        }

        Value value = value_string(result);
        free(result);
        value_free(&array);
        value_free(&separator);
        return value;
    }

    if (strcmp(name, "first") == 0 || strcmp(name, "last") == 0)
    {
        check_builtin_arity(node, 1);

        Value array = evaluate(node->call.arguments[0], environment);

        if (array.type != VALUE_ARRAY)
        {
            value_free(&array);
            diagnostics_runtime_errorf("E3011", "Pass an array value.", "first/last requires an array.\n");
        }

        if (array.array.count == 0)
        {
            value_free(&array);
            diagnostics_runtime_errorf("E3012", "check length(array) > 0 first", "first/last cannot be used on an empty array.\n");
        }

        int index = strcmp(name, "first") == 0 ? 0 : array.array.count - 1;
        Value result = value_copy(&array.array.elements[index]);
        value_free(&array);
        return result;
    }

    if (strcmp(name, "sort") == 0)
    {
        check_builtin_arity(node, 1);

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "sort"
        );

        builtin_sort(array);

        return value_int(0);
    }

    return value_int(0);
}

static Value evaluate_runtime_builtin(
    AstNode *node,
    Environment *environment
)
{
    const char *name = node->call.name;

    if (strcmp(name, "token_at") == 0)
    {
        check_builtin_arity(node, 2);
        AstNode *array_node = node->call.arguments[0];
        Value temporary_array = value_none();
        Value *array = NULL;
        if (array_node->type == AST_VARIABLE_REFERENCE || array_node->type == AST_INDEX)
        {
            array = resolve_array_element(array_node, environment, 0);
        }
        else
        {
            temporary_array = evaluate(array_node, environment);
            array = &temporary_array;
        }
        Value index_value = evaluate(node->call.arguments[1], environment);
        if (array->type != VALUE_ARRAY || index_value.type != VALUE_INT)
        {
            diagnostics_runtime_errorf("E3007", "usage: token_at(array, index)", "token_at expects an array and an integer index");
        }
        long index = index_value.integer;
        if (index < 0 || index >= array->array.count)
        {
            index_error(index, array->array.count);
        }
        Value result = value_copy(&array->array.elements[index]);
        value_free(&temporary_array);
        return result;
    }

    if (strcmp(name, "args") == 0)
    {
        check_builtin_arity(node, 0);

        Value result = value_array(runtime_argument_count);
        for (int i = 0; i < runtime_argument_count; i++)
        {
            result.array.elements[i] = value_string(runtime_arguments[i]);
        }
        return result;
    }

    if (strcmp(name, "eprint") == 0)
    {
        for (int i = 0; i < node->call.argument_count; i++)
        {
            Value argument = evaluate(node->call.arguments[i], environment);
            fflush(stdout);
            value_fprint(stderr, &argument);
            value_free(&argument);
        }

        return value_int(0);
    }

    if (strcmp(name, "exit") == 0)
    {
        check_builtin_arity(node, 1);

        Value code = evaluate(node->call.arguments[0], environment);

        if (code.type != VALUE_INT)
        {
            diagnostics_runtime_errorf("E3011", "pass an integer exit code, e.g. exit(1)", "exit requires an integer");
        }

        fflush(stdout);
        exit((int)code.integer);
    }

    if (strcmp(name, "fail") == 0)
    {
        check_builtin_arity(node, 1);
        Value message = evaluate(node->call.arguments[0], environment);
        if (message.type != VALUE_STRING)
        {
            value_free(&message);
            diagnostics_runtime_errorf("E3011", "Pass a string error message.", "fail requires a string.");
        }
        diagnostics_runtime_errorf("E3022", NULL, "%s", message.string);
    }

    if (strcmp(name, "to_string") == 0)
    {
        check_builtin_arity(node, 1);

        Value value = evaluate(node->call.arguments[0], environment);
        char buffer[64];
        const char *text = NULL;

        if (value.type == VALUE_STRING)
        {
            Value result = value_string(value.string);
            value_free(&value);
            return result;
        }
        else if (value.type == VALUE_INT)
        {
            snprintf(buffer, sizeof(buffer), "%ld", value.integer);
            text = buffer;
        }
        else if (value.type == VALUE_BOOL)
        {
            text = value.boolean ? "true" : "false";
        }
        else if (value.type == VALUE_NONE)
        {
            text = "none";
        }
        else
        {
            char *rendered = value_to_text(&value);
            Value result = value_string(rendered);
            free(rendered);
            value_free(&value);
            return result;
        }

        Value result = value_string(text);
        value_free(&value);
        return result;
    }

    if (strcmp(name, "chr") == 0)
    {
        check_builtin_arity(node, 1);

        Value code_value = evaluate(node->call.arguments[0], environment);
        if (code_value.type != VALUE_INT || code_value.integer < 0 || code_value.integer > 127)
        {
            if (code_value.type != VALUE_INT)
            {
                diagnostics_runtime_errorf("E3011", NULL, "chr requires an integer (got %s)", value_type_name(code_value.type));
            }

            diagnostics_runtime_errorf("E3019", "chr supports ASCII codes 0 through 127", "chr code %ld is out of range", code_value.integer);
        }

        char character[2] = { (char)code_value.integer, '\0' };
        value_free(&code_value);
        return value_string(character);
    }

    if (strcmp(name, "read_file") == 0)
    {
        check_builtin_arity(node, 1);

        Value path = evaluate(node->call.arguments[0], environment);
        if (path.type != VALUE_STRING)
        {
            value_free(&path);
            diagnostics_runtime_errorf("E3011", "Pass a string path.", "read_file requires a string path.");
        }

        FILE *file = fopen(path.string, "rb");
        if (file == NULL)
        {
            diagnostics_runtime_errorf("E3020", "check that the file exists and that the path is relative to where you run the program", "read_file could not open '%s'.", path.string);
        }

        if (fseek(file, 0, SEEK_END) != 0)
        {
            fclose(file);
            diagnostics_runtime_errorf("E3021", NULL, "read_file could not read '%s'", path.string);
            value_free(&path);
        }

        long file_size = ftell(file);
        if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0)
        {
            fclose(file);
            diagnostics_runtime_errorf("E3021", NULL, "read_file could not read '%s'", path.string);
            value_free(&path);
        }

        char *buffer = malloc((size_t)file_size + 1);
        if (buffer == NULL)
        {
            fclose(file);
            value_free(&path);
            diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the input size.");
        }

        size_t bytes_read = fread(buffer, 1, (size_t)file_size, file);
        if (ferror(file))
        {
            free(buffer);
            fclose(file);
            diagnostics_runtime_errorf("E3021", NULL, "read_file could not read '%s'", path.string);
            value_free(&path);
        }

        buffer[bytes_read] = '\0';
        fclose(file);
        value_free(&path);
        Value result = value_string(buffer);
        free(buffer);
        return result;
    }

    if (strcmp(name, "write_file") == 0)
    {
        check_builtin_arity(node, 2);

        Value path = evaluate(node->call.arguments[0], environment);
        Value content = evaluate(node->call.arguments[1], environment);
        if (path.type != VALUE_STRING || content.type != VALUE_STRING)
        {
            value_free(&path);
            value_free(&content);
            diagnostics_runtime_errorf("E3011", "Pass string arguments.", "write_file requires a path and content, both strings.");
        }

        FILE *file = fopen(path.string, "wb");
        if (file == NULL)
        {
            value_free(&path);
            value_free(&content);
            return value_bool(0);
        }

        size_t length = value_string_length(&content);
        size_t written = fwrite(content.string, 1, length, file);
        int close_result = fclose(file);
        int success = written == length && close_result == 0;
        value_free(&path);
        value_free(&content);
        return value_bool(success);
    }

    if (strcmp(name, "char_at") == 0)
    {
        check_builtin_arity(node, 2);

        AstNode *text_node = node->call.arguments[0];
        Value temporary_text = value_none();
        Value *text = NULL;
        if (text_node->type == AST_VARIABLE_REFERENCE || text_node->type == AST_INDEX)
        {
            text = resolve_array_element(text_node, environment, 0);
        }
        else
        {
            temporary_text = evaluate(text_node, environment);
            text = &temporary_text;
        }
        Value index_value = evaluate(node->call.arguments[1], environment);
        if (text->type != VALUE_STRING || index_value.type != VALUE_INT)
        {
            value_free(&temporary_text);
            value_free(&index_value);
            diagnostics_runtime_errorf("E3011", "Pass a string and integer index.", "char_at expects (string, integer).");
        }

        long index = index_value.integer;
        size_t length = value_string_length(text);
        value_free(&index_value);
        if (index < 0 || (size_t)index >= length)
        {
            value_free(&temporary_text);
            return value_string("");
        }

        char character[2] = { text->string[index], '\0' };
        Value result = value_string(character);
        value_free(&temporary_text);
        return result;
    }

    if (strcmp(name, "substring") == 0)
    {
        check_builtin_arity(node, 3);

        Value text = evaluate(node->call.arguments[0], environment);
        Value start_value = evaluate(node->call.arguments[1], environment);
        Value end_value = evaluate(node->call.arguments[2], environment);
        if (text.type != VALUE_STRING || start_value.type != VALUE_INT || end_value.type != VALUE_INT)
        {
            value_free(&text);
            value_free(&start_value);
            value_free(&end_value);
            diagnostics_runtime_errorf("E3011", "Pass (string, integer, integer).", "substring expects a string and two integer indices.");
        }

        long start = start_value.integer;
        long end = end_value.integer;
        size_t length = value_string_length(&text);
        value_free(&start_value);
        value_free(&end_value);
        if (start < 0 || end < start || (size_t)end > length)
        {
            value_free(&text);
            diagnostics_runtime_errorf("E3007", "Use an end-exclusive range inside the string.", "substring indices are out of bounds.");
        }

        size_t result_length = (size_t)(end - start);
        char *buffer = malloc(result_length + 1);
        if (buffer == NULL)
        {
            value_free(&text);
            diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the input size.");
        }
        memcpy(buffer, text.string + start, result_length);
        buffer[result_length] = '\0';
        Value result = value_string(buffer);
        free(buffer);
        value_free(&text);
        return result;
    }

    return value_none();
}

enum
{
    CALL_RUNTIME_BUILTIN = 1,
    CALL_ARRAY_BUILTIN,
    CALL_PRINT,
    CALL_PURE_BUILTIN,
    CALL_USER_FUNCTION
};

static int is_runtime_builtin(const char *name);

static int classify_call(const char *name)
{
    static const char *const array_builtins[] = {
        "length", "append", "pop", "insert", "remove",
        "clear", "reverse", "sort", "slice", "join", "first", "last"
    };

    if (is_runtime_builtin(name))
    {
        return CALL_RUNTIME_BUILTIN;
    }

    if (builtin_exists(name))
    {
        return CALL_PURE_BUILTIN;
    }

    for (size_t i = 0; i < sizeof(array_builtins) / sizeof(array_builtins[0]); i++)
    {
        if (strcmp(name, array_builtins[i]) == 0)
        {
            return CALL_ARRAY_BUILTIN;
        }
    }

    if (strcmp(name, "print") == 0)
    {
        return CALL_PRINT;
    }

    return CALL_USER_FUNCTION;
}

static int is_runtime_builtin(const char *name)
{
    return strcmp(name, "args") == 0 ||
           strcmp(name, "token_at") == 0 ||
           strcmp(name, "fail") == 0 ||
           strcmp(name, "to_string") == 0 ||
           strcmp(name, "chr") == 0 ||
           strcmp(name, "read_file") == 0 ||
           strcmp(name, "write_file") == 0 ||
           strcmp(name, "char_at") == 0 ||
           strcmp(name, "substring") == 0 ||
           strcmp(name, "eprint") == 0 ||
           strcmp(name, "exit") == 0;
}

static Value evaluate_index(
    AstNode *array_node,
    AstNode *index_node,
    Environment *environment
)
{
    Value container = evaluate(array_node, environment);
    Value index = evaluate(index_node, environment);

    /* Strings can be indexed for reading: "abc"[1] is "b". */
    if (container.type == VALUE_STRING && index.type == VALUE_INT)
    {
        long length = (long)value_string_length(&container);

        if (index.integer < 0 || index.integer >= length)
        {
            diagnostics_runtime_errorf("E3007", length ? NULL : "the string is empty", "index %ld is out of bounds for a string of length %ld", index.integer, length);
        }

        Value result = value_string_n(container.string + index.integer, 1);
        value_free(&container);
        return result;
    }

    Value result = value_copy(index_place(&container, index, 0, 0));
    value_free(&container);
    return result;
}

static int is_number(const Value *value)
{
    return value->type == VALUE_INT || value->type == VALUE_FLOAT;
}

static double number_of(const Value *value)
{
    return value->type == VALUE_INT ? (double)value->integer : value->number;
}

/* Applies a binary operator (other than and/or) to evaluated operands. */
static Value binary_values(AstNode *node, Value left, Value right)
{
    TokenType operator = node->binary.operator;

    if (operator == TOKEN_EQUAL_EQUAL || operator == TOKEN_BANG_EQUAL)
    {
        int equal = value_equal(&left, &right);
        value_free(&left);
        value_free(&right);
        return value_bool(operator == TOKEN_EQUAL_EQUAL ? equal : !equal);
    }

    if (left.type == VALUE_INT && right.type == VALUE_INT)
    {
        long a = left.integer;
        long b = right.integer;

        switch (operator)
        {
            case TOKEN_PLUS: return value_int(a + b);
            case TOKEN_MINUS: return value_int(a - b);
            case TOKEN_STAR: return value_int(a * b);
            case TOKEN_SLASH:
                if (b == 0) diagnostics_runtime_errorf("E3013", "check that the divisor is not zero before dividing", "division by zero");
                return value_int(a / b);
            case TOKEN_PERCENT:
                if (b == 0) diagnostics_runtime_errorf("E3014", "check that the divisor is not zero before using %", "modulo by zero");
                return value_int(a % b);
            case TOKEN_LESS: return value_bool(a < b);
            case TOKEN_LESS_EQUAL: return value_bool(a <= b);
            case TOKEN_GREATER: return value_bool(a > b);
            case TOKEN_GREATER_EQUAL: return value_bool(a >= b);
            default: break;
        }
    }

    if (is_number(&left) && is_number(&right))
    {
        double a = number_of(&left);
        double b = number_of(&right);

        switch (operator)
        {
            case TOKEN_PLUS: return value_float(a + b);
            case TOKEN_MINUS: return value_float(a - b);
            case TOKEN_STAR: return value_float(a * b);
            case TOKEN_SLASH:
                if (b == 0) diagnostics_runtime_errorf("E3013", "check that the divisor is not zero before dividing", "division by zero");
                return value_float(a / b);
            case TOKEN_PERCENT:
                if (b == 0) diagnostics_runtime_errorf("E3014", "check that the divisor is not zero before using %", "modulo by zero");
                return value_float(fmod(a, b));
            case TOKEN_LESS: return value_bool(a < b);
            case TOKEN_LESS_EQUAL: return value_bool(a <= b);
            case TOKEN_GREATER: return value_bool(a > b);
            case TOKEN_GREATER_EQUAL: return value_bool(a >= b);
            default: break;
        }
    }

    if (left.type == VALUE_STRING && right.type == VALUE_STRING)
    {
        if (operator == TOKEN_PLUS)
        {
            value_string_append(&left, &right);
            value_free(&right);
            return left;
        }

        int order = strcmp(left.string, right.string);
        int result = -1;

        switch (operator)
        {
            case TOKEN_LESS: result = order < 0; break;
            case TOKEN_LESS_EQUAL: result = order <= 0; break;
            case TOKEN_GREATER: result = order > 0; break;
            case TOKEN_GREATER_EQUAL: result = order >= 0; break;
            default: break;
        }

        if (result >= 0)
        {
            value_free(&left);
            value_free(&right);
            return value_bool(result);
        }
    }

    /* [1, 2] + [3] is [1, 2, 3]. */
    if (operator == TOKEN_PLUS && left.type == VALUE_ARRAY && right.type == VALUE_ARRAY)
    {
        Value joined = value_array(left.array.count + right.array.count);

        for (int i = 0; i < left.array.count; i++)
        {
            joined.array.elements[i] = value_copy(&left.array.elements[i]);
        }

        for (int i = 0; i < right.array.count; i++)
        {
            joined.array.elements[left.array.count + i] = value_copy(&right.array.elements[i]);
        }

        value_free(&left);
        value_free(&right);
        return joined;
    }

    operand_type_error("E3018", operator, left.type, right.type);
}

static Value evaluate_general(AstNode *node, Environment *environment);

/*
 * Hot-path evaluator: handles the most frequent nodes (integers, variable
 * reads, integer arithmetic and comparisons) in a small function, and hands
 * everything else to evaluate_general.
 */
static Value evaluate(
    AstNode *node,
    Environment *environment
)
{
    switch (node->type)
    {
        case AST_INTEGER:
            return value_int(node->integer.value);

        case AST_VARIABLE_REFERENCE:
        {
            Variable *variable = find_variable(
                environment,
                node->variable_reference.name
            );

            if (variable != NULL)
            {
                return value_copy(&variable->value);
            }

            break;
        }

        case AST_BINARY:
        {
            TokenType operator = node->binary.operator;

            if (operator == TOKEN_AND || operator == TOKEN_OR)
            {
                break;
            }

            Value left = evaluate(node->binary.left, environment);
            Value right = evaluate(node->binary.right, environment);

            if (left.type == VALUE_INT && right.type == VALUE_INT)
            {
                long a = left.integer;
                long b = right.integer;

                switch (operator)
                {
                    case TOKEN_PLUS: return value_int(a + b);
                    case TOKEN_MINUS: return value_int(a - b);
                    case TOKEN_STAR: return value_int(a * b);
                    case TOKEN_EQUAL_EQUAL: return value_bool(a == b);
                    case TOKEN_BANG_EQUAL: return value_bool(a != b);
                    case TOKEN_LESS: return value_bool(a < b);
                    case TOKEN_LESS_EQUAL: return value_bool(a <= b);
                    case TOKEN_GREATER: return value_bool(a > b);
                    case TOKEN_GREATER_EQUAL: return value_bool(a >= b);
                    default: break;
                }
            }

            return binary_values(node, left, right);
        }

        default:
            break;
    }

    return evaluate_general(node, environment);
}

static Value evaluate_general(
    AstNode *node,
    Environment *environment
)
{
    if (node->type == AST_FLOAT)
    {
        return value_float(node->floating.value);
    }

    if (node->type == AST_MAP)
    {
        Value map = value_map_new(node->map.count);

        for (int i = 0; i < node->map.count; i++)
        {
            Value key = evaluate(node->map.keys[i], environment);

            if (!value_is_map_key(&key))
            {
                diagnostics_runtime_errorf("E3024", NULL, "map keys must be strings or integers (got %s)", value_type_name(key.type));
            }

            Value value = evaluate(node->map.values[i], environment);
            value_map_set(&map, key, value);
        }

        return map;
    }

    if (node->type == AST_IMPORT)
    {
        return builtin_import(node->import.path);
    }

    if (node->type == AST_STRING)
    {
        return value_string(node->string.value);
    }

    if (node->type == AST_INTEGER)
    {
        return value_int(node->integer.value);
    }
    
    if (node->type == AST_BOOLEAN)
    {
        return value_bool(node->boolean.value);
    }

    if (node->type == AST_NONE)
    {
        return value_none();
    }

    if (node->type == AST_ARRAY)
    {
        Value array = value_array(node->array.count);

        for (int i = 0; i < node->array.count; i++)
        {
            array.array.elements[i] = evaluate(
                node->array.elements[i],
                environment
            );
        }

        return array;
    }

    if (node->type == AST_INDEX)
    {
        return evaluate_index(
            node->index.array,
            node->index.index,
            environment
        );
    }
    
    if (node->type == AST_UNARY)
    {
        Value operand = evaluate(
            node->unary.operand,
            environment
        );

        switch (node->unary.operator)
        {
            case TOKEN_NOT:
                if (operand.type != VALUE_BOOL)
                {
                    diagnostics_runtime_errorf("E3016", NULL, "not requires a boolean (got %s)", value_type_name(operand.type));
                }

                return value_bool(!operand.boolean);

            case TOKEN_MINUS:
                if (operand.type == VALUE_FLOAT)
                {
                    return value_float(-operand.number);
                }

                if (operand.type != VALUE_INT)
                {
                    diagnostics_runtime_errorf("E3017", NULL, "unary minus requires a number (got %s)", value_type_name(operand.type));
                }

                return value_int(-operand.integer);

            default:
                diagnostics_runtime_errorf("E9001", "Use a supported unary operator.", "unknown unary operator.\n");
        }
    }

    if (node->type == AST_VARIABLE_REFERENCE)
    {
        Variable *variable = find_variable(
            environment,
            node->variable_reference.name
        );

        if (variable == NULL)
        {
            diagnostics_runtime_errorf("E3001", undefined_variable_help(environment, node->variable_reference.name), "variable '%s' is not defined", node->variable_reference.name);
        }

        return value_copy(&variable->value);
    }

    if (node->type == AST_CALL)
    {
        /* Resolve which kind of call this is once, then reuse it. */
        if (node->call.kind == 0)
        {
            node->call.kind = classify_call(node->call.name);
        }

        if (node->call.kind == CALL_RUNTIME_BUILTIN)
        {
            return evaluate_runtime_builtin(node, environment);
        }

        if (node->call.kind == CALL_PURE_BUILTIN)
        {
            int count = node->call.argument_count;
            Value arguments[8];

            builtin_check_arity(node->call.name, count);

            for (int i = 0; i < count; i++)
            {
                arguments[i] = evaluate(node->call.arguments[i], environment);
            }

            return builtin_call(node->call.name, arguments, count);
        }

        if (node->call.kind == CALL_ARRAY_BUILTIN)
        {
            return evaluate_array_builtin(node, environment);
        }

        if (node->call.kind == CALL_USER_FUNCTION)
        {
            goto user_function;
        }

        if (strcmp(node->call.name, "length") == 0 ||
            strcmp(node->call.name, "append") == 0 ||
            strcmp(node->call.name, "pop") == 0 ||
            strcmp(node->call.name, "insert") == 0 ||
            strcmp(node->call.name, "remove") == 0 ||
            strcmp(node->call.name, "contains") == 0 ||
            strcmp(node->call.name, "index_of") == 0 ||
            strcmp(node->call.name, "clear") == 0 ||
            strcmp(node->call.name, "reverse") == 0 ||
            strcmp(node->call.name, "sort") == 0 ||
            strcmp(node->call.name, "slice") == 0 ||
            strcmp(node->call.name, "join") == 0 ||
            strcmp(node->call.name, "sum") == 0 ||
            strcmp(node->call.name, "min") == 0 ||
            strcmp(node->call.name, "max") == 0 ||
            strcmp(node->call.name, "first") == 0 ||
            strcmp(node->call.name, "last") == 0)
        {
            return evaluate_array_builtin(node, environment);
        }

        if (strcmp(node->call.name, "print") == 0)
        {
            for (int i = 0; i < node->call.argument_count; i++)
            {
                Value argument = evaluate(
                    node->call.arguments[i],
                    environment
                );

                value_print(&argument);

                value_free(&argument);
            }

            return value_int(0);
        }

    user_function:;
        Function *function = find_function(
            environment,
            node->call.name
        );

        if (function == NULL)
        {
            diagnostics_runtime_errorf("E3002", undefined_function_help(environment, node->call.name), "function '%s' is not defined", node->call.name);
        }

        if (function->parameter_count != node->call.argument_count)
        {
            diagnostics_runtime_errorf("E3003", function_signature_help(function), "function '%s' expects %d argument(s), got %d", node->call.name, function->parameter_count, node->call.argument_count);
        }

        /* Lexical scoping: a function sees its own locals and the globals,
           never the local variables of whoever called it. */
        Environment *function_environment =
            environment_create_function(global_environment);

        for (int i = 0; i < function->parameter_count; i++)
        {
            Value argument = evaluate(
                node->call.arguments[i],
                environment
            );

            set_variable(
                function_environment,
                function->parameters[i],
                argument
            );
        }

        int call_line = diagnostics_line;
        diagnostics_push_frame(function->name, call_line);

        ExecutionResult function_result = execute(
            function->body,
            function_environment
        );

        diagnostics_pop_frame();
        diagnostics_line = call_line;

        environment_free(function_environment);

        if (function_result.broke || function_result.continued)
        {
            diagnostics_runtime_errorf("E3004", "break and continue can only be used inside a while loop", "break or continue outside a loop");
        }

        return function_result.value;
    }

    if (node->type == AST_BINARY)
    {
        if (node->binary.operator == TOKEN_AND)
        {
            Value left = evaluate(
                node->binary.left,
                environment
            );

            if (left.type != VALUE_BOOL)
            {
                diagnostics_runtime_errorf("E3016", NULL, "and requires booleans (got %s)", value_type_name(left.type));
            }

            if (!left.boolean)
            {
                return value_bool(0);
            }

            Value right = evaluate(
                node->binary.right,
                environment
            );

            if (right.type != VALUE_BOOL)
            {
                diagnostics_runtime_errorf("E3016", NULL, "and requires booleans (got %s)", value_type_name(right.type));
            }

            return value_bool(right.boolean);
        }

        if (node->binary.operator == TOKEN_OR)
        {
            Value left = evaluate(
                node->binary.left,
                environment
            );

            if (left.type != VALUE_BOOL)
            {
                diagnostics_runtime_errorf("E3016", NULL, "or requires booleans (got %s)", value_type_name(left.type));
            }

            if (left.boolean)
            {
                return value_bool(1);
            }

            Value right = evaluate(
                node->binary.right,
                environment
            );

            if (right.type != VALUE_BOOL)
            {
                diagnostics_runtime_errorf("E3016", NULL, "or requires booleans (got %s)", value_type_name(right.type));
            }

            return value_bool(right.boolean);
        }

        Value left = evaluate(
            node->binary.left,
            environment
        );

        Value right = evaluate(
            node->binary.right,
            environment
        );

        return binary_values(node, left, right);
    }

    diagnostics_runtime_errorf("E9001", "Provide a valid expression.", "invalid expression.\n");
}

/* Runs one iteration of a for loop body with the loop variable bound. */
static int run_for_body(AstNode *node, Environment *environment, Value item, ExecutionResult *result)
{
    Environment *block = environment_create(environment);
    define_variable(block, node->for_statement.variable, item);
    *result = execute(node->for_statement.body, block);
    environment_free(block);

    if (result->returned)
    {
        return 0;
    }

    if (result->broke)
    {
        result->broke = 0;
        return 0;
    }

    result->continued = 0;
    return 1;
}

static long range_argument(Value value)
{
    if (value.type != VALUE_INT)
    {
        diagnostics_runtime_errorf("E3011", "usage: range(end), range(start, end) or range(start, end, step)", "range requires integers (got %s)", value_type_name(value.type));
    }

    return value.integer;
}

static ExecutionResult execute_for(AstNode *node, Environment *environment)
{
    ExecutionResult result = { 0, 0, 0, value_int(0) };
    AstNode *iterable = node->for_statement.iterable;

    /* for i in range(...) counts directly instead of building an array. */
    if (iterable->type == AST_CALL && strcmp(iterable->call.name, "range") == 0 &&
        iterable->call.argument_count >= 1 && iterable->call.argument_count <= 3)
    {
        long start = 0, end, step = 1;
        int count = iterable->call.argument_count;

        if (count == 1)
        {
            end = range_argument(evaluate(iterable->call.arguments[0], environment));
        }
        else
        {
            start = range_argument(evaluate(iterable->call.arguments[0], environment));
            end = range_argument(evaluate(iterable->call.arguments[1], environment));

            if (count == 3)
            {
                step = range_argument(evaluate(iterable->call.arguments[2], environment));
            }
        }

        if (step == 0)
        {
            diagnostics_runtime_errorf("E3011", "use a positive step to count up or a negative one to count down", "range step cannot be 0");
        }

        for (long i = start; step > 0 ? i < end : i > end; i += step)
        {
            diagnostics_line = node->line;

            if (!run_for_body(node, environment, value_int(i), &result))
            {
                return result;
            }
        }

        return result;
    }

    /* Iterate over a copy, so changing the original inside the loop is safe. */
    Value sequence = evaluate(iterable, environment);

    if (sequence.type == VALUE_ARRAY)
    {
        for (int i = 0; i < sequence.array.count; i++)
        {
            diagnostics_line = node->line;

            if (!run_for_body(node, environment, value_copy(&sequence.array.elements[i]), &result))
            {
                break;
            }
        }
    }
    else if (sequence.type == VALUE_MAP)
    {
        for (int i = 0; i < value_map_count(&sequence); i++)
        {
            diagnostics_line = node->line;

            if (!run_for_body(node, environment, value_copy(value_map_key_at(&sequence, i)), &result))
            {
                break;
            }
        }
    }
    else if (sequence.type == VALUE_STRING)
    {
        size_t length = value_string_length(&sequence);

        for (size_t i = 0; i < length; i++)
        {
            diagnostics_line = node->line;

            if (!run_for_body(node, environment, value_string_n(sequence.string + i, 1), &result))
            {
                break;
            }
        }
    }
    else
    {
        diagnostics_runtime_errorf("E3025", "for works on arrays, maps (their keys), strings (their characters) and range(...)", "cannot loop over %s", value_type_name(sequence.type));
    }

    value_free(&sequence);
    return result;
}

static ExecutionResult execute(
    AstNode *node,
    Environment *environment
)
{
    
    ExecutionResult result = {
        0,
        0,
        0,
        value_int(0)
    };
    
    if (node == NULL)
    {
        return result;
    }

    if (node->type != AST_PROGRAM)
    {
        diagnostics_line = node->line;
    }

    switch (node->type)
    {
        case AST_PROGRAM:
            for (int i = 0; i < node->program.count; i++)
            {
                result = execute(
                    node->program.statements[i],
                    environment
                );

                if (result.returned || result.broke || result.continued)
                {
                    return result;
                }
            }
            break;

        case AST_VARIABLE_DECLARATION:
        {
            AstNode *value_node = node->variable_declaration.value;

            /* Fast path for x = x + e: update x in place. */
            if (value_node->type == AST_BINARY &&
                value_node->binary.operator == TOKEN_PLUS &&
                value_node->binary.left->type == AST_VARIABLE_REFERENCE &&
                value_node->binary.left->variable_reference.name ==
                    node->variable_declaration.name &&
                find_assignable_variable(environment, node->variable_declaration.name) != NULL)
            {
                Value right = evaluate(value_node->binary.right, environment);
                Variable *target = find_assignable_variable(
                    environment,
                    node->variable_declaration.name
                );

                if (target->value.type == VALUE_INT && right.type == VALUE_INT)
                {
                    target->value.integer += right.integer;
                    break;
                }

                if (target->value.type == VALUE_STRING && right.type == VALUE_STRING)
                {
                    value_string_append(&target->value, &right);
                    value_free(&right);
                    break;
                }

                Value combined = binary_values(value_node, value_copy(&target->value), right);
                target = find_assignable_variable(environment, node->variable_declaration.name);
                value_free(&target->value);
                target->value = combined;
                break;
            }

            Value value = evaluate(
                node->variable_declaration.value,
                environment
            );

            set_variable(
                environment,
                node->variable_declaration.name,
                value
            );

            break;
        }

        case AST_CALL:
        {
            Value value = evaluate(node, environment);

            value_free(&value);

            break;
        }

        case AST_STRING:
        case AST_INTEGER:
        case AST_BOOLEAN:
        case AST_NONE:
        case AST_VARIABLE_REFERENCE:
        case AST_BINARY:
        case AST_UNARY:
        case AST_ARRAY:
        case AST_INDEX:
        case AST_FLOAT:
        case AST_MAP:
        case AST_IMPORT:
            break;
            
        case AST_INDEX_ASSIGNMENT:
        {
            Value value = evaluate(
                node->index_assignment.value,
                environment
            );
            Value index = evaluate(
                node->index_assignment.index,
                environment
            );
            Value *container = resolve_array_element(
                node->index_assignment.array,
                environment,
                1
            );

            Value *slot = index_place(container, index, 1, container->type == VALUE_MAP);
            value_free(slot);
            *slot = value;
            break;
        }

        case AST_FUNCTION:
            define_function(
                environment,
                node->function.name,
                node->function.parameters,
                node->function.parameter_count,
                node->function.body
            );
            break;
            
        case AST_RETURN:
        {
            result.value = evaluate(
                node->return_statement.value,
                environment
            );

            result.returned = 1;

            return result;
        }

        case AST_IF:
        {
            Value condition = evaluate(
                node->if_statement.condition,
                environment
            );

            if (condition.type != VALUE_BOOL)
            {
                diagnostics_runtime_errorf("E3015", NULL, "condition must be a boolean (got %s)", value_type_name(condition.type));
            }

            if (condition.boolean)
            {
                Environment *block =
                    environment_create(environment);

                result = execute(node->if_statement.body, block);
                environment_free(block);

                if (result.returned || result.broke || result.continued)
                {
                    return result;
                }
            }
            else if (node->if_statement.else_body != NULL)
            {
                Environment *block =
                    environment_create(environment);

                result = execute(node->if_statement.else_body, block);
                environment_free(block);

                if (result.returned || result.broke || result.continued)
                {
                    return result;
                }
            }

            break;
        }
            
        case AST_WHILE:
        {
            while (1)
            {
                diagnostics_line = node->line;

                Value condition = evaluate(
                    node->while_statement.condition,
                    environment
                );

                if (condition.type != VALUE_BOOL)
                {
                    diagnostics_runtime_errorf("E3015", NULL, "condition must be a boolean (got %s)", value_type_name(condition.type));
                }

                if (!condition.boolean)
                {
                    break;
                }

                Environment *block =
                    environment_create(environment);

                result = execute(node->while_statement.body, block);

                environment_free(block);

                if (result.returned)
                {
                    return result;
                }

                if (result.broke)
                {
                    result.broke = 0;
                    return result;
                }

                if (result.continued)
                {
                    result.continued = 0;
                    continue;
                }
            }

            break;
        }

        case AST_FOR:
            return execute_for(node, environment);

        case AST_BREAK:
            result.broke = 1;
            return result;

        case AST_CONTINUE:
            result.continued = 1;
            return result;
    }

    return result;
}

void interpreter_run(AstNode *program)
{
    Environment *environment =
        environment_create(NULL);

    global_environment = environment;

    ExecutionResult result = execute(program, environment);

    environment_free(environment);

    if (result.returned)
    {
        diagnostics_runtime_errorf("E3005", "return can only be used inside a fun block", "return outside a function");
    }

    if (result.broke || result.continued)
    {
        diagnostics_runtime_errorf("E3004", "break and continue can only be used inside a while loop", "break or continue outside a loop");
    }
}
