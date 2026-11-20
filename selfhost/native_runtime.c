#include "native_runtime.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The data-file parsers are shared with the interpreter. */
#include "../compiler/data.c"

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
        case SYGER_FLOAT: return "float";
        case SYGER_MAP: return "map";
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

static void map_destroy(SygerMap *map);

void syger_free_object(SygerValue value)
{
    if (value.type == SYGER_MAP) {
        map_destroy(value.as.map);
    } else if (value.type == SYGER_STRING) {
        free(value.as.string);
    } else if (value.type == SYGER_ARRAY) {
        SygerArray *array = value.as.array;
        for (long i = 0; i < array->count; i++) syger_release(array->items[i]);
        free(array->items);
        free(array);
    }
}

/* ---- equality and rendering ---------------------------------------------- */

static SygerValue *map_find(const SygerValue *map, const SygerValue *key);

int syger_equal(const SygerValue *left, const SygerValue *right)
{
    if (left->type == SYGER_INT && right->type == SYGER_FLOAT) return (double)left->as.integer == right->as.number;
    if (left->type == SYGER_FLOAT && right->type == SYGER_INT) return left->as.number == (double)right->as.integer;
    if (left->type != right->type) return 0;
    switch (left->type) {
        case SYGER_INT: return left->as.integer == right->as.integer;
        case SYGER_FLOAT: return left->as.number == right->as.number;
        case SYGER_MAP: {
            SygerMap *a = left->as.map, *b = right->as.map;
            if (a == b) return 1;
            if (a->count != b->count) return 0;
            for (long i = 0; i < a->count; i++) {
                SygerValue *other = map_find(right, &a->entries[i].key);
                if (other == NULL || !syger_equal(&a->entries[i].value, other)) return 0;
            }
            return 1;
        }
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

/* Same rules as the interpreter: plain decimal for ordinary magnitudes,
   the fewest digits that read back exactly, and always a '.' or exponent. */
static void format_float(double number, char *buffer, size_t size)
{
    if (isnan(number)) { snprintf(buffer, size, "nan"); return; }
    if (isinf(number)) { snprintf(buffer, size, number < 0 ? "-inf" : "inf"); return; }
    double magnitude = fabs(number);
    if (magnitude == 0 || (magnitude >= 1e-4 && magnitude < 1e16)) {
        for (int decimals = 1; decimals <= 17; decimals++) {
            snprintf(buffer, size, "%.*f", decimals, number);
            if (strtod(buffer, NULL) == number) return;
        }
        return;
    }
    for (int precision = 1; precision <= 17; precision++) {
        snprintf(buffer, size, "%.*g", precision, number);
        if (strtod(buffer, NULL) == number) break;
    }
    if (strpbrk(buffer, ".en") == NULL && strlen(buffer) + 2 < size) strcat(buffer, ".0");
}

static void render(TextBuffer *buffer, const SygerValue *value)
{
    char number[64];
    switch (value->type) {
        case SYGER_FLOAT:
            format_float(value->as.number, number, sizeof(number));
            text_append(buffer, number, strlen(number));
            break;
        case SYGER_MAP:
            text_append(buffer, "{", 1);
            for (long i = 0; i < value->as.map->count; i++) {
                if (i) text_append(buffer, ", ", 2);
                render(buffer, &value->as.map->entries[i].key);
                text_append(buffer, ": ", 2);
                render(buffer, &value->as.map->entries[i].value);
            }
            text_append(buffer, "}", 1);
            break;
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
    else if (value.type == SYGER_MAP) length = value.as.map->count;
    else syger_type_error("E3011", value, "length requires a string, array or map");
    syger_release(value);
    return syger_int(length);
}

SygerValue syger_b_token_at(SygerValue array, SygerValue index)
{
    return syger_index(array, index);
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

static int map_remove(SygerValue *map, const SygerValue *key, SygerValue *removed);
_Noreturn static void key_error(const SygerValue *map, const SygerValue *key);

SygerValue syger_b_remove(SygerValue *target, SygerValue index_value)
{
    if (target->type == SYGER_MAP) {
        SygerValue removed;
        if (!map_remove(target, &index_value, &removed)) key_error(target, &index_value);
        syger_release(index_value);
        return removed;
    }
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

static int compare_values(const void *a, const void *b)
{
    const SygerValue *x = a, *y = b;
    if (x->type == SYGER_STRING) return strcmp(x->as.string->data, y->as.string->data);
    if (x->type == SYGER_INT && y->type == SYGER_INT) return (x->as.integer > y->as.integer) - (x->as.integer < y->as.integer);
    double p = x->type == SYGER_INT ? (double)x->as.integer : x->as.number;
    double q = y->type == SYGER_INT ? (double)y->as.integer : y->as.number;
    return (p > q) - (p < q);
}

SygerValue syger_b_sort(SygerValue *target)
{
    SygerArray *array = mutable_array(target, "sort");
    int strings = 0, numbers = 0;
    for (long i = 0; i < array->count; i++) {
        SygerType type = array->items[i].type;
        if (type == SYGER_STRING) strings++;
        else if (type == SYGER_INT || type == SYGER_FLOAT) numbers++;
        else syger_error("E3011", "sort works on arrays of numbers or arrays of strings", "sort can't order %s values", type_name(type));
    }
    if (strings && numbers) syger_error("E3011", "sort works on arrays of numbers or arrays of strings", "sort can't order a mix of strings and numbers");
    qsort(array->items, (size_t)array->count, sizeof(SygerValue), compare_values);
    return syger_int(0);
}

/* ---- maps ------------------------------------------------------------------ */

static int is_map_key(const SygerValue *key)
{
    return key->type == SYGER_STRING || key->type == SYGER_INT;
}

static size_t hash_key(const SygerValue *key)
{
    if (key->type == SYGER_INT) {
        unsigned long x = (unsigned long)key->as.integer;
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdUL;
        x ^= x >> 33;
        return (size_t)x;
    }
    size_t hash = 1469598103934665603u;
    for (long i = 0; i < key->as.string->length; i++) hash = (hash ^ (unsigned char)key->as.string->data[i]) * 1099511628211u;
    return hash;
}

static int keys_equal(const SygerValue *a, const SygerValue *b)
{
    if (a->type != b->type) return 0;
    if (a->type == SYGER_INT) return a->as.integer == b->as.integer;
    return syger_equal(a, b);
}

static void map_reindex(SygerMap *map, long slot_capacity)
{
    free(map->slots);
    map->slots = checked_malloc(sizeof(long) * (size_t)slot_capacity);
    for (long i = 0; i < slot_capacity; i++) map->slots[i] = -1;
    map->slot_capacity = slot_capacity;
    for (long i = 0; i < map->count; i++) {
        size_t slot = hash_key(&map->entries[i].key) & (size_t)(slot_capacity - 1);
        while (map->slots[slot] != -1) slot = (slot + 1) & (size_t)(slot_capacity - 1);
        map->slots[slot] = i;
    }
}

static SygerMap *map_alloc(long capacity)
{
    if (capacity < 4) capacity = 4;
    SygerMap *map = checked_malloc(sizeof(SygerMap));
    map->refcount = 1;
    map->count = 0;
    map->capacity = capacity;
    map->entries = checked_malloc(sizeof(SygerMapEntry) * (size_t)capacity);
    map->slots = NULL;
    long slots = 8;
    while (slots < capacity * 2) slots *= 2;
    map_reindex(map, slots);
    return map;
}

static void map_destroy(SygerMap *map)
{
    for (long i = 0; i < map->count; i++) {
        syger_release(map->entries[i].key);
        syger_release(map->entries[i].value);
    }
    free(map->entries);
    free(map->slots);
    free(map);
}

static SygerValue map_value(SygerMap *map)
{
    SygerValue value;
    value.type = SYGER_MAP;
    value.as.map = map;
    return value;
}

static long map_index_of(const SygerMap *map, const SygerValue *key)
{
    size_t slot = hash_key(key) & (size_t)(map->slot_capacity - 1);
    while (map->slots[slot] != -1) {
        long index = map->slots[slot];
        if (keys_equal(&map->entries[index].key, key)) return index;
        slot = (slot + 1) & (size_t)(map->slot_capacity - 1);
    }
    return -1;
}

static SygerValue *map_find(const SygerValue *map, const SygerValue *key)
{
    if (!is_map_key(key)) return NULL;
    long index = map_index_of(map->as.map, key);
    return index < 0 ? NULL : &map->as.map->entries[index].value;
}

static void map_make_unique(SygerValue *value)
{
    SygerMap *shared = value->as.map;
    if (shared->refcount == 1) return;
    SygerMap *copy = map_alloc(shared->count);
    for (long i = 0; i < shared->count; i++) {
        copy->entries[i].key = syger_retain(shared->entries[i].key);
        copy->entries[i].value = syger_retain(shared->entries[i].value);
    }
    copy->count = shared->count;
    map_reindex(copy, copy->slot_capacity);
    SygerValue old = *value;
    *value = map_value(copy);
    syger_release(old);
}

/* Pointer to the value under key (consumed), adding none if it is missing. */
static SygerValue *map_slot(SygerValue *value, SygerValue key)
{
    map_make_unique(value);
    SygerMap *map = value->as.map;
    long index = map_index_of(map, &key);
    if (index >= 0) {
        syger_release(key);
        return &map->entries[index].value;
    }
    if (map->count == map->capacity) {
        map->capacity *= 2;
        map->entries = checked_realloc(map->entries, sizeof(SygerMapEntry) * (size_t)map->capacity);
    }
    map->entries[map->count].key = key;
    map->entries[map->count].value = syger_none();
    map->count++;
    if (map->count * 2 > map->slot_capacity) {
        map_reindex(map, map->slot_capacity * 2);
    } else {
        size_t slot = hash_key(&key) & (size_t)(map->slot_capacity - 1);
        while (map->slots[slot] != -1) slot = (slot + 1) & (size_t)(map->slot_capacity - 1);
        map->slots[slot] = map->count - 1;
    }
    return &map->entries[map->count - 1].value;
}

static int map_remove(SygerValue *value, const SygerValue *key, SygerValue *removed)
{
    if (!is_map_key(key)) return 0;
    map_make_unique(value);
    SygerMap *map = value->as.map;
    long index = map_index_of(map, key);
    if (index < 0) return 0;
    syger_release(map->entries[index].key);
    *removed = map->entries[index].value;
    memmove(&map->entries[index], &map->entries[index + 1], sizeof(SygerMapEntry) * (size_t)(map->count - index - 1));
    map->count--;
    map_reindex(map, map->slot_capacity);
    return 1;
}

static void check_map_key(const SygerValue *key)
{
    if (!is_map_key(key)) syger_error("E3024", NULL, "map keys must be strings or integers (got %s)", type_name(key->type));
}

SygerValue syger_map_new(long count, SygerValue *pairs)
{
    SygerValue map = map_value(map_alloc(count));
    for (long i = 0; i < count; i++) {
        check_map_key(&pairs[2 * i]);
        syger_set(map_slot(&map, pairs[2 * i]), pairs[2 * i + 1]);
    }
    return map;
}

/* ---- errors that need maps ------------------------------------------------- */

static int edit_distance(const char *a, const char *b)
{
    int n = (int)strlen(a), m = (int)strlen(b);
    if (n > 63 || m > 63) return 1000;
    int row[64];
    for (int j = 0; j <= m; j++) row[j] = j;
    for (int i = 1; i <= n; i++) {
        int diagonal = row[0];
        row[0] = i;
        for (int j = 1; j <= m; j++) {
            int above = row[j];
            int best = diagonal + (a[i - 1] != b[j - 1]);
            if (row[j] + 1 < best) best = row[j] + 1;
            if (row[j - 1] + 1 < best) best = row[j - 1] + 1;
            row[j] = best;
            diagonal = above;
        }
    }
    return row[m];
}

_Noreturn static void key_error(const SygerValue *map, const SygerValue *key)
{
    if (key->type == SYGER_STRING) {
        const char *name = key->as.string->data;
        int length = (int)strlen(name);
        int limit = length <= 3 ? 1 : (length <= 6 ? 2 : 3);
        const char *best = NULL;
        int best_distance = limit + 1;
        for (long i = 0; i < map->as.map->count; i++) {
            const SygerValue *candidate = &map->as.map->entries[i].key;
            if (candidate->type != SYGER_STRING || strcmp(candidate->as.string->data, name) == 0) continue;
            int distance = edit_distance(name, candidate->as.string->data);
            if (distance < best_distance) { best = candidate->as.string->data; best_distance = distance; }
        }
        char help[320];
        if (best) snprintf(help, sizeof(help), "did you mean '%s'?", best);
        else snprintf(help, sizeof(help), "use contains(map, key) to check first, or get(map, key, default)");
        syger_error("E3023", help, "key '%s' not found in map", name);
    }
    syger_error("E3023", "use contains(map, key) to check first, or get(map, key, default)", "key %ld not found in map", key->as.integer);
}

static _Noreturn void index_error(long index, long count)
{
    char help[128];
    if (count == 0) snprintf(help, sizeof(help), "the array is empty");
    else snprintf(help, sizeof(help), "valid indexes are 0 to %ld", count - 1);
    syger_error("E3007", help, "index %ld is out of bounds for an array of length %ld", index, count);
}

/* ---- operators ------------------------------------------------------------- */

static const char *const operator_symbols[] = { "+", "-", "*", "/", "%", "<", "<=", ">", ">=" };

static int is_number(const SygerValue *value)
{
    return value->type == SYGER_INT || value->type == SYGER_FLOAT;
}

static double number_of(const SygerValue *value)
{
    return value->type == SYGER_INT ? (double)value->as.integer : value->as.number;
}

SygerValue syger_binary_slow(int op, SygerValue left, SygerValue right)
{
    if ((op == SYGER_OP_DIV || op == SYGER_OP_MOD) && is_number(&left) && is_number(&right) && number_of(&right) == 0) {
        if (op == SYGER_OP_DIV) syger_error("E3013", "check that the divisor is not zero before dividing", "division by zero");
        syger_error("E3014", "check that the divisor is not zero before using %", "modulo by zero");
    }

    if (is_number(&left) && is_number(&right)) {
        double a = number_of(&left), b = number_of(&right);
        switch (op) {
            case SYGER_OP_ADD: return syger_float(a + b);
            case SYGER_OP_SUB: return syger_float(a - b);
            case SYGER_OP_MUL: return syger_float(a * b);
            case SYGER_OP_DIV: return syger_float(a / b);
            case SYGER_OP_MOD: return syger_float(fmod(a, b));
            case SYGER_OP_LT: return syger_bool(a < b);
            case SYGER_OP_LE: return syger_bool(a <= b);
            case SYGER_OP_GT: return syger_bool(a > b);
            case SYGER_OP_GE: return syger_bool(a >= b);
        }
    }

    if (left.type == SYGER_STRING && right.type == SYGER_STRING) {
        if (op == SYGER_OP_ADD) {
            SygerString *a = left.as.string, *b = right.as.string;
            SygerString *joined = string_alloc(a->length + b->length, a->length + b->length);
            memcpy(joined->data, a->data, (size_t)a->length);
            memcpy(joined->data + a->length, b->data, (size_t)b->length);
            syger_release(left);
            syger_release(right);
            return string_value(joined);
        }
        if (op >= SYGER_OP_LT) {
            int order = strcmp(left.as.string->data, right.as.string->data);
            int result = op == SYGER_OP_LT ? order < 0 : op == SYGER_OP_LE ? order <= 0 : op == SYGER_OP_GT ? order > 0 : order >= 0;
            syger_release(left);
            syger_release(right);
            return syger_bool(result);
        }
    }

    if (op == SYGER_OP_ADD && left.type == SYGER_ARRAY && right.type == SYGER_ARRAY) {
        SygerArray *a = left.as.array, *b = right.as.array;
        SygerArray *joined = array_alloc(a->count + b->count);
        for (long i = 0; i < a->count; i++) joined->items[i] = syger_retain(a->items[i]);
        for (long i = 0; i < b->count; i++) joined->items[a->count + i] = syger_retain(b->items[i]);
        joined->count = a->count + b->count;
        syger_release(left);
        syger_release(right);
        return array_value(joined);
    }

    const char *help = "arithmetic needs two numbers; < > <= >= compare two numbers or two strings";
    if (op == SYGER_OP_ADD && (left.type == SYGER_STRING || right.type == SYGER_STRING)) {
        help = left.type == right.type ? "+ joins two strings" : "convert the other value first, e.g. \"total: \" + to_string(count)";
    } else if (op == SYGER_OP_ADD) {
        help = "+ adds numbers, or joins two strings or two arrays";
    }
    syger_error("E3018", help, "cannot use '%s' on %s and %s", operator_symbols[op], type_name(left.type), type_name(right.type));
}

/* ---- indexing slow paths ----------------------------------------------------- */

SygerValue *syger_place_slow(SygerValue *container, SygerValue index, int mode)
{
    if (container->type == SYGER_MAP) {
        check_map_key(&index);
        if (mode == SYGER_PLACE_ASSIGN) return map_slot(container, index);
        if (map_find(container, &index) == NULL) key_error(container, &index);
        if (mode == SYGER_PLACE_CHANGE) map_make_unique(container);
        SygerValue *found = map_find(container, &index);
        syger_release(index);
        return found;
    }
    if (container->type != SYGER_ARRAY) syger_error("E3008", NULL, "indexing requires an array or map (got %s)", type_name(container->type));
    if (index.type != SYGER_INT) syger_error("E3009", NULL, "array index must be an integer (got %s)", type_name(index.type));
    index_error(index.as.integer, container->as.array->count);
}

SygerValue syger_index_slow(SygerValue container, SygerValue index)
{
    if (container.type == SYGER_STRING && index.type == SYGER_INT) {
        long length = container.as.string->length;
        if (index.as.integer < 0 || index.as.integer >= length) {
            syger_error("E3007", length ? NULL : "the string is empty", "index %ld is out of bounds for a string of length %ld", index.as.integer, length);
        }
        SygerValue result = single_char((unsigned char)container.as.string->data[index.as.integer]);
        syger_release(container);
        return result;
    }
    SygerValue result = syger_retain(*syger_place_slow(&container, index, SYGER_PLACE_READ));
    syger_release(container);
    return result;
}

/* ---- for loops --------------------------------------------------------------- */

long syger_iter_count(const SygerValue *sequence)
{
    switch (sequence->type) {
        case SYGER_ARRAY: return sequence->as.array->count;
        case SYGER_MAP: return sequence->as.map->count;
        case SYGER_STRING: return sequence->as.string->length;
        default:
            syger_error("E3025", "for works on arrays, maps (their keys), strings (their characters) and range(...)", "cannot loop over %s", type_name(sequence->type));
    }
}

SygerValue syger_iter_item(const SygerValue *sequence, long index)
{
    if (sequence->type == SYGER_ARRAY) return syger_retain(sequence->as.array->items[index]);
    if (sequence->type == SYGER_MAP) return syger_retain(sequence->as.map->entries[index].key);
    return single_char((unsigned char)sequence->as.string->data[index]);
}

long syger_range_argument(SygerValue value)
{
    if (value.type != SYGER_INT) {
        syger_error("E3011", "usage: range(end), range(start, end) or range(start, end, step)", "range requires integers (got %s)", type_name(value.type));
    }
    return value.as.integer;
}

void syger_range_check_step(long step)
{
    if (step == 0) syger_error("E3011", "use a positive step to count up or a negative one to count down", "range step cannot be 0");
}

/* ---- value builtins ------------------------------------------------------------ */

_Noreturn static void type_error(const char *name, const char *expected, const SygerValue *got)
{
    syger_error("E3011", NULL, "%s requires %s (got %s)", name, expected, type_name(got->type));
}

static double need_number(const char *name, const SygerValue *value)
{
    if (!is_number(value)) type_error(name, "a number", value);
    return number_of(value);
}

static SygerValue int_from_double(const char *name, double number)
{
    if (!isfinite(number) || number >= 9.2e18 || number <= -9.2e18) {
        syger_error("E3030", NULL, "%s: %g can't be turned into an integer", name, number);
    }
    return syger_int((long)number);
}

static SygerValue contains_or_index(const char *name, SygerValue container, SygerValue item)
{
    int is_contains = name[0] == 'c';
    SygerValue result;
    if (container.type == SYGER_STRING) {
        if (item.type != SYGER_STRING) type_error(name, "a string", &item);
        const char *found = strstr(container.as.string->data, item.as.string->data);
        result = is_contains ? syger_bool(found != NULL) : syger_int(found ? (long)(found - container.as.string->data) : -1);
    } else if (container.type == SYGER_MAP && is_contains) {
        result = syger_bool(map_find(&container, &item) != NULL);
    } else {
        if (container.type != SYGER_ARRAY) type_error(name, is_contains ? "an array, map or string" : "an array or string", &container);
        long found = -1;
        for (long i = 0; i < container.as.array->count; i++) {
            if (syger_equal(&container.as.array->items[i], &item)) { found = i; break; }
        }
        result = is_contains ? syger_bool(found >= 0) : syger_int(found);
    }
    syger_release(container);
    syger_release(item);
    return result;
}

SygerValue syger_b_contains(SygerValue container, SygerValue item) { return contains_or_index("contains", container, item); }
SygerValue syger_b_index_of(SygerValue container, SygerValue item) { return contains_or_index("index_of", container, item); }

static SygerValue reduce(const char *name, SygerValue array)
{
    if (array.type != SYGER_ARRAY) type_error(name, "an array", &array);
    long count = array.as.array->count;
    SygerValue *items = array.as.array->items;
    int is_sum = name[1] == 'u';
    SygerValue result;
    if (count == 0) {
        if (!is_sum) syger_error("E3012", "check length(array) > 0 first", "%s requires a non-empty array", name);
        result = syger_int(0);
    } else if (!is_sum && items[0].type == SYGER_STRING) {
        long best = 0;
        for (long i = 0; i < count; i++) {
            if (items[i].type != SYGER_STRING) type_error(name, "an array of numbers or of strings", &items[i]);
            int order = strcmp(items[i].as.string->data, items[best].as.string->data);
            if ((name[1] == 'i' && order < 0) || (name[1] == 'a' && order > 0)) best = i;
        }
        result = syger_retain(items[best]);
    } else {
        int any_float = 0;
        long int_total = 0;
        double float_total = 0;
        long best = 0;
        for (long i = 0; i < count; i++) {
            double value = need_number(name, &items[i]);
            if (items[i].type == SYGER_FLOAT) any_float = 1;
            if (items[i].type == SYGER_INT) int_total += items[i].as.integer;
            float_total += value;
            double current = need_number(name, &items[best]);
            if ((name[1] == 'i' && value < current) || (name[1] == 'a' && value > current)) best = i;
        }
        result = is_sum ? (any_float ? syger_float(float_total) : syger_int(int_total)) : syger_retain(items[best]);
    }
    syger_release(array);
    return result;
}

SygerValue syger_b_sum(SygerValue array) { return reduce("sum", array); }
SygerValue syger_b_min(SygerValue array) { return reduce("min", array); }
SygerValue syger_b_max(SygerValue array) { return reduce("max", array); }

static SygerValue keys_or_values(const char *name, SygerValue map)
{
    if (map.type != SYGER_MAP) type_error(name, "a map", &map);
    SygerArray *result = array_alloc(map.as.map->count);
    for (long i = 0; i < map.as.map->count; i++) {
        result->items[i] = syger_retain(name[0] == 'k' ? map.as.map->entries[i].key : map.as.map->entries[i].value);
    }
    result->count = map.as.map->count;
    syger_release(map);
    return array_value(result);
}

SygerValue syger_b_keys(SygerValue map) { return keys_or_values("keys", map); }
SygerValue syger_b_values(SygerValue map) { return keys_or_values("values", map); }

SygerValue syger_b_get(SygerValue container, SygerValue key, SygerValue fallback)
{
    SygerValue result;
    if (container.type == SYGER_MAP) {
        SygerValue *found = map_find(&container, &key);
        result = syger_retain(found ? *found : fallback);
    } else if (container.type == SYGER_ARRAY) {
        if (key.type != SYGER_INT) type_error("get", "an integer", &key);
        long i = key.as.integer;
        result = syger_retain(i >= 0 && i < container.as.array->count ? container.as.array->items[i] : fallback);
    } else {
        type_error("get", "a map or array", &container);
    }
    syger_release(container);
    syger_release(key);
    syger_release(fallback);
    return result;
}

static SygerValue make_range(long start, long end, long step)
{
    syger_range_check_step(step);
    SygerArray *array = array_alloc(0);
    for (long i = start; step > 0 ? i < end : i > end; i += step) {
        array_reserve(array, array->count + 1);
        array->items[array->count++] = syger_int(i);
    }
    return array_value(array);
}

static long range_int(SygerValue value)
{
    if (value.type != SYGER_INT) type_error("range", "an integer", &value);
    return value.as.integer;
}

SygerValue syger_b_range1(SygerValue end) { return make_range(0, range_int(end), 1); }
SygerValue syger_b_range2(SygerValue start, SygerValue end) { return make_range(range_int(start), range_int(end), 1); }
SygerValue syger_b_range3(SygerValue start, SygerValue end, SygerValue step) { return make_range(range_int(start), range_int(end), range_int(step)); }

static SygerValue convert_number_text(const char *name, const char *text, int want_int)
{
    while (isspace((unsigned char)*text)) text++;
    char *end = NULL;
    errno = 0;
    if (want_int) {
        long value = strtol(text, &end, 10);
        if (end != text && errno == 0) {
            while (isspace((unsigned char)*end)) end++;
            if (*end == '\0') return syger_int(value);
        }
        syger_error("E3029", strchr(text, '.') ? "use to_float() for numbers with a decimal point" : NULL, "%s can't convert \"%s\" to an integer", name, text);
    }
    double value = strtod(text, &end);
    if (end != text) {
        while (isspace((unsigned char)*end)) end++;
        if (*end == '\0') return syger_float(value);
    }
    syger_error("E3029", NULL, "%s can't convert \"%s\" to a number", name, text);
}

SygerValue syger_b_to_int(SygerValue value)
{
    SygerValue result;
    switch (value.type) {
        case SYGER_INT: return value;
        case SYGER_FLOAT: return int_from_double("to_int", trunc(value.as.number));
        case SYGER_BOOL: return syger_int(value.as.boolean);
        case SYGER_STRING: result = convert_number_text("to_int", value.as.string->data, 1); break;
        default: type_error("to_int", "a number, string or boolean", &value);
    }
    syger_release(value);
    return result;
}

SygerValue syger_b_to_float(SygerValue value)
{
    SygerValue result;
    switch (value.type) {
        case SYGER_INT: return syger_float((double)value.as.integer);
        case SYGER_FLOAT: return value;
        case SYGER_STRING: result = convert_number_text("to_float", value.as.string->data, 0); break;
        default: type_error("to_float", "a number or string", &value);
    }
    syger_release(value);
    return result;
}

SygerValue syger_b_round1(SygerValue number)
{
    double value = need_number("round", &number);
    return number.type == SYGER_INT ? number : int_from_double("round", round(value));
}

SygerValue syger_b_round2(SygerValue number, SygerValue digits)
{
    double value = need_number("round", &number);
    if (digits.type != SYGER_INT) type_error("round", "an integer", &digits);
    double scale = pow(10, (double)digits.as.integer);
    return syger_float(round(value * scale) / scale);
}

SygerValue syger_b_floor(SygerValue number)
{
    double value = need_number("floor", &number);
    return number.type == SYGER_INT ? number : int_from_double("floor", floor(value));
}

SygerValue syger_b_ceil(SygerValue number)
{
    double value = need_number("ceil", &number);
    return number.type == SYGER_INT ? number : int_from_double("ceil", ceil(value));
}

SygerValue syger_b_abs(SygerValue number)
{
    need_number("abs", &number);
    return number.type == SYGER_INT ? syger_int(labs(number.as.integer)) : syger_float(fabs(number.as.number));
}

SygerValue syger_b_sqrt(SygerValue number)
{
    double value = need_number("sqrt", &number);
    if (value < 0) syger_error("E3030", NULL, "sqrt of a negative number (%g)", value);
    return syger_float(sqrt(value));
}

static SygerArray *push_string(SygerArray *array, const char *text, long length)
{
    array_reserve(array, array->count + 1);
    array->items[array->count++] = length == 1 ? single_char((unsigned char)text[0]) : syger_string_n(text, length);
    return array;
}

SygerValue syger_b_split1(SygerValue text)
{
    SygerString *s = require_string(text, "split");
    SygerArray *parts = array_alloc(0);
    long i = 0;
    while (i < s->length) {
        while (i < s->length && isspace((unsigned char)s->data[i])) i++;
        long start = i;
        while (i < s->length && !isspace((unsigned char)s->data[i])) i++;
        if (i > start) push_string(parts, s->data + start, i - start);
    }
    syger_release(text);
    return array_value(parts);
}

SygerValue syger_b_split2(SygerValue text, SygerValue separator)
{
    SygerString *s = require_string(text, "split");
    SygerString *sep = require_string(separator, "split");
    if (sep->length == 0) syger_error("E3011", "use split(text) to split on spaces, or loop over the text for characters", "split separator can't be empty");
    SygerArray *parts = array_alloc(0);
    const char *start = s->data, *end = s->data + s->length;
    while (1) {
        const char *found = strstr(start, sep->data);
        if (found == NULL || found > end) found = end;
        push_string(parts, start, (long)(found - start));
        if (found == end) break;
        start = found + sep->length;
    }
    syger_release(text);
    syger_release(separator);
    return array_value(parts);
}

SygerValue syger_b_trim(SygerValue text)
{
    SygerString *s = require_string(text, "trim");
    long start = 0, end = s->length;
    while (start < end && isspace((unsigned char)s->data[start])) start++;
    while (end > start && isspace((unsigned char)s->data[end - 1])) end--;
    SygerValue result = syger_string_n(s->data + start, end - start);
    syger_release(text);
    return result;
}

static SygerValue change_case(const char *name, SygerValue text, int upper)
{
    SygerString *s = require_string(text, name);
    SygerValue result = syger_string_n(s->data, s->length);
    for (long i = 0; i < s->length; i++) {
        result.as.string->data[i] = (char)(upper ? toupper((unsigned char)s->data[i]) : tolower((unsigned char)s->data[i]));
    }
    syger_release(text);
    return result;
}

SygerValue syger_b_upper(SygerValue text) { return change_case("upper", text, 1); }
SygerValue syger_b_lower(SygerValue text) { return change_case("lower", text, 0); }

SygerValue syger_b_replace(SygerValue text, SygerValue old_text, SygerValue new_text)
{
    SygerString *s = require_string(text, "replace");
    SygerString *o = require_string(old_text, "replace");
    SygerString *n = require_string(new_text, "replace");
    if (o->length == 0) syger_error("E3011", NULL, "replace: the text to replace can't be empty");
    TextBuffer buffer = {0};
    const char *position = s->data;
    while (1) {
        const char *found = strstr(position, o->data);
        if (found == NULL) {
            text_append(&buffer, position, strlen(position));
            break;
        }
        text_append(&buffer, position, (size_t)(found - position));
        text_append(&buffer, n->data, (size_t)n->length);
        position = found + o->length;
    }
    SygerValue result = buffer.data ? syger_string_n(buffer.data, (long)buffer.length) : empty_string();
    free(buffer.data);
    syger_release(text);
    syger_release(old_text);
    syger_release(new_text);
    return result;
}

static SygerValue affix(const char *name, SygerValue text, SygerValue part, int at_start)
{
    SygerString *s = require_string(text, name);
    SygerString *p = require_string(part, name);
    int result = p->length <= s->length &&
                 memcmp(at_start ? s->data : s->data + s->length - p->length, p->data, (size_t)p->length) == 0;
    syger_release(text);
    syger_release(part);
    return syger_bool(result);
}

SygerValue syger_b_starts_with(SygerValue text, SygerValue prefix) { return affix("starts_with", text, prefix, 1); }
SygerValue syger_b_ends_with(SygerValue text, SygerValue suffix) { return affix("ends_with", text, suffix, 0); }

/* ---- data files ---------------------------------------------------------------- */

static SygerValue data_to_value(const DataNode *node)
{
    switch (node->type) {
        case DATA_NONE: return syger_none();
        case DATA_BOOL: return syger_bool(node->boolean);
        case DATA_INT: return syger_int(node->integer);
        case DATA_FLOAT: return syger_float(node->number);
        case DATA_STRING: return syger_string_n(node->string, (long)node->length);
        case DATA_ARRAY: {
            SygerArray *array = array_alloc(node->count);
            for (int i = 0; i < node->count; i++) array->items[i] = data_to_value(node->items[i]);
            array->count = node->count;
            return array_value(array);
        }
        case DATA_MAP: {
            SygerValue map = map_value(map_alloc(node->count));
            for (int i = 0; i < node->count; i++) syger_set(map_slot(&map, data_to_value(node->keys[i])), data_to_value(node->items[i]));
            return map;
        }
    }
    return syger_none();
}

typedef enum { LOAD_OK, LOAD_MISSING, LOAD_UNREADABLE, LOAD_INVALID } LoadStatus;

static LoadStatus parse_data(const char *path, const char *text, size_t length, SygerValue *result, char *message, size_t size)
{
    DataError error;
    DataNode *node = data_parse(text, length, data_format_for_path(path), &error);
    if (node == NULL) {
        if (error.line > 0) snprintf(message, size, "%s:%d: %s", path, error.line, error.message);
        else snprintf(message, size, "%s: %s", path, error.message);
        return LOAD_INVALID;
    }
    *result = data_to_value(node);
    data_free(node);
    return LOAD_OK;
}

static LoadStatus load_file(const char *path, SygerValue *result, char *message, size_t size)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        snprintf(message, size, "could not open '%s'", path);
        return LOAD_MISSING;
    }
    TextBuffer buffer = {0};
    char chunk[65536];
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0) text_append(&buffer, chunk, got);
    int failed = ferror(file);
    fclose(file);
    if (failed) {
        free(buffer.data);
        snprintf(message, size, "could not read '%s'", path);
        return LOAD_UNREADABLE;
    }
    LoadStatus status = parse_data(path, buffer.data ? buffer.data : "", buffer.length, result, message, size);
    free(buffer.data);
    return status;
}

static SygerValue load_or_fail(LoadStatus status, SygerValue result, const char *message, const char *where_help)
{
    switch (status) {
        case LOAD_OK: return result;
        case LOAD_MISSING: syger_error("E3020", where_help, "%s", message);
        case LOAD_UNREADABLE: syger_error("E3021", NULL, "%s", message);
        case LOAD_INVALID: syger_error("E3026", "fix the file; load_error(path) checks a file without stopping the program", "invalid data in %s", message);
    }
    return result;
}

SygerValue syger_b_load(SygerValue path)
{
    SygerString *p = require_string(path, "load");
    SygerValue result = syger_none();
    char message[512];
    LoadStatus status = load_file(p->data, &result, message, sizeof(message));
    result = load_or_fail(status, result, message, "load paths are relative to the directory you run the program from");
    syger_release(path);
    return result;
}

SygerValue syger_b_load_error(SygerValue path)
{
    SygerString *p = require_string(path, "load_error");
    SygerValue result = syger_none();
    char message[512];
    SygerValue text = load_file(p->data, &result, message, sizeof(message)) == LOAD_OK ? empty_string() : syger_string(message);
    syger_release(result);
    syger_release(path);
    return text;
}

SygerValue syger_import_embedded(const char *path, const char *text, long length)
{
    SygerValue result = syger_none();
    char message[512];
    LoadStatus status = parse_data(path, text, (size_t)length, &result, message, sizeof(message));
    return load_or_fail(status, result, message, NULL);
}
