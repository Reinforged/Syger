#include "native_runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int runtime_argc = 0;
static char **runtime_argv = NULL;

/* ---- errors -------------------------------------------------------------- */

long syger_line = 0;
const char *syger_source_path = NULL;
SygerFrame syger_frames[SYGER_MAX_FRAMES];
int syger_depth = 0;

static int use_color(void)
{
    static int decided = 0, color = 0;
    if (!decided) { color = isatty(fileno(stderr)) && getenv("NO_COLOR") == NULL; decided = 1; }
    return color;
}

#define COLOR(code) (use_color() ? code : "")

static int count_digits(long n)
{
    int count = 1;
    while (n >= 10) { n /= 10; count++; }
    return count;
}

/* Prints one line of the source file, if it can still be read. */
static int print_source_line(long line, int width)
{
    if (syger_source_path == NULL || line <= 0) return 0;
    FILE *file = fopen(syger_source_path, "rb");
    if (file == NULL) return 0;
    long current = 1;
    int c;
    while (current < line && (c = fgetc(file)) != EOF) if (c == '\n') current++;
    if (current != line) { fclose(file); return 0; }
    fprintf(stderr, "%*s %s|%s\n", width, "", COLOR("\033[1;34m"), COLOR("\033[0m"));
    fprintf(stderr, "%s%*ld |%s ", COLOR("\033[1;34m"), width, line, COLOR("\033[0m"));
    while ((c = fgetc(file)) != EOF && c != '\n' && c != '\r') fputc(c, stderr);
    fputc('\n', stderr);
    fprintf(stderr, "%*s %s|%s\n", width, "", COLOR("\033[1;34m"), COLOR("\033[0m"));
    fclose(file);
    return 1;
}

static _Noreturn void report(const char *code, const char *help, const char *message)
{
    fflush(stdout);
    int width = count_digits(syger_line > 0 ? syger_line : 1);
    fprintf(stderr, "%serror[%s]%s%s: %s%s\n", COLOR("\033[1;31m"), code, COLOR("\033[0m"), COLOR("\033[1m"), message, COLOR("\033[0m"));
    if (syger_source_path != NULL && syger_line > 0) {
        fprintf(stderr, "%*s%s-->%s %s:%ld\n", width, "", COLOR("\033[1;34m"), COLOR("\033[0m"), syger_source_path, syger_line);
        print_source_line(syger_line, width);
    }
    if (help != NULL && help[0] != '\0') {
        fprintf(stderr, "%*s %s= help:%s %s\n", width, "", COLOR("\033[1;36m"), COLOR("\033[0m"), help);
    }
    int shown = 0;
    for (int i = syger_depth - 1; i >= 0 && shown < 10; i--, shown++) {
        if (i >= SYGER_MAX_FRAMES) continue;
        fprintf(stderr, "%*s %s= note:%s in function '%s', called from line %ld\n",
                width, "", COLOR("\033[1;36m"), COLOR("\033[0m"), syger_frames[i].name, syger_frames[i].call_line);
    }
    if (syger_depth > shown) {
        fprintf(stderr, "%*s %s= note:%s ... %d more calls\n", width, "", COLOR("\033[1;36m"), COLOR("\033[0m"), syger_depth - shown);
    }
    exit(1);
}

_Noreturn void syger_error(const char *code, const char *help, const char *format, ...)
{
    char message[1024];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    report(code, help, message);
}

/* Only for conditions that indicate a bug in Syger itself. */
_Noreturn void syger_fail(const char *message)
{
    report("E9001", "this is a bug in Syger; please report it", message);
}

_Noreturn void syger_failf(const char *format, ...)
{
    char message[1024];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    report("E9001", "this is a bug in Syger; please report it", message);
}

static const char *type_name(SygerType type)
{
    switch (type) {
        case SYGER_STRING: return "string";
        case SYGER_INT: return "integer";
        case SYGER_BOOL: return "boolean";
        case SYGER_ARRAY: return "array";
        case SYGER_NONE: return "none";
        case SYGER_UNDEFINED: return "undefined";
    }
    return "unknown";
}

_Noreturn void syger_undefined_variable(const char *name)
{
    syger_error("E3001", "it is a global that has not been assigned yet; assign it before this line runs", "variable '%s' is not defined", name);
}

