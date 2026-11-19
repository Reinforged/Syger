#include "value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Strings and array buffers are reference counted and shared between copies.
 * A hidden header sits directly in front of the character data / element
 * array, so existing code can keep using value.string and
 * value.array.elements as plain pointers. Arrays are copy-on-write: code
 * that mutates an array must call value_array_make_unique (or
 * value_array_reserve) first.
 */

typedef struct {
    long refcount;
    long length;
    long capacity;
} StringHeader;

typedef struct {
    long refcount;
    long capacity;
} ArrayHeader;

static void out_of_memory(void)
{
    fprintf(stderr, "Syger: out of memory.\n");
    exit(1);
}

static StringHeader *string_header(const char *string)
{
    return (StringHeader *)(void *)string - 1;
}

static ArrayHeader *array_header(const Value *elements)
{
    return (ArrayHeader *)(void *)elements - 1;
}

Value value_string_n(const char *string, size_t length)
{
    StringHeader *header = malloc(sizeof(StringHeader) + length + 1);

    if (header == NULL)
    {
        out_of_memory();
    }

    header->refcount = 1;
    header->length = (long)length;
    header->capacity = (long)length;

    char *data = (char *)(header + 1);
    memcpy(data, string, length);
    data[length] = '\0';

    Value value;
    value.type = VALUE_STRING;
    value.string = data;
    return value;
}

void value_string_append(Value *target, const Value *suffix)
{
    StringHeader *header = string_header(target->string);
    long extra = string_header(suffix->string)->length;
    long needed = header->length + extra;

    if (header->refcount != 1)
    {
        Value joined = value_string_n(target->string, (size_t)header->length);
        value_free(target);
        *target = joined;
        header = string_header(target->string);
    }

    if (needed > header->capacity)
    {
        long capacity = header->capacity * 2;

        if (capacity < needed)
        {
            capacity = needed;
        }

        if (capacity < 16)
        {
            capacity = 16;
        }

        header = realloc(header, sizeof(StringHeader) + (size_t)capacity + 1);

        if (header == NULL)
        {
            out_of_memory();
        }

        header->capacity = capacity;
        target->string = (char *)(header + 1);
    }

    memcpy(target->string + header->length, suffix->string, (size_t)extra);
    header->length = needed;
    target->string[needed] = '\0';
}

Value value_string(const char *string)
{
    return value_string_n(string, strlen(string));
}

size_t value_string_length(const Value *value)
{
    return (size_t)string_header(value->string)->length;
}



Value value_none(void)
{
    Value value;

    value.type = VALUE_NONE;
    value.integer = 0;

    return value;
}

static Value *array_buffer(int capacity)
{
    ArrayHeader *header = malloc(sizeof(ArrayHeader) + sizeof(Value) * (size_t)capacity);

    if (header == NULL)
    {
        out_of_memory();
    }

    header->refcount = 1;
    header->capacity = capacity;
    return (Value *)(void *)(header + 1);
}

Value value_array(int count)
{
    Value value;

    value.type = VALUE_ARRAY;
    value.array.count = count;
    value.array.elements = count > 0 ? array_buffer(count) : NULL;

    return value;
}

void value_array_make_unique(Value *array)
{
    if (array->array.elements == NULL)
    {
        return;
    }

    ArrayHeader *header = array_header(array->array.elements);

    if (header->refcount == 1)
    {
        return;
    }

    int capacity = array->array.count > 0 ? array->array.count : 1;
    Value *copy = array_buffer(capacity);

    for (int i = 0; i < array->array.count; i++)
    {
        copy[i] = value_copy(&array->array.elements[i]);
    }

    header->refcount--;
    array->array.elements = copy;
}

void value_array_reserve(Value *array, int needed)
{
    value_array_make_unique(array);

    long capacity = array->array.elements ? array_header(array->array.elements)->capacity : 0;

    if (needed <= capacity)
    {
        return;
    }

    long next = capacity ? capacity * 2 : 4;

    while (next < needed)
    {
        next *= 2;
    }

    ArrayHeader *header = array->array.elements ? array_header(array->array.elements) : NULL;
    ArrayHeader *grown = realloc(header, sizeof(ArrayHeader) + sizeof(Value) * (size_t)next);

    if (grown == NULL)
    {
        out_of_memory();
    }

    if (header == NULL)
    {
        grown->refcount = 1;
    }

    grown->capacity = next;
    array->array.elements = (Value *)(void *)(grown + 1);
}

Value value_copy_object(const Value *value)
{
    if (value->type == VALUE_STRING)
    {
        if (value->string != NULL)
        {
            string_header(value->string)->refcount++;
        }

        return *value;
    }

    if (value->type == VALUE_ARRAY)
    {
        if (value->array.elements != NULL)
        {
            array_header(value->array.elements)->refcount++;
        }

        return *value;
    }

    return *value;
}

void value_free(Value *value)
{
    if (value->type == VALUE_STRING)
    {
        if (value->string != NULL)
        {
            StringHeader *header = string_header(value->string);

            if (--header->refcount == 0)
            {
                free(header);
            }
        }

        value->string = NULL;
    }
    else if (value->type == VALUE_ARRAY)
    {
        if (value->array.elements != NULL)
        {
            ArrayHeader *header = array_header(value->array.elements);

            if (--header->refcount == 0)
            {
                for (int i = 0; i < value->array.count; i++)
                {
                    value_free(&value->array.elements[i]);
                }

                free(header);
            }
        }

        value->array.elements = NULL;
        value->array.count = 0;
    }
}

static void value_print_inline(FILE *out, const Value *value)
{
    switch (value->type)
    {
        case VALUE_STRING:
            fprintf(out, "%s", value->string);
            break;

        case VALUE_INT:
            fprintf(out, "%ld", value->integer);
            break;

        case VALUE_BOOL:
            fprintf(out, "%s", value->boolean ? "true" : "false");
            break;

        case VALUE_NONE:
            fprintf(out, "none");
            break;

        case VALUE_ARRAY:
            fprintf(out, "[");

            for (int i = 0; i < value->array.count; i++)
            {
                if (i > 0)
                {
                    fprintf(out, ", ");
                }

                value_print_inline(out, &value->array.elements[i]);
            }

            fprintf(out, "]");
            break;
    }
}

void value_fprint(FILE *out, const Value *value)
{
    value_print_inline(out, value);
    fputc('\n', out);
}

char *value_to_text(const Value *value)
{
    char *text = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&text, &size);

    if (out == NULL)
    {
        out_of_memory();
    }

    value_print_inline(out, value);
    fclose(out);
    return text;
}

void value_print(const Value *value)
{
    value_fprint(stdout, value);
}
