#include "native_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SygerVariable {
    char *name;
    SygerValue value;
};

struct SygerEnv {
    struct SygerVariable *variables;
    int count;
    SygerFunctionDef *functions;
    int function_count;
    int function_scope;
    SygerEnv *parent;
};

static int runtime_argc = 0;
static char **runtime_argv = NULL;

_Noreturn void syger_fail(const char *message)
{
    fprintf(stderr, "Syger runtime error: %s\n", message);
    exit(1);
}

static char *copy_text(const char *text)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy == NULL) syger_fail("out of memory");
    memcpy(copy, text, length + 1);
    return copy;
}

void syger_runtime_set_arguments(int argc, char **argv)
{
    runtime_argc = argc > 0 ? argc - 1 : 0;
    runtime_argv = argc > 1 ? argv + 1 : NULL;
}

void syger_runtime_shutdown(void)
{
    runtime_argc = 0;
    runtime_argv = NULL;
}

SygerValue syger_value_string(const char *text)
{
    SygerValue value;
    value.type = SYGER_VALUE_STRING;
    value.string = copy_text(text ? text : "");
    return value;
}

SygerValue syger_value_int(long integer)
{
    SygerValue value;
    value.type = SYGER_VALUE_INT;
    value.integer = integer;
    return value;
}

SygerValue syger_value_bool(int boolean)
{
    SygerValue value;
    value.type = SYGER_VALUE_BOOL;
    value.boolean = boolean != 0;
    return value;
}

SygerValue syger_value_none(void)
{
    SygerValue value;
    value.type = SYGER_VALUE_NONE;
    value.integer = 0;
    return value;
}

SygerValue syger_value_array(int count, SygerValue *items)
{
    if (count < 0) syger_fail("negative array size");

    SygerValue value;
    value.type = SYGER_VALUE_ARRAY;
    value.array.count = count;
    value.array.elements = count == 0 ? NULL : malloc(sizeof(SygerValue) * (size_t)count);
    if (count > 0 && value.array.elements == NULL) syger_fail("out of memory");

    for (int i = 0; i < count; i++) {
        value.array.elements[i] = items[i];
        items[i] = syger_value_none();
    }
    return value;
}

SygerValue syger_value_copy(const SygerValue *value)
{
    switch (value->type) {
        case SYGER_VALUE_STRING:
            return syger_value_string(value->string);
        case SYGER_VALUE_INT:
            return syger_value_int(value->integer);
        case SYGER_VALUE_BOOL:
            return syger_value_bool(value->boolean);
        case SYGER_VALUE_NONE:
            return syger_value_none();
        case SYGER_VALUE_ARRAY: {
            SygerValue result;
            result.type = SYGER_VALUE_ARRAY;
            result.array.count = value->array.count;
            result.array.elements = result.array.count == 0 ? NULL : malloc(sizeof(SygerValue) * (size_t)result.array.count);
            if (result.array.count > 0 && result.array.elements == NULL) syger_fail("out of memory");
            for (int i = 0; i < result.array.count; i++) {
                result.array.elements[i] = syger_value_copy(&value->array.elements[i]);
            }
            return result;
        }
    }
    syger_fail("invalid value type");
}

void syger_value_free(SygerValue *value)
{
    if (value == NULL) return;
    if (value->type == SYGER_VALUE_STRING) {
        free(value->string);
        value->string = NULL;
    } else if (value->type == SYGER_VALUE_ARRAY) {
        for (int i = 0; i < value->array.count; i++) syger_value_free(&value->array.elements[i]);
        free(value->array.elements);
        value->array.elements = NULL;
        value->array.count = 0;
    }
    value->type = SYGER_VALUE_NONE;
}

void syger_value_discard(SygerValue value)
{
    syger_value_free(&value);
}

static int values_equal(const SygerValue *left, const SygerValue *right)
{
    if (left->type != right->type) return 0;
    switch (left->type) {
        case SYGER_VALUE_STRING: return strcmp(left->string, right->string) == 0;
        case SYGER_VALUE_INT: return left->integer == right->integer;
        case SYGER_VALUE_BOOL: return left->boolean == right->boolean;
        case SYGER_VALUE_NONE: return 1;
        case SYGER_VALUE_ARRAY:
            if (left->array.count != right->array.count) return 0;
            for (int i = 0; i < left->array.count; i++) {
                if (!values_equal(&left->array.elements[i], &right->array.elements[i])) return 0;
            }
            return 1;
    }
    return 0;
}