_Noreturn void syger_type_error(const char *code, SygerValue value, const char *message)
{
    syger_error(code, NULL, "%s (got %s)", message, type_name(value.type));
}

_Noreturn void syger_arith_error(SygerValue left, SygerValue right)
{
    const char *help = left.type == SYGER_STRING || right.type == SYGER_STRING
        ? "convert the other value first, e.g. \"total: \" + to_string(count)"
        : "arithmetic and comparisons like < need two integers";
    syger_error("E3018", help, "cannot use an arithmetic or comparison operator on %s and %s",
                type_name(left.type), type_name(right.type));
}

_Noreturn void syger_index_error(SygerValue container, SygerValue index)
{
    if (container.type != SYGER_ARRAY) syger_error("E3008", NULL, "indexing requires an array (got %s)", type_name(container.type));
    if (index.type != SYGER_INT) syger_error("E3009", NULL, "array index must be an integer (got %s)", type_name(index.type));
    char help[128];
    long count = container.as.array->count;
    if (count == 0) snprintf(help, sizeof(help), "the array is empty");
    else snprintf(help, sizeof(help), "valid indexes are 0 to %ld", count - 1);
    syger_error("E3007", help, "index %ld is out of bounds for an array of length %ld", index.as.integer, count);
}

static void *checked_malloc(size_t size)
{
    void *memory = malloc(size ? size : 1);
    if (memory == NULL) syger_error("E0003", NULL, "out of memory");
    return memory;
}

static void *checked_realloc(void *pointer, size_t size)
{
    void *memory = realloc(pointer, size ? size : 1);
    if (memory == NULL) syger_error("E0003", NULL, "out of memory");
    return memory;
}

/* ---- program lifetime ---------------------------------------------------- */

void syger_runtime_set_arguments(int argc, char **argv)
{
    runtime_argc = argc > 0 ? argc - 1 : 0;
    runtime_argv = argc > 1 ? argv + 1 : NULL;
}

void syger_runtime_shutdown(void)
{
    fflush(stdout);
}

/* ---- strings ------------------------------------------------------------- */

static SygerString *string_alloc(long length, long capacity)
{
    if (capacity < length) capacity = length;
    SygerString *string = checked_malloc(sizeof(SygerString) + (size_t)capacity + 1);
    string->refcount = 1;
    string->length = length;
    string->capacity = capacity;
    string->data[length] = '\0';
    return string;
}

static SygerValue string_value(SygerString *string)
{
    SygerValue value;
    value.type = SYGER_STRING;
    value.as.string = string;
    return value;
}

SygerValue syger_string_n(const char *text, long length)
{
    SygerString *string = string_alloc(length, length);
    memcpy(string->data, text, (size_t)length);
    return string_value(string);
}

SygerValue syger_string(const char *text)
{
    return syger_string_n(text ? text : "", text ? (long)strlen(text) : 0);
}

SygerValue syger_string_literal(const char *text, long length)
{
    SygerValue value = syger_string_n(text, length);
    value.as.string->refcount = -1;
    return value;
}

/* Immortal one-character strings so char_at never allocates. */
static SygerValue single_chars[256];
static int single_chars_ready = 0;

static SygerValue single_char(unsigned char c)
{
    if (!single_chars_ready) {
        for (int i = 0; i < 256; i++) {
            char text = (char)i;
            single_chars[i] = syger_string_literal(&text, 1);
        }
        single_chars_ready = 1;
    }
    return single_chars[c];
}

static SygerValue empty_string(void)
{
    static SygerValue empty;
    static int ready = 0;
    if (!ready) { empty = syger_string_literal("", 0); ready = 1; }
    return empty;
}

/* ---- arrays -------------------------------------------------------------- */

static SygerArray *array_alloc(long capacity)
{
    SygerArray *array = checked_malloc(sizeof(SygerArray));
    array->refcount = 1;
    array->count = 0;
    array->capacity = capacity;
    array->items = capacity ? checked_malloc(sizeof(SygerValue) * (size_t)capacity) : NULL;
    return array;
}

static SygerValue array_value(SygerArray *array)
{
    SygerValue value;
    value.type = SYGER_ARRAY;
    value.as.array = array;
    return value;
}

SygerValue syger_array_new(long count, SygerValue *items)
{
    SygerArray *array = array_alloc(count);
    for (long i = 0; i < count; i++) array->items[i] = items[i];
    array->count = count;
    return array_value(array);
}

