#include "interpreter.h"
#include "value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    Environment *parent;
    Function *functions;
    int function_count;
};

static Environment *environment_create(Environment *parent)
{
    Environment *environment = malloc(sizeof(Environment));

    if (environment == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }
    
    environment->functions = NULL;
    environment->function_count = 0;
    environment->variables = NULL;
    environment->count = 0;
    environment->parent = parent;

    return environment;
}

static Variable *find_variable(Environment *environment, const char *name)
{
    for (Environment *current = environment;
         current != NULL;
         current = current->parent)
    {
        for (int i = 0; i < current->count; i++)
        {
            if (strcmp(current->variables[i].name, name) == 0)
            {
                return &current->variables[i];
            }
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
            if (strcmp(current->functions[i].name, name) == 0)
            {
                return &current->functions[i];
            }
        }
    }

    return NULL;
}

static void set_variable(
    Environment *environment,
    const char *name,
    Value value
)
{
    Variable *variable = find_variable(environment, name);

    if (variable != NULL)
    {
        value_free(&variable->value);
        variable->value = value;
        return;
    }

    Variable *variables = realloc(
        environment->variables,
        sizeof(Variable) * (environment->count + 1)
    );

    if (variables == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    environment->variables = variables;

    Variable *new_variable =
        &environment->variables[environment->count];

    new_variable->name = malloc(strlen(name) + 1);

    if (new_variable->name == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    strcpy(new_variable->name, name);
    new_variable->value = value;

    environment->count++;
}

static void environment_free(Environment *environment)
{
    for (int i = 0; i < environment->count; i++)
    {
        free(environment->variables[i].name);
        value_free(&environment->variables[i].value);
    }
    
    for (int i = 0; i < environment->function_count; i++)
    {
        free(environment->functions[i].name);

        for (int j = 0;
             j < environment->functions[i].parameter_count;
             j++)
        {
            free(environment->functions[i].parameters[j]);
        }

        free(environment->functions[i].parameters);
    }

    free(environment->variables);
    free(environment->functions);
    free(environment);
}

static void define_function(
    Environment *environment,
    const char *name,
    char **parameters,
    int parameter_count,
    AstNode *body
)
{
    Function *function = find_function(environment, name);

    if (function != NULL)
    {
        function->body = body;
        return;
    }

    Function *functions = realloc(
        environment->functions,
        sizeof(Function) * (environment->function_count + 1)
    );

    if (functions == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    environment->functions = functions;

    Function *new_function =
        &environment->functions[environment->function_count];

    new_function->name = malloc(strlen(name) + 1);

    if (new_function->name == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    strcpy(new_function->name, name);

    new_function->parameter_count = parameter_count;

    new_function->parameters = NULL;

    if (parameter_count > 0)
    {
        new_function->parameters = malloc(
            sizeof(char *) * parameter_count
        );

        if (new_function->parameters == NULL)
        {
            fprintf(stderr, "Syger: out of memory.\n");
            exit(1);
        }
    }
    
    for (int i = 0; i < parameter_count; i++)
    {
        new_function->parameters[i] =
            malloc(strlen(parameters[i]) + 1);

        if (new_function->parameters[i] == NULL)
        {
            fprintf(stderr, "Syger: out of memory.\n");
            exit(1);
        }

        strcpy(
            new_function->parameters[i],
            parameters[i]
        );
    }

    new_function->body = body;
    environment->function_count++;
}

static ExecutionResult execute(
    AstNode *node,
    Environment *environment
);

static Value evaluate(
    AstNode *node,
    Environment *environment
);

static Value *resolve_array_element(
    AstNode *node,
    Environment *environment
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
            fprintf(
                stderr,
                "Syger runtime error: variable '%s' is not defined.\n",
                node->variable_reference.name
            );
            exit(1);
        }

        return &variable->value;
    }

    if (node->type == AST_INDEX)
    {
        Value *array = resolve_array_element(
            node->index.array,
            environment
        );

        Value index = evaluate(node->index.index, environment);

        if (array->type != VALUE_ARRAY)
        {
            value_free(&index);
            fprintf(
                stderr,
                "Syger runtime error: indexing requires an array.\n"
            );
            exit(1);
        }

        if (index.type != VALUE_INT)
        {
            value_free(&index);
            fprintf(
                stderr,
                "Syger runtime error: array index must be an integer.\n"
            );
            exit(1);
        }

        if (index.integer < 0 || index.integer >= array->array.count)
        {
            value_free(&index);
            fprintf(
                stderr,
                "Syger runtime error: array index out of bounds.\n"
            );
            exit(1);
        }

        Value *element = &array->array.elements[index.integer];
        value_free(&index);
        return element;
    }

    fprintf(
        stderr,
        "Syger runtime error: array assignment requires an array expression.\n"
    );
    exit(1);
}

static int value_equals(const Value *left, const Value *right)
{
    if (left->type != right->type)
    {
        return 0;
    }

    switch (left->type)
    {
        case VALUE_STRING:
            return strcmp(left->string, right->string) == 0;

        case VALUE_INT:
            return left->integer == right->integer;

        case VALUE_BOOL:
            return left->boolean == right->boolean;

        case VALUE_ARRAY:
            if (left->array.count != right->array.count)
            {
                return 0;
            }

            for (int i = 0; i < left->array.count; i++)
            {
                if (!value_equals(
                    &left->array.elements[i],
                    &right->array.elements[i]
                ))
                {
                    return 0;
                }
            }

            return 1;
    }

    return 0;
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
        fprintf(
            stderr,
            "Syger runtime error: %s requires an array variable or indexed array.\n",
            name
        );
        exit(1);
    }

    Value *value = resolve_array_element(node, environment);

    if (value->type != VALUE_ARRAY)
    {
        fprintf(
            stderr,
            "Syger runtime error: %s requires an array.\n",
            name
        );
        exit(1);
    }

    return value;
}

static long require_integer_value(
    Value *value,
    const char *name
)
{
    if (value->type != VALUE_INT)
    {
        fprintf(
            stderr,
            "Syger runtime error: %s requires an integer index.\n",
            name
        );
        value_free(value);
        exit(1);
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
        fprintf(
            stderr,
            "Syger runtime error: %s index out of bounds.\n",
            name
        );
        exit(1);
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
        if (node->call.argument_count != 1)
        {
            fprintf(stderr, "Syger runtime error: length expects 1 argument.\n");
            exit(1);
        }

        Value value = evaluate(node->call.arguments[0], environment);

        if (value.type == VALUE_ARRAY)
        {
            int count = value.array.count;
            value_free(&value);
            return value_int(count);
        }

        if (value.type == VALUE_STRING)
        {
            size_t count = strlen(value.string);
            value_free(&value);
            return value_int((long)count);
        }

        value_free(&value);
        fprintf(stderr, "Syger runtime error: length requires an array or string.\n");
        exit(1);
    }

    if (strcmp(name, "append") == 0)
    {
        if (node->call.argument_count != 2)
        {
            fprintf(stderr, "Syger runtime error: append expects 2 arguments.\n");
            exit(1);
        }

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "append"
        );

        Value value = evaluate(node->call.arguments[1], environment);

        Value *elements = realloc(
            array->array.elements,
            sizeof(Value) * (size_t)(array->array.count + 1)
        );

        if (elements == NULL)
        {
            value_free(&value);
            fprintf(stderr, "Syger: out of memory.\n");
            exit(1);
        }

        array->array.elements = elements;
        array->array.elements[array->array.count] = value;
        array->array.count++;

        return value_int(0);
    }

    if (strcmp(name, "pop") == 0)
    {
        if (node->call.argument_count != 1)
        {
            fprintf(stderr, "Syger runtime error: pop expects 1 argument.\n");
            exit(1);
        }

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "pop"
        );

        if (array->array.count == 0)
        {
            fprintf(stderr, "Syger runtime error: pop cannot remove from an empty array.\n");
            exit(1);
        }

        int index = array->array.count - 1;
        Value result = array->array.elements[index];
        array->array.count--;

        if (array->array.count == 0)
        {
            free(array->array.elements);
            array->array.elements = NULL;
        }
        else
        {
            Value *elements = realloc(
                array->array.elements,
                sizeof(Value) * (size_t)array->array.count
            );

            if (elements != NULL)
            {
                array->array.elements = elements;
            }
        }

        return result;
    }

    if (strcmp(name, "insert") == 0)
    {
        if (node->call.argument_count != 3)
        {
            fprintf(stderr, "Syger runtime error: insert expects 3 arguments.\n");
            exit(1);
        }

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "insert"
        );

        Value index_value = evaluate(node->call.arguments[1], environment);
        long index = require_integer_value(&index_value, "insert");
        check_array_index(array, index, "insert", 1);

        Value value = evaluate(node->call.arguments[2], environment);

        Value *elements = realloc(
            array->array.elements,
            sizeof(Value) * (size_t)(array->array.count + 1)
        );

        if (elements == NULL)
        {
            value_free(&value);
            fprintf(stderr, "Syger: out of memory.\n");
            exit(1);
        }

        array->array.elements = elements;

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
        if (node->call.argument_count != 2)
        {
            fprintf(stderr, "Syger runtime error: remove expects 2 arguments.\n");
            exit(1);
        }

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "remove"
        );

        Value index_value = evaluate(node->call.arguments[1], environment);
        long index = require_integer_value(&index_value, "remove");
        check_array_index(array, index, "remove", 0);

        Value result = array->array.elements[index];

        for (int i = (int)index; i < array->array.count - 1; i++)
        {
            array->array.elements[i] = array->array.elements[i + 1];
        }

        array->array.count--;

        if (array->array.count == 0)
        {
            free(array->array.elements);
            array->array.elements = NULL;
        }
        else
        {
            Value *elements = realloc(
                array->array.elements,
                sizeof(Value) * (size_t)array->array.count
            );

            if (elements != NULL)
            {
                array->array.elements = elements;
            }
        }

        return result;
    }

    if (strcmp(name, "contains") == 0)
    {
        if (node->call.argument_count != 2)
        {
            fprintf(stderr, "Syger runtime error: contains expects 2 arguments.\n");
            exit(1);
        }

        Value array = evaluate(node->call.arguments[0], environment);
        Value value = evaluate(node->call.arguments[1], environment);

        if (array.type != VALUE_ARRAY)
        {
            value_free(&array);
            value_free(&value);
            fprintf(stderr, "Syger runtime error: contains requires an array.\n");
            exit(1);
        }

        int found = 0;

        for (int i = 0; i < array.array.count; i++)
        {
            if (value_equals(&array.array.elements[i], &value))
            {
                found = 1;
                break;
            }
        }

        value_free(&array);
        value_free(&value);
        return value_bool(found);
    }

    if (strcmp(name, "index_of") == 0)
    {
        if (node->call.argument_count != 2)
        {
            fprintf(stderr, "Syger runtime error: index_of expects 2 arguments.\n");
            exit(1);
        }

        Value array = evaluate(node->call.arguments[0], environment);
        Value value = evaluate(node->call.arguments[1], environment);

        if (array.type != VALUE_ARRAY)
        {
            value_free(&array);
            value_free(&value);
            fprintf(stderr, "Syger runtime error: index_of requires an array.\n");
            exit(1);
        }

        long index = -1;

        for (int i = 0; i < array.array.count; i++)
        {
            if (value_equals(&array.array.elements[i], &value))
            {
                index = i;
                break;
            }
        }

        value_free(&array);
        value_free(&value);
        return value_int(index);
    }

    if (strcmp(name, "clear") == 0)
    {
        if (node->call.argument_count != 1)
        {
            fprintf(stderr, "Syger runtime error: clear expects 1 argument.\n");
            exit(1);
        }

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "clear"
        );

        for (int i = 0; i < array->array.count; i++)
        {
            value_free(&array->array.elements[i]);
        }

        free(array->array.elements);
        array->array.elements = NULL;
        array->array.count = 0;

        return value_int(0);
    }

    if (strcmp(name, "reverse") == 0)
    {
        if (node->call.argument_count != 1)
        {
            fprintf(stderr, "Syger runtime error: reverse expects 1 argument.\n");
            exit(1);
        }

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
        if (node->call.argument_count != 3)
        {
            fprintf(stderr, "Syger runtime error: slice expects 3 arguments.\n");
            exit(1);
        }

        Value array = evaluate(node->call.arguments[0], environment);
        Value start_value = evaluate(node->call.arguments[1], environment);
        Value end_value = evaluate(node->call.arguments[2], environment);

        if (array.type != VALUE_ARRAY)
        {
            value_free(&array);
            value_free(&start_value);
            value_free(&end_value);
            fprintf(stderr, "Syger runtime error: slice requires an array.\n");
            exit(1);
        }

        long start = require_integer_value(&start_value, "slice");
        long end = require_integer_value(&end_value, "slice");

        if (start < 0 || end < start || end > array.array.count)
        {
            value_free(&array);
            fprintf(stderr, "Syger runtime error: slice indices out of bounds.\n");
            exit(1);
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
        if (node->call.argument_count != 2)
        {
            fprintf(stderr, "Syger runtime error: join expects 2 arguments.\n");
            exit(1);
        }

        Value array = evaluate(node->call.arguments[0], environment);
        Value separator = evaluate(node->call.arguments[1], environment);

        if (array.type != VALUE_ARRAY || separator.type != VALUE_STRING)
        {
            value_free(&array);
            value_free(&separator);
            fprintf(stderr, "Syger runtime error: join requires an array and a string separator.\n");
            exit(1);
        }

        size_t total = 1;
        size_t separator_length = strlen(separator.string);

        for (int i = 0; i < array.array.count; i++)
        {
            if (array.array.elements[i].type != VALUE_STRING)
            {
                value_free(&array);
                value_free(&separator);
                fprintf(stderr, "Syger runtime error: join requires an array of strings.\n");
                exit(1);
            }

            total += strlen(array.array.elements[i].string);

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
            fprintf(stderr, "Syger: out of memory.\n");
            exit(1);
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

    if (strcmp(name, "sum") == 0 ||
        strcmp(name, "min") == 0 ||
        strcmp(name, "max") == 0)
    {
        if (node->call.argument_count != 1)
        {
            fprintf(stderr, "Syger runtime error: array reduction expects 1 argument.\n");
            exit(1);
        }

        Value array = evaluate(node->call.arguments[0], environment);

        if (array.type != VALUE_ARRAY)
        {
            value_free(&array);
            fprintf(stderr, "Syger runtime error: array reduction requires an array.\n");
            exit(1);
        }

        if (array.array.count == 0)
        {
            value_free(&array);
            fprintf(stderr, "Syger runtime error: array reduction requires a non-empty array.\n");
            exit(1);
        }

        for (int i = 0; i < array.array.count; i++)
        {
            if (array.array.elements[i].type != VALUE_INT)
            {
                value_free(&array);
                fprintf(stderr, "Syger runtime error: array reduction requires an array of integers.\n");
                exit(1);
            }
        }

        long result = array.array.elements[0].integer;

        if (strcmp(name, "sum") == 0)
        {
            result = 0;

            for (int i = 0; i < array.array.count; i++)
            {
                result += array.array.elements[i].integer;
            }
        }
        else
        {
            for (int i = 1; i < array.array.count; i++)
            {
                if (strcmp(name, "min") == 0 &&
                    array.array.elements[i].integer < result)
                {
                    result = array.array.elements[i].integer;
                }

                if (strcmp(name, "max") == 0 &&
                    array.array.elements[i].integer > result)
                {
                    result = array.array.elements[i].integer;
                }
            }
        }

        value_free(&array);
        return value_int(result);
    }

    if (strcmp(name, "first") == 0 || strcmp(name, "last") == 0)
    {
        if (node->call.argument_count != 1)
        {
            fprintf(stderr, "Syger runtime error: first/last expects 1 argument.\n");
            exit(1);
        }

        Value array = evaluate(node->call.arguments[0], environment);

        if (array.type != VALUE_ARRAY)
        {
            value_free(&array);
            fprintf(stderr, "Syger runtime error: first/last requires an array.\n");
            exit(1);
        }

        if (array.array.count == 0)
        {
            value_free(&array);
            fprintf(stderr, "Syger runtime error: first/last cannot be used on an empty array.\n");
            exit(1);
        }

        int index = strcmp(name, "first") == 0 ? 0 : array.array.count - 1;
        Value result = value_copy(&array.array.elements[index]);
        value_free(&array);
        return result;
    }

    if (strcmp(name, "sort") == 0)
    {
        if (node->call.argument_count != 1)
        {
            fprintf(stderr, "Syger runtime error: sort expects 1 argument.\n");
            exit(1);
        }

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "sort"
        );

        for (int i = 0; i < array->array.count; i++)
        {
            if (array->array.elements[i].type != VALUE_INT)
            {
                fprintf(
                    stderr,
                    "Syger runtime error: sort currently requires an array of integers.\n"
                );
                exit(1);
            }
        }

        for (int i = 0; i < array->array.count; i++)
        {
            for (int j = i + 1; j < array->array.count; j++)
            {
                if (array->array.elements[j].integer <
                    array->array.elements[i].integer)
                {
                    Value temporary = array->array.elements[i];
                    array->array.elements[i] = array->array.elements[j];
                    array->array.elements[j] = temporary;
                }
            }
        }

        return value_int(0);
    }

    return value_int(0);
}

static Value evaluate_index(
    AstNode *array_node,
    AstNode *index_node,
    Environment *environment
)
{
    Value array = evaluate(array_node, environment);
    Value index = evaluate(index_node, environment);

    if (array.type != VALUE_ARRAY)
    {
        value_free(&array);
        value_free(&index);
        fprintf(stderr, "Syger runtime error: indexing requires an array.\n");
        exit(1);
    }

    if (index.type != VALUE_INT)
    {
        value_free(&array);
        value_free(&index);
        fprintf(stderr, "Syger runtime error: array index must be an integer.\n");
        exit(1);
    }

    if (index.integer < 0 || index.integer >= array.array.count)
    {
        value_free(&array);
        value_free(&index);
        fprintf(stderr, "Syger runtime error: array index out of bounds.\n");
        exit(1);
    }

    Value result = value_copy(&array.array.elements[index.integer]);
    value_free(&array);
    value_free(&index);
    return result;
}

static Value evaluate(
    AstNode *node,
    Environment *environment
)
{
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
                    fprintf(
                        stderr,
                        "Syger runtime error: 'not' requires a boolean value.\n"
                    );
                    exit(1);
                }

                return value_bool(!operand.boolean);

            case TOKEN_MINUS:
                if (operand.type != VALUE_INT)
                {
                    fprintf(
                        stderr,
                        "Syger runtime error: unary '-' requires an integer value.\n"
                    );
                    exit(1);
                }

                return value_int(-operand.integer);

            default:
                fprintf(
                    stderr,
                    "Syger runtime error: unknown unary operator.\n"
                );
                exit(1);
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
            fprintf(
                stderr,
                "Syger runtime error: variable '%s' is not defined.\n",
                node->variable_reference.name
            );
            exit(1);
        }

        return value_copy(&variable->value);
    }

    if (node->type == AST_CALL)
    {
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

        Function *function = find_function(
            environment,
            node->call.name
        );

        if (function == NULL)
        {
            fprintf(
                stderr,
                "Syger runtime error: function '%s' is not defined.\n",
                node->call.name
            );
            exit(1);
        }

        if (function->parameter_count != node->call.argument_count)
        {
            fprintf(
                stderr,
                "Syger runtime error: function '%s' expects %d argument(s), got %d.\n",
                node->call.name,
                function->parameter_count,
                node->call.argument_count
            );
            exit(1);
        }

        Environment *function_environment =
            environment_create(environment);

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

        ExecutionResult function_result = execute(
            function->body,
            function_environment
        );

        environment_free(function_environment);

        if (function_result.broke || function_result.continued)
        {
            fprintf(
                stderr,
                "Syger runtime error: loop control statement outside of a loop.\n"
            );
            exit(1);
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
                fprintf(
                    stderr,
                    "Syger runtime error: 'and' requires boolean values.\n"
                );
                exit(1);
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
                fprintf(
                    stderr,
                    "Syger runtime error: 'and' requires boolean values.\n"
                );
                exit(1);
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
                fprintf(
                    stderr,
                    "Syger runtime error: 'or' requires boolean values.\n"
                );
                exit(1);
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
                fprintf(
                    stderr,
                    "Syger runtime error: 'or' requires boolean values.\n"
                );
                exit(1);
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

        if (left.type == VALUE_STRING &&
            right.type == VALUE_STRING)
        {
            switch (node->binary.operator)
            {
                case TOKEN_PLUS:
                {
                    size_t left_length = strlen(left.string);
                    size_t right_length = strlen(right.string);

                    char *result = malloc(
                        left_length + right_length + 1
                    );

                    if (result == NULL)
                    {
                        fprintf(stderr, "Syger: out of memory.\n");
                        exit(1);
                    }

                    memcpy(result, left.string, left_length);
                    memcpy(
                        result + left_length,
                        right.string,
                        right_length + 1
                    );

                    Value value = value_string(result);
                    free(result);
                    value_free(&left);
                    value_free(&right);
                    return value;
                }

                case TOKEN_EQUAL_EQUAL:
                {
                    int equal = strcmp(left.string, right.string) == 0;
                    value_free(&left);
                    value_free(&right);
                    return value_bool(equal);
                }

                case TOKEN_BANG_EQUAL:
                {
                    int not_equal = strcmp(left.string, right.string) != 0;
                    value_free(&left);
                    value_free(&right);
                    return value_bool(not_equal);
                }

                default:
                    value_free(&left);
                    value_free(&right);
                    fprintf(
                        stderr,
                        "Syger runtime error: unsupported string operator.\n"
                    );
                    exit(1);
            }
        }

        if (left.type != VALUE_INT || right.type != VALUE_INT)
        {
            value_free(&left);
            value_free(&right);
            fprintf(
                stderr,
                "Syger runtime error: arithmetic and numeric comparisons require integer values.\n"
            );
            exit(1);
        }

        switch (node->binary.operator)
        {
            case TOKEN_PLUS:
                return value_int(left.integer + right.integer);

            case TOKEN_MINUS:
                return value_int(left.integer - right.integer);

            case TOKEN_STAR:
                return value_int(left.integer * right.integer);

            case TOKEN_SLASH:
                if (right.integer == 0)
                {
                    fprintf(
                        stderr,
                        "Syger runtime error: division by zero.\n"
                    );
                    exit(1);
                }

                return value_int(left.integer / right.integer);

            case TOKEN_PERCENT:
                if (right.integer == 0)
                {
                    fprintf(
                        stderr,
                        "Syger runtime error: modulo by zero.\n"
                    );
                    exit(1);
                }

                return value_int(left.integer % right.integer);

            case TOKEN_EQUAL_EQUAL:
                return value_bool(left.integer == right.integer);

            case TOKEN_BANG_EQUAL:
                return value_bool(left.integer != right.integer);

            case TOKEN_LESS:
                return value_bool(left.integer < right.integer);

            case TOKEN_LESS_EQUAL:
                return value_bool(left.integer <= right.integer);

            case TOKEN_GREATER:
                return value_bool(left.integer > right.integer);

            case TOKEN_GREATER_EQUAL:
                return value_bool(left.integer >= right.integer);

            default:
                fprintf(
                    stderr,
                    "Syger runtime error: unknown binary operator.\n"
                );
                exit(1);
        }
    }

    fprintf(
        stderr,
        "Syger runtime error: invalid expression.\n"
    );
    exit(1);
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
        case AST_VARIABLE_REFERENCE:
        case AST_BINARY:
        case AST_UNARY:
        case AST_ARRAY:
        case AST_INDEX:
            break;
            
        case AST_INDEX_ASSIGNMENT:
        {
            Value *array = resolve_array_element(
                node->index_assignment.array,
                environment
            );
            Value index = evaluate(
                node->index_assignment.index,
                environment
            );
            Value value = evaluate(
                node->index_assignment.value,
                environment
            );

            if (array->type != VALUE_ARRAY)
            {
                value_free(&index);
                value_free(&value);
                fprintf(stderr, "Syger runtime error: indexing requires an array.\n");
                exit(1);
            }

            if (index.type != VALUE_INT)
            {
                value_free(&index);
                value_free(&value);
                fprintf(stderr, "Syger runtime error: array index must be an integer.\n");
                exit(1);
            }

            if (index.integer < 0 || index.integer >= array->array.count)
            {
                value_free(&index);
                value_free(&value);
                fprintf(stderr, "Syger runtime error: array index out of bounds.\n");
                exit(1);
            }

            value_free(&array->array.elements[index.integer]);
            array->array.elements[index.integer] = value;
            value_free(&index);
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
                fprintf(
                    stderr,
                    "Syger runtime error: if condition must be a boolean.\n"
                );
                exit(1);
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
                Value condition = evaluate(
                    node->while_statement.condition,
                    environment
                );

                if (condition.type != VALUE_BOOL)
                {
                    fprintf(
                        stderr,
                        "Syger runtime error: while condition must be a boolean.\n"
                    );
                    exit(1);
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

    ExecutionResult result = execute(program, environment);

    environment_free(environment);

    if (result.broke || result.continued)
    {
        fprintf(
            stderr,
            "Syger runtime error: loop control statement outside of a loop.\n"
        );
        exit(1);
    }
}
