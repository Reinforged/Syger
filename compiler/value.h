#ifndef SYGER_VALUE_H
#define SYGER_VALUE_H

#include <stddef.h>
#include <stdio.h>

typedef enum {
    VALUE_STRING,
    VALUE_INT,
    VALUE_BOOL,
    VALUE_NONE,
    VALUE_ARRAY
} ValueType;

typedef struct Value Value;

struct Value {
    ValueType type;

    union {
        char *string;
        long integer;
        int boolean;
        struct {
            Value *elements;
            int count;
        } array;
    };
};

Value value_string(const char *string);
static inline Value value_int(long integer)
{
    Value value;
    value.type = VALUE_INT;
    value.integer = integer;
    return value;
}

static inline Value value_bool(int boolean)
{
    Value value;
    value.type = VALUE_BOOL;
    value.boolean = boolean != 0;
    return value;
}

Value value_none(void);
Value value_array(int count);
Value value_copy_object(const Value *value);

/* Copies are cheap: strings and arrays are shared and reference counted. */
static inline Value value_copy(const Value *value)
{
    if (value->type == VALUE_STRING || value->type == VALUE_ARRAY)
    {
        return value_copy_object(value);
    }

    return *value;
}
Value value_string_n(const char *string, size_t length);
size_t value_string_length(const Value *value);
void value_string_append(Value *target, const Value *suffix);
void value_array_make_unique(Value *array);
void value_array_reserve(Value *array, int needed);

void value_free(Value *value);
void value_print(const Value *value);
void value_fprint(FILE *out, const Value *value);

/* The text print() would show, without the newline; caller frees it. */
char *value_to_text(const Value *value);

#endif