static void array_reserve(SygerArray *array, long needed)
{
    if (needed <= array->capacity) return;
    long capacity = array->capacity ? array->capacity * 2 : 4;
    while (capacity < needed) capacity *= 2;
    array->items = checked_realloc(array->items, sizeof(SygerValue) * (size_t)capacity);
    array->capacity = capacity;
}

void syger_make_unique(SygerValue *value)
{
    SygerArray *shared = value->as.array;
    if (shared->refcount == 1) return;
    SygerArray *copy = array_alloc(shared->count);
    for (long i = 0; i < shared->count; i++) copy->items[i] = syger_retain(shared->items[i]);
    copy->count = shared->count;
    SygerValue old = *value;
    *value = array_value(copy);
    syger_release(old);
}

void syger_free_object(SygerValue value)
{
    if (value.type == SYGER_STRING) {
        free(value.as.string);
    } else if (value.type == SYGER_ARRAY) {
        SygerArray *array = value.as.array;
        for (long i = 0; i < array->count; i++) syger_release(array->items[i]);
        free(array->items);
        free(array);
    }
}

/* ---- equality and rendering ---------------------------------------------- */

int syger_equal(const SygerValue *left, const SygerValue *right)
{
    if (left->type != right->type) return 0;
    switch (left->type) {
        case SYGER_INT: return left->as.integer == right->as.integer;
        case SYGER_BOOL: return left->as.boolean == right->as.boolean;
        case SYGER_NONE: case SYGER_UNDEFINED: return 1;
        case SYGER_STRING:
            return left->as.string == right->as.string ||
                   (left->as.string->length == right->as.string->length &&
                    memcmp(left->as.string->data, right->as.string->data, (size_t)left->as.string->length) == 0);
        case SYGER_ARRAY: {
            SygerArray *a = left->as.array, *b = right->as.array;
            if (a == b) return 1;
            if (a->count != b->count) return 0;
            for (long i = 0; i < a->count; i++) if (!syger_equal(&a->items[i], &b->items[i])) return 0;
            return 1;
        }
    }
    return 0;
}

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} TextBuffer;

