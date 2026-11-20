#ifndef SYGER_VALUE_H
#define SYGER_VALUE_H

#include <stddef.h>
#include <stdio.h>

typedef enum {
    VALUE_STRING,
    VALUE_INT,
    VALUE_BOOL,
    VALUE_NONE,
    VALUE_ARRAY,
    VALUE_FLOAT,
    VALUE_MAP
} ValueType;

typedef struct Value Value;
typedef struct Map Map;

struct Value {
    ValueType type;

    union {
        char *string;
        long integer;
        int boolean;
        double number;
        Map *map;
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

static inline Value value_float(double number)
{
    Value value;
    value.type = VALUE_FLOAT;
    value.number = number;
    return value;
}

Value value_none(void);
Value value_array(int count);
Value value_copy_object(const Value *value);

/* Copies are cheap: strings and arrays are shared and reference counted. */
static inline Value value_copy(const Value *value)
{
    if (value->type == VALUE_STRING || value->type == VALUE_ARRAY || value->type == VALUE_MAP)
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

/* Shortest text that reads back as the same double, always with a '.' or
   exponent so it doesn't look like an integer (2.0, 0.1, 1e+30). */
void value_format_float(double number, char *buffer, size_t size);

const char *value_type_name(ValueType type);

/* == semantics: same type and contents; ints and floats compare by value. */
int value_equal(const Value *left, const Value *right);

/*
 * Maps keep keys in insertion order. Keys are strings or integers. Like
 * arrays they are shared and copy-on-write: functions that take a Value *
 * map and change it make it unique first.
 */
Value value_map_new(int capacity);
int value_map_count(const Value *map);
int value_is_map_key(const Value *key);
/* Pointer to the value stored under key, or NULL. Does not copy. */
Value *value_map_find(const Value *map, const Value *key);
/* Pointer to the value under key, inserting none if missing. Consumes key. */
Value *value_map_slot(Value *map, Value key);
void value_map_set(Value *map, Value key, Value value);
/* Removes key; returns 1 and stores the old value in *removed if found. */
int value_map_remove(Value *map, const Value *key, Value *removed);
const Value *value_map_key_at(const Value *map, int index);
const Value *value_map_value_at(const Value *map, int index);
void value_map_make_unique(Value *map);

#endif
