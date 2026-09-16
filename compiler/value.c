#include "value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy_string(const char *string)
{
    size_t length = strlen(string);

    char *copy = malloc(length + 1);

    if (copy == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    memcpy(copy, string, length + 1);

    return copy;
}

Value value_string(const char *string)
{
    Value value;

    value.type = VALUE_STRING;
    value.string = copy_string(string);

    return value;
}

Value value_int(long integer)
{
    Value value;

    value.type = VALUE_INT;
    value.integer = integer;

    return value;
}

Value value_bool(int boolean)
{
    Value value;

    value.type = VALUE_BOOL;
    value.boolean = boolean != 0;

    return value;
}

Value value_array(int count)
{
    Value value;

    value.type = VALUE_ARRAY;
    value.array.count = count;
    value.array.elements = NULL;

    if (count > 0)
    {
        value.array.elements = malloc(sizeof(Value) * (size_t)count);

        if (value.array.elements == NULL)
        {
            fprintf(stderr, "Syger: out of memory.\n");
            exit(1);
        }
    }

    return value;
}

Value value_copy(const Value *value)
{
    if (value->type == VALUE_STRING)
    {
        return value_string(value->string);
    }

    if (value->type == VALUE_ARRAY)
    {
        Value copy = value_array(value->array.count);

        for (int i = 0; i < value->array.count; i++)
        {
            copy.array.elements[i] = value_copy(&value->array.elements[i]);
        }

        return copy;
    }

    if (value->type == VALUE_INT)
    {
        return value_int(value->integer);
    }

    return value_bool(value->boolean);
}

void value_free(Value *value)
{
    if (value->type == VALUE_STRING)
    {
        free(value->string);
        value->string = NULL;
    }
    else if (value->type == VALUE_ARRAY)
    {
        for (int i = 0; i < value->array.count; i++)
        {
            value_free(&value->array.elements[i]);
        }

        free(value->array.elements);
        value->array.elements = NULL;
        value->array.count = 0;
    }
}

static void value_print_inline(const Value *value)
{
    switch (value->type)
    {
        case VALUE_STRING:
            printf("%s", value->string);
            break;

        case VALUE_INT:
            printf("%ld", value->integer);
            break;

        case VALUE_BOOL:
            printf("%s", value->boolean ? "true" : "false");
            break;

        case VALUE_ARRAY:
            printf("[");

            for (int i = 0; i < value->array.count; i++)
            {
                if (i > 0)
                {
                    printf(", ");
                }

                value_print_inline(&value->array.elements[i]);
            }

            printf("]");
            break;
    }
}

void value_print(const Value *value)
{
    value_print_inline(value);
    printf("\n");
}