static void text_append(TextBuffer *buffer, const char *text, size_t length)
{
    if (buffer->length + length + 1 > buffer->capacity) {
        size_t next = buffer->capacity ? buffer->capacity : 64;
        while (next < buffer->length + length + 1) next *= 2;
        buffer->data = checked_realloc(buffer->data, next);
        buffer->capacity = next;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

static void render(TextBuffer *buffer, const SygerValue *value)
{
    char number[32];
    switch (value->type) {
        case SYGER_STRING: text_append(buffer, value->as.string->data, (size_t)value->as.string->length); break;
        case SYGER_INT: text_append(buffer, number, (size_t)snprintf(number, sizeof(number), "%ld", value->as.integer)); break;
        case SYGER_BOOL: if (value->as.boolean) text_append(buffer, "true", 4); else text_append(buffer, "false", 5); break;
        case SYGER_NONE: case SYGER_UNDEFINED: text_append(buffer, "none", 4); break;
        case SYGER_ARRAY:
            text_append(buffer, "[", 1);
            for (long i = 0; i < value->as.array->count; i++) {
                if (i) text_append(buffer, ", ", 2);
                render(buffer, &value->as.array->items[i]);
            }
            text_append(buffer, "]", 1);
            break;
    }
}

/* ---- string operators ---------------------------------------------------- */

SygerValue syger_add_slow(SygerValue left, SygerValue right)
{
    if (left.type != SYGER_STRING || right.type != SYGER_STRING) {
        if (left.type == SYGER_STRING || right.type == SYGER_STRING) {
            syger_error("E3018", "convert the other value first, e.g. \"total: \" + to_string(count)", "cannot use '+' on %s and %s", type_name(left.type), type_name(right.type));
        }
        syger_arith_error(left, right);
    }
    SygerString *a = left.as.string, *b = right.as.string;
    SygerString *joined = string_alloc(a->length + b->length, a->length + b->length);
    memcpy(joined->data, a->data, (size_t)a->length);
    memcpy(joined->data + a->length, b->data, (size_t)b->length);
    syger_release(left);
    syger_release(right);
    return string_value(joined);
}

void syger_append_assign(SygerValue *slot, SygerValue value)
{
    if (slot->type == SYGER_STRING && value.type == SYGER_STRING && slot->as.string->refcount == 1) {
        SygerString *target = slot->as.string;
        long needed = target->length + value.as.string->length;
        if (needed > target->capacity) {
            long capacity = target->capacity * 2;
            if (capacity < needed) capacity = needed;
            if (capacity < 16) capacity = 16;
            target = checked_realloc(target, sizeof(SygerString) + (size_t)capacity + 1);
            target->capacity = capacity;
            slot->as.string = target;
        }
        memcpy(target->data + target->length, value.as.string->data, (size_t)value.as.string->length);
        target->length = needed;
        target->data[needed] = '\0';
        syger_release(value);
        return;
    }
    syger_set(slot, syger_add(syger_retain(*slot), value));
}

/* ---- builtins ------------------------------------------------------------ */

static long require_int(SygerValue value, const char *name)
{
    if (value.type != SYGER_INT) syger_error("E3011", NULL, "%s requires an integer (got %s)", name, type_name(value.type));
    return value.as.integer;
}

static SygerString *require_string(SygerValue value, const char *name)
{
    if (value.type != SYGER_STRING) syger_error("E3011", NULL, "%s requires a string (got %s)", name, type_name(value.type));
    return value.as.string;
}

static SygerArray *require_array(SygerValue value, const char *name)
{
    if (value.type != SYGER_ARRAY) syger_error("E3011", NULL, "%s requires an array (got %s)", name, type_name(value.type));
    return value.as.array;
}

/* Lvalue arrays are made unique before being mutated in place. */
static SygerArray *mutable_array(SygerValue *value, const char *name)
{
    require_array(*value, name);
    if (value->as.array->refcount != 1) syger_make_unique(value);
    return value->as.array;
}

SygerValue syger_b_print(SygerValue value)
{
    if (value.type == SYGER_STRING) {
        fwrite(value.as.string->data, 1, (size_t)value.as.string->length, stdout);
    } else if (value.type == SYGER_INT) {
        printf("%ld", value.as.integer);
    } else {
        TextBuffer buffer = {0};
        render(&buffer, &value);
        if (buffer.data) fwrite(buffer.data, 1, buffer.length, stdout);
        free(buffer.data);
    }
    putchar('\n');
    syger_release(value);
    return syger_int(0);
}

SygerValue syger_b_eprint(SygerValue value)
{
    fflush(stdout);
    TextBuffer buffer = {0};
    render(&buffer, &value);
    if (buffer.data) fwrite(buffer.data, 1, buffer.length, stderr);
    fputc('\n', stderr);
    free(buffer.data);
    syger_release(value);
    return syger_int(0);
}

SygerValue syger_b_exit(SygerValue code)
{
    long value = require_int(code, "exit");
    fflush(stdout);
    exit((int)value);
}

SygerValue syger_b_type_of(SygerValue value)
{
    SygerValue result = syger_string(type_name(value.type));
    syger_release(value);
    return result;
}

SygerValue syger_b_fail(SygerValue message)
{
    report("E3022", NULL, require_string(message, "fail")->data);
}

SygerValue syger_b_args(void)
{
    SygerArray *array = array_alloc(runtime_argc);
    for (int i = 0; i < runtime_argc; i++) array->items[i] = syger_string(runtime_argv[i]);
    array->count = runtime_argc;
    return array_value(array);
}

SygerValue syger_b_read_file(SygerValue path_value)
{
    const char *path = require_string(path_value, "read_file")->data;
    FILE *file = fopen(path, "rb");
    if (!file) syger_error("E3020", "check that the file exists and that the path is relative to where you run the program", "read_file could not open '%s'", path);
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); syger_error("E3021", NULL, "read_file could not read '%s'", path); }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); syger_error("E3021", NULL, "read_file could not read '%s'", path); }
    SygerString *string = string_alloc(length, length);
    size_t n = fread(string->data, 1, (size_t)length, file);
    int failed = ferror(file);
    fclose(file);
    if (failed) syger_error("E3021", NULL, "read_file could not read '%s'", path);
    string->length = (long)n;
    string->data[n] = '\0';
    syger_release(path_value);
    return string_value(string);
}

