#ifndef SYGER_VALUE_H
#define SYGER_VALUE_H

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
Value value_int(long integer);
Value value_bool(int boolean);
Value value_none(void);
Value value_array(int count);
Value value_copy(const Value *value);

void value_free(Value *value);
void value_print(const Value *value);

#endif
