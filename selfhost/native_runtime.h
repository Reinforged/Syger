#ifndef SYGER_NATIVE_RUNTIME_H
#define SYGER_NATIVE_RUNTIME_H

#include <stddef.h>

typedef enum {
    SYGER_VALUE_STRING,
    SYGER_VALUE_INT,
    SYGER_VALUE_BOOL,
    SYGER_VALUE_NONE,
    SYGER_VALUE_ARRAY
} SygerValueType;

typedef struct SygerValue SygerValue;
typedef struct SygerEnv SygerEnv;
typedef struct SygerFunctionDef SygerFunctionDef;

typedef SygerValue (*SygerNativeFunction)(
    SygerEnv *parent,
    const SygerValue *arguments,
    int argument_count
);

struct SygerValue {
    SygerValueType type;
    union {
        char *string;
        long integer;
        int boolean;
        struct {
            SygerValue *elements;
            int count;
        } array;
    };
};

struct SygerFunctionDef {
    const char *name;
    int parameter_count;
    SygerNativeFunction function;
};

typedef struct {
    SygerValue value;
    SygerValue *reference;
} SygerCallArg;

enum {
    SYGER_OP_PLUS = 1,
    SYGER_OP_MINUS,
    SYGER_OP_STAR,
    SYGER_OP_SLASH,
    SYGER_OP_PERCENT,
    SYGER_OP_EQUAL_EQUAL,
    SYGER_OP_BANG_EQUAL,
    SYGER_OP_LESS,
    SYGER_OP_LESS_EQUAL,
    SYGER_OP_GREATER,
    SYGER_OP_GREATER_EQUAL,
    SYGER_OP_NOT,
    SYGER_OP_NEGATE
};

void syger_runtime_set_arguments(int argc, char **argv);
void syger_runtime_shutdown(void);

SygerValue syger_value_string(const char *text);
SygerValue syger_value_int(long value);
SygerValue syger_value_bool(int value);
SygerValue syger_value_none(void);
SygerValue syger_value_array(int count, SygerValue *items);
SygerValue syger_value_copy(const SygerValue *value);
void syger_value_free(SygerValue *value);
void syger_value_discard(SygerValue value);
void syger_value_print(const SygerValue *value);
char *syger_value_render(const SygerValue *value);

SygerEnv *syger_env_new(SygerEnv *parent);
SygerEnv *syger_env_new_function(SygerEnv *parent);
void syger_env_free(SygerEnv *environment);
void syger_env_free_until(SygerEnv *stop, SygerEnv *current);
SygerValue syger_env_get(SygerEnv *environment, const char *name);
SygerValue *syger_env_get_ptr(SygerEnv *environment, const char *name);
void syger_env_set(SygerEnv *environment, const char *name, SygerValue value);
void syger_env_set_local(SygerEnv *environment, const char *name, SygerValue value);
void syger_env_define_function(SygerEnv *environment, const char *name, int parameter_count, SygerNativeFunction function);

SygerValue syger_binary(int operator, SygerValue left, SygerValue right);
SygerValue syger_unary(int operator, SygerValue operand);
int syger_require_bool(SygerValue value);
SygerValue syger_index(SygerValue array, SygerValue index);
SygerValue syger_array_at(SygerValue *array, SygerValue index);
SygerValue syger_char_at_ptr(SygerValue *text, SygerValue index);
SygerValue syger_substring_ptr(SygerValue *text, SygerValue start, SygerValue end);
SygerValue syger_length_ptr(SygerValue *value);
SygerValue *syger_index_ptr(SygerValue *array, SygerValue index);
void syger_value_assign(SygerValue *target, SygerValue replacement);
SygerCallArg syger_call_arg(SygerValue value, SygerValue *reference);
SygerValue syger_call(
    SygerEnv *environment,
    const char *name,
    int argument_count,
    SygerCallArg *arguments
);

_Noreturn void syger_fail(const char *message);

#endif