SygerValue syger_b_write_file(SygerValue path, SygerValue text)
{
    SygerString *p = require_string(path, "write_file");
    SygerString *t = require_string(text, "write_file");
    FILE *file = fopen(p->data, "wb");
    int ok = 0;
    if (file) {
        size_t written = fwrite(t->data, 1, (size_t)t->length, file);
        ok = fclose(file) == 0 && written == (size_t)t->length;
    }
    syger_release(path);
    syger_release(text);
    return syger_bool(ok);
}

SygerValue syger_b_char_at(SygerValue text, SygerValue index_value)
{
    SygerString *s = require_string(text, "char_at");
    long index = require_int(index_value, "char_at");
    SygerValue result = (index < 0 || index >= s->length) ? empty_string() : single_char((unsigned char)s->data[index]);
    syger_release(text);
    return result;
}

SygerValue syger_b_substring(SygerValue text, SygerValue start_value, SygerValue end_value)
{
    SygerString *s = require_string(text, "substring");
    long start = require_int(start_value, "substring");
    long end = require_int(end_value, "substring");
    if (start < 0 || end < start || end > s->length) {
        syger_error("E3007", "start and end must satisfy 0 <= start <= end <= length(text)", "substring range %ld..%ld is out of bounds for a string of length %ld", start, end, s->length);
    }
    SygerValue result = end - start == 1 ? single_char((unsigned char)s->data[start]) : syger_string_n(s->data + start, end - start);
    syger_release(text);
    return result;
}

SygerValue syger_b_chr(SygerValue code_value)
{
    long code = require_int(code_value, "chr");
    if (code < 0 || code > 127) syger_error("E3019", "chr supports ASCII codes 0 through 127", "chr code %ld is out of range", code);
    return single_char((unsigned char)code);
}

SygerValue syger_b_to_string(SygerValue value)
{
    if (value.type == SYGER_STRING) return value;
    TextBuffer buffer = {0};
    render(&buffer, &value);
    SygerValue result = buffer.data ? syger_string_n(buffer.data, (long)buffer.length) : empty_string();
    free(buffer.data);
    syger_release(value);
    return result;
}

SygerValue syger_b_length(SygerValue value)
{
    long length;
    if (value.type == SYGER_STRING) length = value.as.string->length;
    else if (value.type == SYGER_ARRAY) length = value.as.array->count;
    else syger_type_error("E3011", value, "length requires a string or array");
    syger_release(value);
    return syger_int(length);
}

SygerValue syger_b_token_at(SygerValue array, SygerValue index)
{
    return syger_index(array, index);
}

static long find_item(SygerArray *array, const SygerValue *item)
{
    for (long i = 0; i < array->count; i++) if (syger_equal(&array->items[i], item)) return i;
    return -1;
}

SygerValue syger_b_contains(SygerValue array, SygerValue item)
{
    long found = find_item(require_array(array, "contains"), &item);
    syger_release(array);
    syger_release(item);
    return syger_bool(found >= 0);
}

SygerValue syger_b_index_of(SygerValue array, SygerValue item)
{
    long found = find_item(require_array(array, "index_of"), &item);
    syger_release(array);
    syger_release(item);
    return syger_int(found);
}

SygerValue syger_b_slice(SygerValue array_value_in, SygerValue start_value, SygerValue end_value)
{
    SygerArray *source = require_array(array_value_in, "slice");
    long start = require_int(start_value, "slice"), end = require_int(end_value, "slice");
    if (start < 0 || end < start || end > source->count) syger_error("E3007", "start and end must satisfy 0 <= start <= end <= length(array)", "slice range %ld..%ld is out of bounds for an array of length %ld", start, end, source->count);
    SygerArray *result = array_alloc(end - start);
    for (long i = start; i < end; i++) result->items[i - start] = syger_retain(source->items[i]);
    result->count = end - start;
    syger_release(array_value_in);
    return array_value(result);
}

SygerValue syger_b_join(SygerValue array, SygerValue separator)
{
    SygerArray *items = require_array(array, "join");
    SygerString *sep = require_string(separator, "join");
    TextBuffer buffer = {0};
    for (long i = 0; i < items->count; i++) {
        if (items->items[i].type != SYGER_STRING) syger_error("E3011", "convert the items with to_string() first", "join requires an array of strings");
        if (i) text_append(&buffer, sep->data, (size_t)sep->length);
        text_append(&buffer, items->items[i].as.string->data, (size_t)items->items[i].as.string->length);
    }
    SygerValue result = buffer.data ? syger_string_n(buffer.data, (long)buffer.length) : empty_string();
    free(buffer.data);
    syger_release(array);
    syger_release(separator);
    return result;
}