static void print_inline(const SygerValue *value)
{
    switch (value->type) {
        case SYGER_VALUE_STRING: fputs(value->string, stdout); break;
        case SYGER_VALUE_INT: printf("%ld", value->integer); break;
        case SYGER_VALUE_BOOL: fputs(value->boolean ? "true" : "false", stdout); break;
        case SYGER_VALUE_NONE: fputs("none", stdout); break;
        case SYGER_VALUE_ARRAY:
            putchar('[');
            for (int i = 0; i < value->array.count; i++) {
                if (i) fputs(", ", stdout);
                print_inline(&value->array.elements[i]);
            }
            putchar(']');
            break;
    }
}

void syger_value_print(const SygerValue *value)
{
    print_inline(value);
    putchar('\n');
}

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} TextBuffer;

static void text_append_n(TextBuffer *buffer, const char *text, size_t length)
{
    if (buffer->length + length + 1 > buffer->capacity) {
        size_t next = buffer->capacity ? buffer->capacity : 64;
        while (next < buffer->length + length + 1) next *= 2;
        char *memory = realloc(buffer->data, next);
        if (memory == NULL) syger_fail("out of memory");
        buffer->data = memory;
        buffer->capacity = next;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

static void render_inline(TextBuffer *buffer, const SygerValue *value)
{
    char number[64];
    switch (value->type) {
        case SYGER_VALUE_STRING:
            text_append_n(buffer, value->string, strlen(value->string));
            break;
        case SYGER_VALUE_INT: {
            int n = snprintf(number, sizeof(number), "%ld", value->integer);
            text_append_n(buffer, number, (size_t)n);
            break;
        }
        case SYGER_VALUE_BOOL: {
            const char *text = value->boolean ? "true" : "false";
            text_append_n(buffer, text, strlen(text));
            break;
        }
        case SYGER_VALUE_NONE:
            text_append_n(buffer, "none", 4);
            break;
        case SYGER_VALUE_ARRAY:
            text_append_n(buffer, "[", 1);
            for (int i = 0; i < value->array.count; i++) {
                if (i) text_append_n(buffer, ", ", 2);
                render_inline(buffer, &value->array.elements[i]);
            }
            text_append_n(buffer, "]", 1);
            break;
    }
}

char *syger_value_render(const SygerValue *value)
{
    TextBuffer buffer = {0};
    render_inline(&buffer, value);
    if (buffer.data == NULL) return copy_text("");
    return buffer.data;
}

SygerEnv *syger_env_new(SygerEnv *parent)
{
    SygerEnv *environment = calloc(1, sizeof(*environment));
    if (environment == NULL) syger_fail("out of memory");
    environment->functions = NULL;
    environment->function_count = 0;
    environment->function_scope = 0;
    environment->parent = parent;
    return environment;
}

SygerEnv *syger_env_new_function(SygerEnv *parent)
{
    SygerEnv *environment = syger_env_new(parent);
    environment->function_scope = 1;
    return environment;
}

static int find_local(SygerEnv *environment, const char *name)
{
    for (int i = 0; i < environment->count; i++) {
        if (strcmp(environment->variables[i].name, name) == 0) return i;
    }
    return -1;
}

SygerValue *syger_env_get_ptr(SygerEnv *environment, const char *name)
{
    for (SygerEnv *current = environment; current != NULL; current = current->parent) {
        int index = find_local(current, name);
        if (index >= 0) return &current->variables[index].value;
    }
    char message[256];
    snprintf(message, sizeof(message), "variable '%s' is not defined", name);
    syger_fail(message);
}

SygerValue syger_env_get(SygerEnv *environment, const char *name)
{
    return syger_value_copy(syger_env_get_ptr(environment, name));
}

void syger_env_set_local(SygerEnv *environment, const char *name, SygerValue value)
{
    int index = find_local(environment, name);
    if (index >= 0) {
        syger_value_free(&environment->variables[index].value);
        environment->variables[index].value = value;
        return;
    }
    struct SygerVariable *next = realloc(environment->variables, sizeof(*next) * (size_t)(environment->count + 1));
    if (next == NULL) syger_fail("out of memory");
    environment->variables = next;
    environment->variables[environment->count].name = copy_text(name);
    environment->variables[environment->count].value = value;
    environment->count++;
}

void syger_env_set(SygerEnv *environment, const char *name, SygerValue value)
{
    for (SygerEnv *current = environment; current != NULL; current = current->parent) {
        int index = find_local(current, name);
        if (index >= 0) {
            syger_value_free(&current->variables[index].value);
            current->variables[index].value = value;
            return;
        }
        if (current->function_scope) break;
    }
    syger_env_set_local(environment, name, value);
}

void syger_env_define_function(SygerEnv *environment, const char *name, int parameter_count, SygerNativeFunction function)
{
    for (SygerEnv *current = environment; current != NULL; current = current->parent) {
        for (int i = 0; i < current->function_count; i++) {
            if (strcmp(current->functions[i].name, name) == 0) {
                current->functions[i].parameter_count = parameter_count;
                current->functions[i].function = function;
                return;
            }
        }
    }
    SygerFunctionDef *next = realloc(environment->functions, sizeof(*next) * (size_t)(environment->function_count + 1));
    if (next == NULL) syger_fail("out of memory");
    environment->functions = next;
    environment->functions[environment->function_count].name = copy_text(name);
    environment->functions[environment->function_count].parameter_count = parameter_count;
    environment->functions[environment->function_count].function = function;
    environment->function_count++;
}

static SygerFunctionDef *syger_env_find_function(SygerEnv *environment, const char *name)
{
    for (SygerEnv *current = environment; current != NULL; current = current->parent) {
        for (int i = 0; i < current->function_count; i++) {
            if (strcmp(current->functions[i].name, name) == 0) return &current->functions[i];
        }
    }
    return NULL;
}

void syger_env_free(SygerEnv *environment)
{
    if (environment == NULL) return;
    for (int i = 0; i < environment->count; i++) {
        free(environment->variables[i].name);
        syger_value_free(&environment->variables[i].value);
    }
    for (int i = 0; i < environment->function_count; i++) free((char *)environment->functions[i].name);
    free(environment->functions);
    free(environment->variables);
    free(environment);
}

void syger_env_free_until(SygerEnv *stop, SygerEnv *current)
{
    while (current != NULL && current != stop) {
        SygerEnv *parent = current->parent;
        syger_env_free(current);
        current = parent;
    }
}

SygerValue syger_binary(int operator, SygerValue left, SygerValue right)
{
    if (left.type == SYGER_VALUE_NONE && right.type == SYGER_VALUE_NONE) {
        int result;
        if (operator == SYGER_OP_EQUAL_EQUAL) result = 1;
        else if (operator == SYGER_OP_BANG_EQUAL) result = 0;
        else syger_fail("unsupported operator for none values");
        syger_value_free(&left); syger_value_free(&right);
        return syger_value_bool(result);
    }

    if (left.type == SYGER_VALUE_STRING && right.type == SYGER_VALUE_STRING) {
        if (operator == SYGER_OP_PLUS) {
            size_t a = strlen(left.string), b = strlen(right.string);
            char *joined = malloc(a + b + 1);
            if (joined == NULL) syger_fail("out of memory");
            memcpy(joined, left.string, a);
            memcpy(joined + a, right.string, b + 1);
            SygerValue result = syger_value_string(joined);
            free(joined); syger_value_free(&left); syger_value_free(&right);
            return result;
        }
        int cmp = strcmp(left.string, right.string);
        syger_value_free(&left); syger_value_free(&right);
        if (operator == SYGER_OP_EQUAL_EQUAL) return syger_value_bool(cmp == 0);
        if (operator == SYGER_OP_BANG_EQUAL) return syger_value_bool(cmp != 0);
        syger_fail("unsupported string operator");
    }

    if (left.type == SYGER_VALUE_BOOL && right.type == SYGER_VALUE_BOOL) {
        int equal = left.boolean == right.boolean;
        syger_value_free(&left); syger_value_free(&right);
        if (operator == SYGER_OP_EQUAL_EQUAL) return syger_value_bool(equal);
        if (operator == SYGER_OP_BANG_EQUAL) return syger_value_bool(!equal);
        syger_fail("unsupported boolean operator");
    }

    if (left.type != SYGER_VALUE_INT || right.type != SYGER_VALUE_INT) {
        syger_value_free(&left); syger_value_free(&right);
        syger_fail("arithmetic and numeric comparisons require integers");
    }
    long a = left.integer, b = right.integer;
    syger_value_free(&left); syger_value_free(&right);
    switch (operator) {
        case SYGER_OP_PLUS: return syger_value_int(a + b);
        case SYGER_OP_MINUS: return syger_value_int(a - b);
        case SYGER_OP_STAR: return syger_value_int(a * b);
        case SYGER_OP_SLASH: if (!b) syger_fail("division by zero"); return syger_value_int(a / b);
        case SYGER_OP_PERCENT: if (!b) syger_fail("modulo by zero"); return syger_value_int(a % b);
        case SYGER_OP_EQUAL_EQUAL: return syger_value_bool(a == b);
        case SYGER_OP_BANG_EQUAL: return syger_value_bool(a != b);
        case SYGER_OP_LESS: return syger_value_bool(a < b);
        case SYGER_OP_LESS_EQUAL: return syger_value_bool(a <= b);
        case SYGER_OP_GREATER: return syger_value_bool(a > b);
        case SYGER_OP_GREATER_EQUAL: return syger_value_bool(a >= b);
        default: syger_fail("unknown binary operator");
    }
}

SygerValue syger_unary(int operator, SygerValue operand)
{
    if (operator == SYGER_OP_NOT) {
        if (operand.type != SYGER_VALUE_BOOL) { syger_value_free(&operand); syger_fail("not requires a boolean"); }
        int value = !operand.boolean; syger_value_free(&operand); return syger_value_bool(value);
    }
    if (operator == SYGER_OP_NEGATE) {
        if (operand.type != SYGER_VALUE_INT) { syger_value_free(&operand); syger_fail("unary minus requires an integer"); }
        long value = -operand.integer; syger_value_free(&operand); return syger_value_int(value);
    }
    syger_value_free(&operand);
    syger_fail("unknown unary operator");
}

int syger_require_bool(SygerValue value)
{
    if (value.type != SYGER_VALUE_BOOL) { syger_value_free(&value); syger_fail("condition must be a boolean"); }
    int result = value.boolean;
    syger_value_free(&value);
    return result;
}

SygerValue syger_index(SygerValue array, SygerValue index)
{
    if (array.type != SYGER_VALUE_ARRAY) { syger_value_free(&array); syger_value_free(&index); syger_fail("indexing requires an array"); }
    if (index.type != SYGER_VALUE_INT) { syger_value_free(&array); syger_value_free(&index); syger_fail("array index must be an integer"); }
    long i = index.integer;
    syger_value_free(&index);
    if (i < 0 || i >= array.array.count) { syger_value_free(&array); syger_fail("array index out of bounds"); }
    SygerValue result = syger_value_copy(&array.array.elements[i]);
    syger_value_free(&array);
    return result;
}

SygerValue *syger_index_ptr(SygerValue *array, SygerValue index)
{
    if (array == NULL || array->type != SYGER_VALUE_ARRAY) { syger_value_free(&index); syger_fail("indexed assignment requires an array"); }
    if (index.type != SYGER_VALUE_INT) { syger_value_free(&index); syger_fail("array index must be an integer"); }
    long i = index.integer;
    syger_value_free(&index);
    if (i < 0 || i >= array->array.count) syger_fail("array index out of bounds");
    return &array->array.elements[i];
}

static long require_int(SygerValue *value, const char *name);

SygerValue syger_array_at(SygerValue *array, SygerValue index)
{
    if (array == NULL || array->type != SYGER_VALUE_ARRAY) { syger_value_free(&index); syger_fail("array_at requires an array"); }
    if (index.type != SYGER_VALUE_INT) { syger_value_free(&index); syger_fail("array_at index must be an integer"); }
    long i = index.integer;
    syger_value_free(&index);
    if (i < 0 || i >= array->array.count) syger_fail("array_at index out of bounds");
    return syger_value_copy(&array->array.elements[i]);
}

SygerValue syger_char_at_ptr(SygerValue *text, SygerValue index)
{
    if (text == NULL || text->type != SYGER_VALUE_STRING) { syger_value_free(&index); syger_fail("char_at expects a string"); }
    if (index.type != SYGER_VALUE_INT) { syger_value_free(&index); syger_fail("char_at index must be an integer"); }
    long i = index.integer;
    syger_value_free(&index);
    size_t length = strlen(text->string);
    if (i < 0 || (size_t)i >= length) return syger_value_string("");
    char ch[2] = { text->string[i], '\0' };
    return syger_value_string(ch);
}

SygerValue syger_substring_ptr(SygerValue *text, SygerValue start_value, SygerValue end_value)
{
    if (text == NULL || text->type != SYGER_VALUE_STRING) { syger_value_free(&start_value); syger_value_free(&end_value); syger_fail("substring expects a string"); }
    long start = require_int(&start_value, "substring");
    long end = require_int(&end_value, "substring");
    syger_value_free(&start_value); syger_value_free(&end_value);
    size_t length = strlen(text->string);
    if (start < 0 || end < start || (size_t)end > length) syger_fail("substring range is out of bounds");
    size_t n = (size_t)(end - start);
    char *buffer = malloc(n + 1);
    if (buffer == NULL) syger_fail("out of memory");
    memcpy(buffer, text->string + start, n); buffer[n] = '\0';
    SygerValue result = syger_value_string(buffer); free(buffer); return result;
}

SygerValue syger_length_ptr(SygerValue *value)
{
    if (value == NULL) syger_fail("length requires a string or array");
    if (value->type == SYGER_VALUE_STRING) return syger_value_int((long)strlen(value->string));
    if (value->type == SYGER_VALUE_ARRAY) return syger_value_int(value->array.count);
    syger_fail("length requires a string or array");
}

void syger_value_assign(SygerValue *target, SygerValue replacement)
{
    if (target == NULL) { syger_value_free(&replacement); syger_fail("invalid assignment target"); }
    syger_value_free(target);
    *target = replacement;
}

SygerCallArg syger_call_arg(SygerValue value, SygerValue *reference)
{
    SygerCallArg argument = { value, reference };
    return argument;
}

static void check_argc(const char *name, int actual, int expected)
{
    if (actual != expected) {
        char message[256];
        snprintf(message, sizeof(message), "%s expects %d argument(s), got %d", name, expected, actual);
        syger_fail(message);
    }
}

static long require_int(SygerValue *value, const char *name)
{
    if (value->type != SYGER_VALUE_INT) {
        char message[160]; snprintf(message, sizeof(message), "%s requires an integer", name); syger_fail(message);
    }
    return value->integer;
}

static SygerValue run_builtin(const char *name, int argc, SygerCallArg *args)
{
    if (strcmp(name, "print") == 0) {
        for (int i = 0; i < argc; i++) syger_value_print(&args[i].value);
        return syger_value_int(0);
    }
    if (strcmp(name, "token_at") == 0) {
        check_argc(name, argc, 2);
        SygerValue *array = args[0].reference;
        if (!array || array->type != SYGER_VALUE_ARRAY) syger_fail("token_at requires an array variable");
        long index = require_int(&args[1].value, "token_at");
        if (index < 0 || index >= array->array.count) syger_fail("token_at index out of bounds");
        return syger_value_copy(&array->array.elements[index]);
    }
    if (strcmp(name, "type_of") == 0) {
        check_argc(name, argc, 1);
        const char *type_name = "none";
        switch (args[0].value.type) {
            case SYGER_VALUE_STRING: type_name = "string"; break;
            case SYGER_VALUE_INT: type_name = "integer"; break;
            case SYGER_VALUE_BOOL: type_name = "boolean"; break;
            case SYGER_VALUE_NONE: type_name = "none"; break;
            case SYGER_VALUE_ARRAY: type_name = "array"; break;
        }
        return syger_value_string(type_name);
    }
    if (strcmp(name, "fail") == 0) {
        check_argc(name, argc, 1);
        if (args[0].value.type != SYGER_VALUE_STRING) syger_fail("fail requires a string message");
        syger_fail(args[0].value.string);
    }
    if (strcmp(name, "args") == 0) {
        check_argc(name, argc, 0);
        SygerValue *items = runtime_argc ? malloc(sizeof(SygerValue) * (size_t)runtime_argc) : NULL;
        if (runtime_argc && items == NULL) syger_fail("out of memory");
        for (int i = 0; i < runtime_argc; i++) items[i] = syger_value_string(runtime_argv[i]);
        SygerValue result = syger_value_array(runtime_argc, items);
        free(items);
        return result;
    }
    if (strcmp(name, "read_file") == 0) {
        check_argc(name, argc, 1);
        if (args[0].value.type != SYGER_VALUE_STRING) syger_fail("read_file requires a string path");
        const char *path = args[0].value.string;
        FILE *file = fopen(path, "rb");
        if (!file) { char msg[512]; snprintf(msg, sizeof(msg), "could not open file '%s'", path); syger_fail(msg); }
        if (fseek(file, 0, SEEK_END) != 0) { fclose(file); syger_fail("could not seek file"); }
        long length = ftell(file);
        if (length < 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); syger_fail("could not determine file length"); }
        char *buffer = malloc((size_t)length + 1);
        if (!buffer) { fclose(file); syger_fail("out of memory"); }
        size_t n = fread(buffer, 1, (size_t)length, file);
        int failed = ferror(file); fclose(file);
        if (failed) { free(buffer); syger_fail("could not read file"); }
        buffer[n] = '\0'; SygerValue result = syger_value_string(buffer); free(buffer); return result;
    }
    if (strcmp(name, "write_file") == 0) {
        check_argc(name, argc, 2);
        if (args[0].value.type != SYGER_VALUE_STRING || args[1].value.type != SYGER_VALUE_STRING) syger_fail("write_file requires two strings");
        FILE *file = fopen(args[0].value.string, "wb");
        if (!file) return syger_value_bool(0);
        size_t length = strlen(args[1].value.string);
        size_t written = fwrite(args[1].value.string, 1, length, file);
        int closed = fclose(file);
        return syger_value_bool(written == length && closed == 0);
    }
    if (strcmp(name, "char_at") == 0) {
        check_argc(name, argc, 2);
        if (args[0].value.type != SYGER_VALUE_STRING) syger_fail("char_at expects a string");
        long index = require_int(&args[1].value, "char_at");
        size_t length = strlen(args[0].value.string);
        if (index < 0 || (size_t)index >= length) return syger_value_string("");
        char character[2] = { args[0].value.string[index], '\0' };
        return syger_value_string(character);
    }
    if (strcmp(name, "substring") == 0) {
        check_argc(name, argc, 3);
        if (args[0].value.type != SYGER_VALUE_STRING) syger_fail("substring expects a string");
        long start = require_int(&args[1].value, "substring");
        long end = require_int(&args[2].value, "substring");
        size_t length = strlen(args[0].value.string);
        if (start < 0 || end < start || (size_t)end > length) syger_fail("substring range is out of bounds");
        size_t n = (size_t)(end - start); char *buffer = malloc(n + 1);
        if (!buffer) syger_fail("out of memory");
        memcpy(buffer, args[0].value.string + start, n); buffer[n] = '\0';
        SygerValue result = syger_value_string(buffer); free(buffer); return result;
    }
    if (strcmp(name, "chr") == 0) {
        check_argc(name, argc, 1);
        long code = require_int(&args[0].value, "chr");
        if (code < 0 || code > 127) syger_fail("chr supports ASCII values 0 through 127");
        char character[2] = { (char)code, '\0' }; return syger_value_string(character);
    }
    if (strcmp(name, "to_string") == 0) {
        check_argc(name, argc, 1); char *text = syger_value_render(&args[0].value);
        SygerValue result = syger_value_string(text); free(text); return result;
    }
    if (strcmp(name, "length") == 0) {
        check_argc(name, argc, 1);
        if (args[0].value.type == SYGER_VALUE_STRING) return syger_value_int((long)strlen(args[0].value.string));
        if (args[0].value.type == SYGER_VALUE_ARRAY) return syger_value_int(args[0].value.array.count);
        syger_fail("length requires a string or array");
    }
    if (strcmp(name, "append") == 0) {
        check_argc(name, argc, 2);
        SygerValue *array = args[0].reference;
        if (!array || array->type != SYGER_VALUE_ARRAY) syger_fail("append requires an array variable or indexed array");
        SygerValue *items = realloc(array->array.elements, sizeof(SygerValue) * (size_t)(array->array.count + 1));
        if (!items) syger_fail("out of memory");
        array->array.elements = items; array->array.elements[array->array.count++] = args[1].value; args[1].value = syger_value_none();
        return syger_value_int(0);
    }
    if (strcmp(name, "pop") == 0) {
        check_argc(name, argc, 1); SygerValue *array = args[0].reference;
        if (!array || array->type != SYGER_VALUE_ARRAY) syger_fail("pop requires an array variable or indexed array");
        if (!array->array.count) syger_fail("pop cannot remove from an empty array");
        SygerValue result = array->array.elements[array->array.count - 1]; array->array.count--;
        if (!array->array.count) { free(array->array.elements); array->array.elements = NULL; }
        else { SygerValue *items = realloc(array->array.elements, sizeof(SygerValue) * (size_t)array->array.count); if (items) array->array.elements = items; }
        return result;
    }
    if (strcmp(name, "insert") == 0) {
        check_argc(name, argc, 3); SygerValue *array = args[0].reference;
        if (!array || array->type != SYGER_VALUE_ARRAY) syger_fail("insert requires an array variable or indexed array");
        long index = require_int(&args[1].value, "insert");
        if (index < 0 || index > array->array.count) syger_fail("insert index out of bounds");
        SygerValue *items = realloc(array->array.elements, sizeof(SygerValue) * (size_t)(array->array.count + 1)); if (!items) syger_fail("out of memory");
        array->array.elements = items;
        for (int i = array->array.count; i > index; i--) array->array.elements[i] = array->array.elements[i - 1];
        array->array.elements[index] = args[2].value; args[2].value = syger_value_none(); array->array.count++; return syger_value_int(0);
    }
    if (strcmp(name, "remove") == 0) {
        check_argc(name, argc, 2); SygerValue *array = args[0].reference;
        if (!array || array->type != SYGER_VALUE_ARRAY) syger_fail("remove requires an array variable or indexed array");
        long index = require_int(&args[1].value, "remove"); if (index < 0 || index >= array->array.count) syger_fail("remove index out of bounds");
        SygerValue result = array->array.elements[index];
        for (int i = (int)index; i < array->array.count - 1; i++) array->array.elements[i] = array->array.elements[i + 1];
        array->array.count--;
        if (!array->array.count) { free(array->array.elements); array->array.elements = NULL; }
        else { SygerValue *items = realloc(array->array.elements, sizeof(SygerValue) * (size_t)array->array.count); if (items) array->array.elements = items; }
        return result;
    }
    if (strcmp(name, "clear") == 0) {
        check_argc(name, argc, 1); SygerValue *array = args[0].reference;
        if (!array || array->type != SYGER_VALUE_ARRAY) syger_fail("clear requires an array variable or indexed array");
        for (int i = 0; i < array->array.count; i++) syger_value_free(&array->array.elements[i]);
        free(array->array.elements); array->array.elements = NULL; array->array.count = 0; return syger_value_int(0);
    }
    if (strcmp(name, "reverse") == 0 || strcmp(name, "sort") == 0) {
        check_argc(name, argc, 1); SygerValue *array = args[0].reference;
        if (!array || array->type != SYGER_VALUE_ARRAY) syger_fail("array mutator requires an array variable or indexed array");
        if (strcmp(name, "reverse") == 0) {
            for (int i = 0, j = array->array.count - 1; i < j; i++, j--) { SygerValue tmp = array->array.elements[i]; array->array.elements[i] = array->array.elements[j]; array->array.elements[j] = tmp; }
        } else {
            for (int i = 0; i < array->array.count; i++) if (array->array.elements[i].type != SYGER_VALUE_INT) syger_fail("sort requires an array of integers");
            for (int i = 0; i < array->array.count; i++) for (int j = i + 1; j < array->array.count; j++) if (array->array.elements[j].integer < array->array.elements[i].integer) { SygerValue tmp = array->array.elements[i]; array->array.elements[i] = array->array.elements[j]; array->array.elements[j] = tmp; }
        }
        return syger_value_int(0);
    }
    if (strcmp(name, "contains") == 0 || strcmp(name, "index_of") == 0) {
        check_argc(name, argc, 2); if (args[0].value.type != SYGER_VALUE_ARRAY) syger_fail("contains/index_of requires an array");
        long found = -1;
        for (int i = 0; i < args[0].value.array.count; i++) if (values_equal(&args[0].value.array.elements[i], &args[1].value)) { found = i; break; }
        if (strcmp(name, "contains") == 0) return syger_value_bool(found >= 0);
        return syger_value_int(found);
    }
    if (strcmp(name, "slice") == 0) {
        check_argc(name, argc, 3); if (args[0].value.type != SYGER_VALUE_ARRAY) syger_fail("slice requires an array");
        long start = require_int(&args[1].value, "slice"), end = require_int(&args[2].value, "slice");
        if (start < 0 || end < start || end > args[0].value.array.count) syger_fail("slice indices are out of bounds");
        int count = (int)(end - start); SygerValue *items = count ? malloc(sizeof(SygerValue) * (size_t)count) : NULL;
        if (count && !items) syger_fail("out of memory");
        for (int i = 0; i < count; i++) items[i] = syger_value_copy(&args[0].value.array.elements[start + i]);
        SygerValue result = syger_value_array(count, items);
        free(items);
        return result;
    }
    if (strcmp(name, "join") == 0) {
        check_argc(name, argc, 2); if (args[0].value.type != SYGER_VALUE_ARRAY || args[1].value.type != SYGER_VALUE_STRING) syger_fail("join requires an array and string separator");
        size_t total = 1, separator_length = strlen(args[1].value.string);
        for (int i = 0; i < args[0].value.array.count; i++) { if (args[0].value.array.elements[i].type != SYGER_VALUE_STRING) syger_fail("join requires an array of strings"); total += strlen(args[0].value.array.elements[i].string); if (i) total += separator_length; }
        char *buffer = malloc(total); if (!buffer) syger_fail("out of memory"); buffer[0] = '\0';
        for (int i = 0; i < args[0].value.array.count; i++) { if (i) strcat(buffer, args[1].value.string); strcat(buffer, args[0].value.array.elements[i].string); }
        SygerValue result = syger_value_string(buffer); free(buffer); return result;
    }
    if (strcmp(name, "sum") == 0 || strcmp(name, "min") == 0 || strcmp(name, "max") == 0) {
        check_argc(name, argc, 1); if (args[0].value.type != SYGER_VALUE_ARRAY || args[0].value.array.count == 0) syger_fail("sum/min/max require a non-empty array");
        long result = strcmp(name, "sum") == 0 ? 0 : args[0].value.array.elements[0].integer;
        for (int i = 0; i < args[0].value.array.count; i++) {
            SygerValue *v = &args[0].value.array.elements[i];
            if (v->type != SYGER_VALUE_INT) syger_fail("sum/min/max require integers");
            if (strcmp(name, "sum") == 0) result += v->integer;
            else if (strcmp(name, "min") == 0 && v->integer < result) result = v->integer;
            else if (strcmp(name, "max") == 0 && v->integer > result) result = v->integer;
        }
        return syger_value_int(result);
    }
    if (strcmp(name, "first") == 0 || strcmp(name, "last") == 0) {
        check_argc(name, argc, 1); if (args[0].value.type != SYGER_VALUE_ARRAY || args[0].value.array.count == 0) syger_fail("first/last require a non-empty array");
        int index = strcmp(name, "first") == 0 ? 0 : args[0].value.array.count - 1; return syger_value_copy(&args[0].value.array.elements[index]);
    }

    char message[256]; snprintf(message, sizeof(message), "unknown function '%s'", name); syger_fail(message);
}

SygerValue syger_call(
    SygerEnv *environment,
    const char *name,
    int argument_count,
    SygerCallArg *arguments
)
{
    static const char *const builtins[] = {
        "print", "type_of", "token_at", "fail", "args", "read_file", "write_file", "char_at", "substring", "chr", "to_string",
        "length", "append", "pop", "insert", "remove", "clear", "reverse", "sort", "contains", "index_of", "slice", "join", "sum", "min", "max", "first", "last"
    };
    int builtin = 0;
    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        if (strcmp(name, builtins[i]) == 0) { builtin = 1; break; }
    }

    SygerValue result;
    if (builtin) {
        result = run_builtin(name, argument_count, arguments);
    } else {
        SygerFunctionDef *found = syger_env_find_function(environment, name);
        if (!found) { char message[256]; snprintf(message, sizeof(message), "function '%s' is not defined", name); syger_fail(message); }
        if (found->parameter_count != argument_count) { char message[256]; snprintf(message, sizeof(message), "function '%s' expects %d argument(s), got %d", name, found->parameter_count, argument_count); syger_fail(message); }
        SygerValue *values = argument_count ? malloc(sizeof(SygerValue) * (size_t)argument_count) : NULL;
        if (argument_count && !values) syger_fail("out of memory");
        for (int i = 0; i < argument_count; i++) { values[i] = arguments[i].value; arguments[i].value = syger_value_none(); }
        SygerNativeFunction function = found->function;
        result = function(environment, values, argument_count);
        for (int i = 0; i < argument_count; i++) syger_value_free(&values[i]);
        free(values);
    }
    for (int i = 0; i < argument_count; i++) syger_value_free(&arguments[i].value);
    return result;
}
