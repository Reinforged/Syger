#include "interpreter.h"
#include "value.h"
#include "diagnostics.h"

#include <stdio.h>
#include <stdlib.h>
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
    Environment *parent;
    Function *functions;
    int function_count;
    int function_scope;
};

static int runtime_argument_count = 0;
static char **runtime_arguments = NULL;

void interpreter_set_arguments(int count, char **arguments)
{
    runtime_argument_count = count;
    runtime_arguments = arguments;
}

static Environment *environment_create(Environment *parent)
{
    Environment *environment = malloc(sizeof(Environment));

    if (environment == NULL)
    {
        diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
    }
    
    environment->functions = NULL;
    environment->function_count = 0;
    environment->variables = NULL;
    environment->count = 0;
    environment->parent = parent;
    environment->function_scope = 0;

    return environment;
}

static Environment *environment_create_function(Environment *parent)
{
    Environment *environment = environment_create(parent);
    environment->function_scope = 1;
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
    Variable *variable = NULL;
    for (Environment *current = environment;
         current != NULL;
         current = current->parent)
    {
        for (int i = 0; i < current->count; i++)
        {
            if (strcmp(current->variables[i].name, name) == 0)
            {
                variable = &current->variables[i];
                break;
            }
        }

        if (variable != NULL || current->function_scope)
        {
            break;
        }
    }

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
        diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
    }

    environment->variables = variables;

    Variable *new_variable =
        &environment->variables[environment->count];

    new_variable->name = malloc(strlen(name) + 1);

    if (new_variable->name == NULL)
    {
        diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
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
        diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
    }

    environment->functions = functions;

    Function *new_function =
        &environment->functions[environment->function_count];

    new_function->name = malloc(strlen(name) + 1);

    if (new_function->name == NULL)
    {
        diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
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
            diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
        }
    }
    
    for (int i = 0; i < parameter_count; i++)
    {
        new_function->parameters[i] =
            malloc(strlen(parameters[i]) + 1);

        if (new_function->parameters[i] == NULL)
        {
            diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
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
            diagnostics_runtime_errorf("E3001", "Define the variable before using it.", "variable '%s' is not defined.", node->variable_reference.name);
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
            diagnostics_runtime_errorf("E3005", "Index only an array value.", "indexing requires an array.\n");
        }

        if (index.type != VALUE_INT)
        {
            value_free(&index);
            diagnostics_runtime_errorf("E3006", "Use an integer array index.", "array index must be an integer.\n");
        }

        if (index.integer < 0 || index.integer >= array->array.count)
        {
            value_free(&index);
            diagnostics_runtime_errorf("E3007", "Use an index within the array bounds.", "array index out of bounds.\n");
        }

        Value *element = &array->array.elements[index.integer];
        value_free(&index);
        return element;
    }

    diagnostics_runtime_errorf("E3009", "Pass an array value.", "array assignment requires an array expression.\n");
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

        case VALUE_NONE:
            return 1;

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
        diagnostics_runtime_errorf("E3008", "Pass an array variable or an indexed array.", "%s requires an array variable or indexed array.", name);
    }

    Value *value = resolve_array_element(node, environment);

    if (value->type != VALUE_ARRAY)
    {
        diagnostics_runtime_errorf("E3009", "Pass an array value.", "%s requires an array.", name);
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
        diagnostics_runtime_errorf("E3010", "Pass an integer index.", "%s requires an integer index.", name
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
        diagnostics_runtime_errorf("E3011", "Use an index within the valid range.", "%s index out of bounds.", name);
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
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "length expects 1 argument.\n");
        }

        AstNode *argument = node->call.arguments[0];
        if (argument->type == AST_VARIABLE_REFERENCE || argument->type == AST_INDEX)
        {
            Value *value = resolve_array_element(argument, environment);
            if (value->type == VALUE_ARRAY) return value_int(value->array.count);
            if (value->type == VALUE_STRING) return value_int((long)strlen(value->string));
            diagnostics_runtime_errorf("E3009", "Pass an array value.", "length requires an array or string.\n");
        }

        Value value = evaluate(argument, environment);
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
        diagnostics_runtime_errorf("E3009", "Pass an array value.", "length requires an array or string.\n");
    }

    if (strcmp(name, "append") == 0)
    {
        if (node->call.argument_count != 2)
        {
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "append expects 2 arguments.\n");
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
            diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
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
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "pop expects 1 argument.\n");
        }

        Value *array = require_array_argument(
            node->call.arguments[0],
            environment,
            "pop"
        );

        if (array->array.count == 0)
        {
            diagnostics_runtime_errorf("E3012", "Check that the array is not empty first.", "pop cannot remove from an empty array.\n");
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
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "insert expects 3 arguments.\n");
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
            diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
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
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "remove expects 2 arguments.\n");
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
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "contains expects 2 arguments.\n");
        }

        Value array = evaluate(node->call.arguments[0], environment);
        Value value = evaluate(node->call.arguments[1], environment);

        if (array.type != VALUE_ARRAY)
        {
            value_free(&array);
            value_free(&value);
            diagnostics_runtime_errorf("E3009", "Pass an array value.", "contains requires an array.\n");
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
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "index_of expects 2 arguments.\n");
        }

        Value array = evaluate(node->call.arguments[0], environment);
        Value value = evaluate(node->call.arguments[1], environment);

        if (array.type != VALUE_ARRAY)
        {
            value_free(&array);
            value_free(&value);
            diagnostics_runtime_errorf("E3009", "Pass an array value.", "index_of requires an array.\n");
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
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "clear expects 1 argument.\n");
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
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "reverse expects 1 argument.\n");
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
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "slice expects 3 arguments.\n");
        }

        Value array = evaluate(node->call.arguments[0], environment);
        Value start_value = evaluate(node->call.arguments[1], environment);
        Value end_value = evaluate(node->call.arguments[2], environment);

        if (array.type != VALUE_ARRAY)
        {
            value_free(&array);
            value_free(&start_value);
            value_free(&end_value);
            diagnostics_runtime_errorf("E3009", "Pass an array value.", "slice requires an array.\n");
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
        if (node->call.argument_count != 2)
        {
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "join expects 2 arguments.\n");
        }

        Value array = evaluate(node->call.arguments[0], environment);
        Value separator = evaluate(node->call.arguments[1], environment);

        if (array.type != VALUE_ARRAY || separator.type != VALUE_STRING)
        {
            value_free(&array);
            value_free(&separator);
            diagnostics_runtime_errorf("E3009", "Pass an array value.", "join requires an array and a string separator.\n");
        }

        size_t total = 1;
        size_t separator_length = strlen(separator.string);

        for (int i = 0; i < array.array.count; i++)
        {
            if (array.array.elements[i].type != VALUE_STRING)
            {
                value_free(&array);
                value_free(&separator);
                diagnostics_runtime_errorf("E3009", "Pass an array value.", "join requires an array of strings.\n");
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

    if (strcmp(name, "sum") == 0 ||
        strcmp(name, "min") == 0 ||
        strcmp(name, "max") == 0)
    {
        if (node->call.argument_count != 1)
        {
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "array reduction expects 1 argument.\n");
        }

        Value array = evaluate(node->call.arguments[0], environment);

        if (array.type != VALUE_ARRAY)
        {
            value_free(&array);
            diagnostics_runtime_errorf("E3009", "Pass an array value.", "array reduction requires an array.\n");
        }

        if (array.array.count == 0)
        {
            value_free(&array);
            diagnostics_runtime_errorf("E3013", "Check that the array is not empty first.", "array reduction requires a non-empty array.\n");
        }

        for (int i = 0; i < array.array.count; i++)
        {
            if (array.array.elements[i].type != VALUE_INT)
            {
                value_free(&array);
                diagnostics_runtime_errorf("E3009", "Pass an array value.", "array reduction requires an array of integers.\n");
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
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "first/last expects 1 argument.\n");
        }

        Value array = evaluate(node->call.arguments[0], environment);

        if (array.type != VALUE_ARRAY)
        {
            value_free(&array);
            diagnostics_runtime_errorf("E3009", "Pass an array value.", "first/last requires an array.\n");
        }

        if (array.array.count == 0)
        {
            value_free(&array);
            diagnostics_runtime_errorf("E3013", "Check that the array is not empty first.", "first/last cannot be used on an empty array.\n");
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
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "sort expects 1 argument.\n");
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
                diagnostics_runtime_errorf("E3009", "Pass an array value.", "sort currently requires an array of integers.\n");
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

static Value evaluate_runtime_builtin(
    AstNode *node,
    Environment *environment
)
{
    const char *name = node->call.name;

    if (strcmp(name, "token_at") == 0)
    {
        if (node->call.argument_count != 2)
        {
            diagnostics_runtime_errorf("E3121", "Pass an array and integer index.", "token_at expects two arguments.");
        }
        AstNode *array_node = node->call.arguments[0];
        if (array_node->type != AST_VARIABLE_REFERENCE && array_node->type != AST_INDEX)
        {
            diagnostics_runtime_errorf("E3122", "Pass an array variable.", "token_at requires an array variable or indexed array.");
        }
        Value *array = resolve_array_element(array_node, environment);
        Value index_value = evaluate(node->call.arguments[1], environment);
        if (array->type != VALUE_ARRAY || index_value.type != VALUE_INT)
        {
            value_free(&index_value);
            diagnostics_runtime_errorf("E3123", "Pass an array and integer index.", "token_at expects (array, integer).");
        }
        long index = index_value.integer;
        value_free(&index_value);
        if (index < 0 || index >= array->array.count)
        {
            diagnostics_runtime_errorf("E3124", "Use an index within the array range.", "token_at index out of bounds.");
        }
        return value_copy(&array->array.elements[index]);
    }

    if (strcmp(name, "args") == 0)
    {
        if (node->call.argument_count != 0)
        {
            diagnostics_runtime_errorf("E3100", "Call args() without parameters.", "args expects no arguments.");
        }

        Value result = value_array(runtime_argument_count);
        for (int i = 0; i < runtime_argument_count; i++)
        {
            result.array.elements[i] = value_string(runtime_arguments[i]);
        }
        return result;
    }

    if (strcmp(name, "type_of") == 0)
    {
        if (node->call.argument_count != 1)
        {
            diagnostics_runtime_errorf("E3120", "Pass one value.", "type_of expects one argument.");
        }
        Value value = evaluate(node->call.arguments[0], environment);
        const char *type_name = "none";
        switch (value.type)
        {
            case VALUE_STRING: type_name = "string"; break;
            case VALUE_INT: type_name = "integer"; break;
            case VALUE_BOOL: type_name = "boolean"; break;
            case VALUE_NONE: type_name = "none"; break;
            case VALUE_ARRAY: type_name = "array"; break;
        }
        Value result = value_string(type_name);
        value_free(&value);
        return result;
    }

    if (strcmp(name, "fail") == 0)
    {
        if (node->call.argument_count != 1)
        {
            diagnostics_runtime_errorf("E3117", "Pass one error message.", "fail expects one string argument.");
        }
        Value message = evaluate(node->call.arguments[0], environment);
        if (message.type != VALUE_STRING)
        {
            value_free(&message);
            diagnostics_runtime_errorf("E3118", "Pass a string error message.", "fail requires a string.");
        }
        diagnostics_runtime_errorf("E3119", "Fix the reported problem and run again.", "%s", message.string);
    }

    if (strcmp(name, "to_string") == 0)
    {
        if (node->call.argument_count != 1)
        {
            diagnostics_runtime_errorf("E3113", "Pass one value.", "to_string expects one argument.");
        }

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
            value_free(&value);
            diagnostics_runtime_errorf("E3114", "Convert a scalar value.", "to_string does not accept arrays.");
        }

        Value result = value_string(text);
        value_free(&value);
        return result;
    }

    if (strcmp(name, "chr") == 0)
    {
        if (node->call.argument_count != 1)
        {
            diagnostics_runtime_errorf("E3115", "Pass one integer character code.", "chr expects one integer argument.");
        }

        Value code_value = evaluate(node->call.arguments[0], environment);
        if (code_value.type != VALUE_INT || code_value.integer < 0 || code_value.integer > 127)
        {
            value_free(&code_value);
            diagnostics_runtime_errorf("E3116", "Pass an integer from 0 through 127.", "chr currently supports ASCII character codes only.");
        }

        char character[2] = { (char)code_value.integer, '\0' };
        value_free(&code_value);
        return value_string(character);
    }

    if (strcmp(name, "read_file") == 0)
    {
        if (node->call.argument_count != 1)
        {
            diagnostics_runtime_errorf("E3101", "Pass one file path.", "read_file expects one string argument.");
        }

        Value path = evaluate(node->call.arguments[0], environment);
        if (path.type != VALUE_STRING)
        {
            value_free(&path);
            diagnostics_runtime_errorf("E3102", "Pass a string path.", "read_file requires a string path.");
        }

        FILE *file = fopen(path.string, "rb");
        if (file == NULL)
        {
            diagnostics_runtime_errorf("E3103", "Check that the file exists and is readable.", "read_file could not open '%s'.", path.string);
        }

        if (fseek(file, 0, SEEK_END) != 0)
        {
            fclose(file);
            value_free(&path);
            diagnostics_runtime_errorf("E3104", "Use a readable regular file.", "read_file could not seek to the end of the file.");
        }

        long file_size = ftell(file);
        if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0)
        {
            fclose(file);
            value_free(&path);
            diagnostics_runtime_errorf("E3104", "Use a readable regular file.", "read_file could not determine the file size.");
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
            value_free(&path);
            diagnostics_runtime_errorf("E3105", "Check file permissions and storage health.", "read_file failed while reading the file.");
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
        if (node->call.argument_count != 2)
        {
            diagnostics_runtime_errorf("E3106", "Pass a path and text.", "write_file expects two string arguments.");
        }

        Value path = evaluate(node->call.arguments[0], environment);
        Value content = evaluate(node->call.arguments[1], environment);
        if (path.type != VALUE_STRING || content.type != VALUE_STRING)
        {
            value_free(&path);
            value_free(&content);
            diagnostics_runtime_errorf("E3107", "Pass string arguments.", "write_file requires a path and content, both strings.");
        }

        FILE *file = fopen(path.string, "wb");
        if (file == NULL)
        {
            value_free(&path);
            value_free(&content);
            return value_bool(0);
        }

        size_t length = strlen(content.string);
        size_t written = fwrite(content.string, 1, length, file);
        int close_result = fclose(file);
        int success = written == length && close_result == 0;
        value_free(&path);
        value_free(&content);
        return value_bool(success);
    }

    if (strcmp(name, "char_at") == 0)
    {
        if (node->call.argument_count != 2)
        {
            diagnostics_runtime_errorf("E3108", "Pass a string and integer index.", "char_at expects two arguments.");
        }

        AstNode *text_node = node->call.arguments[0];
        Value temporary_text = value_none();
        Value *text = NULL;
        if (text_node->type == AST_VARIABLE_REFERENCE || text_node->type == AST_INDEX)
        {
            text = resolve_array_element(text_node, environment);
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
            diagnostics_runtime_errorf("E3109", "Pass a string and integer index.", "char_at expects (string, integer).");
        }

        long index = index_value.integer;
        size_t length = strlen(text->string);
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
        if (node->call.argument_count != 3)
        {
            diagnostics_runtime_errorf("E3110", "Pass a string, start index, and end index.", "substring expects three arguments.");
        }

        Value text = evaluate(node->call.arguments[0], environment);
        Value start_value = evaluate(node->call.arguments[1], environment);
        Value end_value = evaluate(node->call.arguments[2], environment);
        if (text.type != VALUE_STRING || start_value.type != VALUE_INT || end_value.type != VALUE_INT)
        {
            value_free(&text);
            value_free(&start_value);
            value_free(&end_value);
            diagnostics_runtime_errorf("E3111", "Pass (string, integer, integer).", "substring expects a string and two integer indices.");
        }

        long start = start_value.integer;
        long end = end_value.integer;
        size_t length = strlen(text.string);
        value_free(&start_value);
        value_free(&end_value);
        if (start < 0 || end < start || (size_t)end > length)
        {
            value_free(&text);
            diagnostics_runtime_errorf("E3112", "Use an end-exclusive range inside the string.", "substring indices are out of bounds.");
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

static int is_runtime_builtin(const char *name)
{
    return strcmp(name, "args") == 0 ||
           strcmp(name, "token_at") == 0 ||
           strcmp(name, "type_of") == 0 ||
           strcmp(name, "fail") == 0 ||
           strcmp(name, "to_string") == 0 ||
           strcmp(name, "chr") == 0 ||
           strcmp(name, "read_file") == 0 ||
           strcmp(name, "write_file") == 0 ||
           strcmp(name, "char_at") == 0 ||
           strcmp(name, "substring") == 0;
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
        diagnostics_runtime_errorf("E3005", "Index only an array value.", "indexing requires an array.\n");
    }

    if (index.type != VALUE_INT)
    {
        value_free(&array);
        value_free(&index);
        diagnostics_runtime_errorf("E3006", "Use an integer array index.", "array index must be an integer.\n");
    }

    if (index.integer < 0 || index.integer >= array.array.count)
    {
        value_free(&array);
        value_free(&index);
        diagnostics_runtime_errorf("E3007", "Use an index within the array bounds.", "array index out of bounds.\n");
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
                    diagnostics_runtime_errorf("E3016", "Use not with a boolean value.", "'not' requires a boolean value.\n");
                }

                return value_bool(!operand.boolean);

            case TOKEN_MINUS:
                if (operand.type != VALUE_INT)
                {
                    diagnostics_runtime_errorf("E3017", "Use unary - with an integer value.", "unary '-' requires an integer value.\n");
                }

                return value_int(-operand.integer);

            default:
                diagnostics_runtime_errorf("E3023", "Use a supported unary operator.", "unknown unary operator.\n");
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
            diagnostics_runtime_errorf("E3001", "Define the variable before using it.", "variable '%s' is not defined.", node->variable_reference.name);
        }

        return value_copy(&variable->value);
    }

    if (node->type == AST_CALL)
    {
        if (is_runtime_builtin(node->call.name))
        {
            return evaluate_runtime_builtin(node, environment);
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

        Function *function = find_function(
            environment,
            node->call.name
        );

        if (function == NULL)
        {
            diagnostics_runtime_errorf("E3002", "Define the function before calling it.", "function '%s' is not defined.", node->call.name);
        }

        if (function->parameter_count != node->call.argument_count)
        {
            diagnostics_runtime_errorf("E3003", "Pass the required number of arguments.", "function '%s' expects %d argument(s), got %d.", node->call.name, function->parameter_count, node->call.argument_count);
        }

        Environment *function_environment =
            environment_create_function(environment);

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
            diagnostics_runtime_errorf("E3004", "Use break or continue inside a loop.", "loop control statement outside of a loop.\n");
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
                diagnostics_runtime_errorf("E3018", "Use and with boolean values.", "'and' requires boolean values.\n");
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
                diagnostics_runtime_errorf("E3018", "Use and with boolean values.", "'and' requires boolean values.\n");
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
                diagnostics_runtime_errorf("E3019", "Use or with boolean values.", "'or' requires boolean values.\n");
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
                diagnostics_runtime_errorf("E3019", "Use or with boolean values.", "'or' requires boolean values.\n");
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

        if (left.type == VALUE_NONE &&
            right.type == VALUE_NONE)
        {
            switch (node->binary.operator)
            {
                case TOKEN_EQUAL_EQUAL:
                    return value_bool(1);

                case TOKEN_BANG_EQUAL:
                    return value_bool(0);

                default:
                    value_free(&left);
                    value_free(&right);
                    diagnostics_runtime_errorf("E3020", "Use none only with supported operators.", "unsupported none operator.\n");
            }
        }

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
                        diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
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
                    diagnostics_runtime_errorf("E3021", "Use strings with supported operators.", "unsupported string operator.\n");
            }
        }

        if (left.type != VALUE_INT || right.type != VALUE_INT)
        {
            value_free(&left);
            value_free(&right);
            diagnostics_runtime_errorf("E3022", "Use integers for arithmetic and numeric comparisons.", "arithmetic and numeric comparisons require integer values.\n");
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
                    diagnostics_runtime_errorf("E3014", "Make sure the divisor is not zero.", "division by zero.\n");
                }

                return value_int(left.integer / right.integer);

            case TOKEN_PERCENT:
                if (right.integer == 0)
                {
                    diagnostics_runtime_errorf("E3015", "Make sure the divisor is not zero.", "modulo by zero.\n");
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
                diagnostics_runtime_errorf("E3024", "Use a supported binary operator.", "unknown binary operator.\n");
        }
    }

    diagnostics_runtime_errorf("E3025", "Provide a valid expression.", "invalid expression.\n");
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
        case AST_NONE:
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
                diagnostics_runtime_errorf("E3005", "Index only an array value.", "indexing requires an array.\n");
            }

            if (index.type != VALUE_INT)
            {
                value_free(&index);
                value_free(&value);
                diagnostics_runtime_errorf("E3006", "Use an integer array index.", "array index must be an integer.\n");
            }

            if (index.integer < 0 || index.integer >= array->array.count)
            {
                value_free(&index);
                value_free(&value);
                diagnostics_runtime_errorf("E3007", "Use an index within the array bounds.", "array index out of bounds.\n");
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
                diagnostics_runtime_errorf("E3026", "Use a boolean expression as the if condition.", "if condition must be a boolean.\n");
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
                    diagnostics_runtime_errorf("E3027", "Use a boolean expression as the while condition.", "while condition must be a boolean.\n");
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
        diagnostics_runtime_errorf("E3004", "Use break or continue inside a loop.", "loop control statement outside of a loop.\n");
    }
}