static SygerValue fold_ints(SygerValue array, const char *name, int mode)
{
    SygerArray *items = require_array(array, name);
    if (items->count == 0) syger_error("E3012", "check length(array) > 0 first", "%s requires a non-empty array", name);
    long result = mode == 0 ? 0 : require_int(items->items[0], name);
    for (long i = 0; i < items->count; i++) {
        long v = require_int(items->items[i], name);
        if (mode == 0) result += v;
        else if (mode == 1 && v < result) result = v;
        else if (mode == 2 && v > result) result = v;
    }
    syger_release(array);
    return syger_int(result);
}

SygerValue syger_b_sum(SygerValue array) { return fold_ints(array, "sum", 0); }
SygerValue syger_b_min(SygerValue array) { return fold_ints(array, "min", 1); }
SygerValue syger_b_max(SygerValue array) { return fold_ints(array, "max", 2); }

SygerValue syger_b_first(SygerValue array)
{
    SygerArray *items = require_array(array, "first");
    if (items->count == 0) syger_error("E3012", "check length(array) > 0 first", "first requires a non-empty array");
    return syger_index(array, syger_int(0));
}

SygerValue syger_b_last(SygerValue array)
{
    SygerArray *items = require_array(array, "last");
    if (items->count == 0) syger_error("E3012", "check length(array) > 0 first", "last requires a non-empty array");
    return syger_index(array, syger_int(items->count - 1));
}

SygerValue syger_b_append(SygerValue *target, SygerValue item)
{
    SygerArray *array = mutable_array(target, "append");
    array_reserve(array, array->count + 1);
    array->items[array->count++] = item;
    return syger_int(0);
}

SygerValue syger_b_pop(SygerValue *target)
{
    SygerArray *array = mutable_array(target, "pop");
    if (array->count == 0) syger_error("E3012", "check length(array) > 0 first", "pop cannot remove from an empty array");
    return array->items[--array->count];
}

SygerValue syger_b_insert(SygerValue *target, SygerValue index_value, SygerValue item)
{
    SygerArray *array = mutable_array(target, "insert");
    long index = require_int(index_value, "insert");
    if (index < 0 || index > array->count) syger_error("E3007", NULL, "insert index %ld is out of bounds for an array of length %ld", index, array->count);
    array_reserve(array, array->count + 1);
    memmove(&array->items[index + 1], &array->items[index], sizeof(SygerValue) * (size_t)(array->count - index));
    array->items[index] = item;
    array->count++;
    return syger_int(0);
}

SygerValue syger_b_remove(SygerValue *target, SygerValue index_value)
{
    SygerArray *array = mutable_array(target, "remove");
    long index = require_int(index_value, "remove");
    if (index < 0 || index >= array->count) syger_error("E3007", NULL, "remove index %ld is out of bounds for an array of length %ld", index, array->count);
    SygerValue result = array->items[index];
    memmove(&array->items[index], &array->items[index + 1], sizeof(SygerValue) * (size_t)(array->count - index - 1));
    array->count--;
    return result;
}

SygerValue syger_b_clear(SygerValue *target)
{
    SygerArray *array = mutable_array(target, "clear");
    for (long i = 0; i < array->count; i++) syger_release(array->items[i]);
    array->count = 0;
    return syger_int(0);
}

SygerValue syger_b_reverse(SygerValue *target)
{
    SygerArray *array = mutable_array(target, "reverse");
    for (long i = 0, j = array->count - 1; i < j; i++, j--) {
        SygerValue tmp = array->items[i];
        array->items[i] = array->items[j];
        array->items[j] = tmp;
    }
    return syger_int(0);
}

static int compare_ints(const void *a, const void *b)
{
    long x = ((const SygerValue *)a)->as.integer, y = ((const SygerValue *)b)->as.integer;
    return (x > y) - (x < y);
}

SygerValue syger_b_sort(SygerValue *target)
{
    SygerArray *array = mutable_array(target, "sort");
    for (long i = 0; i < array->count; i++) {
        if (array->items[i].type != SYGER_INT) syger_error("E3011", NULL, "sort requires an array of integers");
    }
    qsort(array->items, (size_t)array->count, sizeof(SygerValue), compare_ints);
    return syger_int(0);
}
