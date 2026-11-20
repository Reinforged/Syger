#include "builtins.h"
#include "data.h"
#include "diagnostics.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    int min_arguments;
    int max_arguments;
    const char *usage;
} BuiltinInfo;

static const BuiltinInfo builtins[] = {
    {"contains", 2, 2, "contains(array_map_or_text, value)"},
    {"index_of", 2, 2, "index_of(array_or_text, value)"},
    {"sum", 1, 1, "sum(array)"},
    {"min", 1, 1, "min(array)"},
    {"max", 1, 1, "max(array)"},
    {"type_of", 1, 1, "type_of(value)"},
    {"keys", 1, 1, "keys(map)"},
    {"values", 1, 1, "values(map)"},
    {"get", 3, 3, "get(map_or_array, key, default)"},
    {"range", 1, 3, "range(end), range(start, end) or range(start, end, step)"},
    {"to_int", 1, 1, "to_int(value)"},
    {"to_float", 1, 1, "to_float(value)"},
    {"round", 1, 2, "round(number) or round(number, digits)"},
    {"floor", 1, 1, "floor(number)"},
    {"ceil", 1, 1, "ceil(number)"},
    {"abs", 1, 1, "abs(number)"},
    {"sqrt", 1, 1, "sqrt(number)"},
    {"split", 1, 2, "split(text) or split(text, separator)"},
    {"trim", 1, 1, "trim(text)"},
    {"upper", 1, 1, "upper(text)"},
    {"lower", 1, 1, "lower(text)"},
    {"replace", 3, 3, "replace(text, old, new)"},
    {"starts_with", 2, 2, "starts_with(text, prefix)"},
    {"ends_with", 2, 2, "ends_with(text, suffix)"},
    {"load", 1, 1, "load(path)"},
    {"load_error", 1, 1, "load_error(path)"},
};

#define BUILTIN_COUNT (sizeof(builtins) / sizeof(builtins[0]))

static const BuiltinInfo *find_builtin(const char *name)
{
    for (size_t i = 0; i < BUILTIN_COUNT; i++)
    {
        if (strcmp(builtins[i].name, name) == 0)
        {
            return &builtins[i];
        }
    }

    return NULL;
}

int builtin_exists(const char *name)
{
    return find_builtin(name) != NULL;
}

const char *builtin_usage_text(const char *name)
{
    const BuiltinInfo *info = find_builtin(name);
    return info ? info->usage : NULL;
}

void builtin_check_arity(const char *name, int count)
{
    const BuiltinInfo *info = find_builtin(name);

    if (info == NULL || (count >= info->min_arguments && count <= info->max_arguments))
    {
        return;
    }

    char help[160];
    snprintf(help, sizeof(help), "usage: %s", info->usage);

    if (info->min_arguments == info->max_arguments)
    {
        diagnostics_runtime_errorf("E3003", help, "%s expects %d argument(s), got %d", name, info->min_arguments, count);
    }

    diagnostics_runtime_errorf("E3003", help, "%s expects %d to %d arguments, got %d", name, info->min_arguments, info->max_arguments, count);
}

/* ---- argument helpers ----------------------------------------------------------- */

_Noreturn static void type_error(const char *name, const char *expected, const Value *got)
{
    diagnostics_runtime_errorf("E3011", NULL, "%s requires %s (got %s)", name, expected, value_type_name(got->type));
}

static const char *need_string(const char *name, const Value *value)
{
    if (value->type != VALUE_STRING)
    {
        type_error(name, "a string", value);
    }

    return value->string;
}

static double need_number(const char *name, const Value *value)
{
    if (value->type == VALUE_INT) return (double)value->integer;
    if (value->type == VALUE_FLOAT) return value->number;
    type_error(name, "a number", value);
}

static long need_int(const char *name, const Value *value)
{
    if (value->type != VALUE_INT)
    {
        type_error(name, "an integer", value);
    }

    return value->integer;
}

static Value int_from_double(const char *name, double number)
{
    if (!isfinite(number) || number >= 9.2e18 || number <= -9.2e18)
    {
        diagnostics_runtime_errorf("E3030", NULL, "%s: %g can't be turned into an integer", name, number);
    }

    return value_int((long)number);
}

/* ---- data files -------------------------------------------------------------------- */

static Value data_to_value(const DataNode *node)
{
    switch (node->type)
    {
        case DATA_NONE: return value_none();
        case DATA_BOOL: return value_bool(node->boolean);
        case DATA_INT: return value_int(node->integer);
        case DATA_FLOAT: return value_float(node->number);
        case DATA_STRING: return value_string_n(node->string, node->length);

        case DATA_ARRAY:
        {
            Value array = value_array(node->count);

            for (int i = 0; i < node->count; i++)
            {
                array.array.elements[i] = data_to_value(node->items[i]);
            }

            return array;
        }

        case DATA_MAP:
        {
            Value map = value_map_new(node->count);

            for (int i = 0; i < node->count; i++)
            {
                value_map_set(&map, data_to_value(node->keys[i]), data_to_value(node->items[i]));
            }

            return map;
        }
    }

    return value_none();
}

static char script_directory[4096] = "";

void builtins_set_script(const char *path)
{
    const char *slash = strrchr(path, '/');

    if (slash == NULL)
    {
        script_directory[0] = '\0';
        return;
    }

    size_t length = (size_t)(slash - path) + 1;

    if (length >= sizeof(script_directory))
    {
        length = sizeof(script_directory) - 1;
    }

    memcpy(script_directory, path, length);
    script_directory[length] = '\0';
}

typedef enum { LOAD_OK, LOAD_MISSING, LOAD_UNREADABLE, LOAD_INVALID } LoadStatus;

/* Reads and parses a data file. On failure, message describes the problem. */
static LoadStatus load_file(const char *path, Value *result, char *message, size_t size)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL)
    {
        snprintf(message, size, "could not open '%s'", path);
        return LOAD_MISSING;
    }

    char *text = NULL;
    size_t length = 0, capacity = 0;
    char chunk[65536];
    size_t got;

    while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0)
    {
        if (length + got + 1 > capacity)
        {
            capacity = (length + got + 1) * 2;
            text = realloc(text, capacity);

            if (text == NULL)
            {
                diagnostics_fatal("E0003", "Out of memory.", NULL);
            }
        }

        memcpy(text + length, chunk, got);
        length += got;
    }

    int failed = ferror(file);
    fclose(file);

    if (failed)
    {
        free(text);
        snprintf(message, size, "could not read '%s'", path);
        return LOAD_UNREADABLE;
    }

    DataError error;
    DataNode *node = data_parse(text ? text : "", length, data_format_for_path(path), &error);
    free(text);

    if (node == NULL)
    {
        if (error.line > 0)
        {
            snprintf(message, size, "%s:%d: %s", path, error.line, error.message);
        }
        else
        {
            snprintf(message, size, "%s: %s", path, error.message);
        }

        return LOAD_INVALID;
    }

    *result = data_to_value(node);
    data_free(node);
    return LOAD_OK;
}

static Value load_or_fail(const char *path, const char *where_help)
{
    Value result = value_none();
    char message[512];

    switch (load_file(path, &result, message, sizeof(message)))
    {
        case LOAD_OK:
            return result;
        case LOAD_MISSING:
            diagnostics_runtime_errorf("E3020", where_help, "%s", message);
        case LOAD_UNREADABLE:
            diagnostics_runtime_errorf("E3021", NULL, "%s", message);
        case LOAD_INVALID:
            diagnostics_runtime_errorf("E3026", "fix the file; load_error(path) checks a file without stopping the program", "invalid data in %s", message);
    }

    return result;
}

Value builtin_import(const char *path)
{
    char full[8192];

    if (path[0] == '/')
    {
        snprintf(full, sizeof(full), "%s", path);
    }
    else
    {
        snprintf(full, sizeof(full), "%s%s", script_directory, path);
    }

    return load_or_fail(full, "import paths are relative to the .sg file that imports them");
}

/* ---- sorting ----------------------------------------------------------------------- */

static int compare_values(const void *a, const void *b)
{
    const Value *x = a, *y = b;

    if (x->type == VALUE_STRING)
    {
        return strcmp(x->string, y->string);
    }

    double p = x->type == VALUE_INT ? (double)x->integer : x->number;
    double q = y->type == VALUE_INT ? (double)y->integer : y->number;

    if (x->type == VALUE_INT && y->type == VALUE_INT)
    {
        return (x->integer > y->integer) - (x->integer < y->integer);
    }

    return (p > q) - (p < q);
}

void builtin_sort(Value *array)
{
    int strings = 0, numbers = 0;

    for (int i = 0; i < array->array.count; i++)
    {
        ValueType type = array->array.elements[i].type;

        if (type == VALUE_STRING) strings++;
        else if (type == VALUE_INT || type == VALUE_FLOAT) numbers++;
        else diagnostics_runtime_errorf("E3011", "sort works on arrays of numbers or arrays of strings", "sort can't order %s values", value_type_name(type));
    }

    if (strings && numbers)
    {
        diagnostics_runtime_errorf("E3011", "sort works on arrays of numbers or arrays of strings", "sort can't order a mix of strings and numbers");
    }

    qsort(array->array.elements, (size_t)array->array.count, sizeof(Value), compare_values);
}

/* ---- strings ----------------------------------------------------------------------- */

static Value split(const char *text, size_t length, const char *separator)
{
    Value parts = value_array(0);

    if (separator == NULL)
    {
        size_t i = 0;

        while (i < length)
        {
            while (i < length && isspace((unsigned char)text[i])) i++;
            size_t start = i;
            while (i < length && !isspace((unsigned char)text[i])) i++;

            if (i > start)
            {
                value_array_reserve(&parts, parts.array.count + 1);
                parts.array.elements[parts.array.count++] = value_string_n(text + start, i - start);
            }
        }

        return parts;
    }

    size_t separator_length = strlen(separator);

    if (separator_length == 0)
    {
        diagnostics_runtime_errorf("E3011", "use split(text) to split on spaces, or loop over the text for characters", "split separator can't be empty");
    }

    const char *start = text;
    const char *end = text + length;

    while (1)
    {
        const char *found = strstr(start, separator);

        if (found == NULL || found > end)
        {
            found = end;
        }

        value_array_reserve(&parts, parts.array.count + 1);
        parts.array.elements[parts.array.count++] = value_string_n(start, (size_t)(found - start));

        if (found == end)
        {
            break;
        }

        start = found + separator_length;
    }

    return parts;
}

static Value replace_all(const char *text, const char *old, const char *new_text)
{
    size_t old_length = strlen(old);

    if (old_length == 0)
    {
        diagnostics_runtime_errorf("E3011", NULL, "replace: the text to replace can't be empty");
    }

    Value result = value_string("");
    const char *position = text;

    while (1)
    {
        const char *found = strstr(position, old);

        if (found == NULL)
        {
            Value rest = value_string(position);
            value_string_append(&result, &rest);
            value_free(&rest);
            return result;
        }

        Value piece = value_string_n(position, (size_t)(found - position));
        Value replacement = value_string(new_text);
        value_string_append(&result, &piece);
        value_string_append(&result, &replacement);
        value_free(&piece);
        value_free(&replacement);
        position = found + old_length;
    }
}

static Value parse_number_string(const char *name, const char *text, int want_int)
{
    while (isspace((unsigned char)*text)) text++;
    char *end = NULL;
    errno = 0;

    if (want_int)
    {
        long value = strtol(text, &end, 10);

        if (end != text && errno == 0)
        {
            while (isspace((unsigned char)*end)) end++;
            if (*end == '\0') return value_int(value);
        }

        diagnostics_runtime_errorf("E3029", strchr(text, '.') ? "use to_float() for numbers with a decimal point" : NULL,
                                   "%s can't convert \"%s\" to an integer", name, text);
    }

    double value = strtod(text, &end);

    if (end != text)
    {
        while (isspace((unsigned char)*end)) end++;
        if (*end == '\0') return value_float(value);
    }

    diagnostics_runtime_errorf("E3029", NULL, "%s can't convert \"%s\" to a number", name, text);
}

/* ---- dispatch ---------------------------------------------------------------------- */

static Value reduce(const char *name, Value *array)
{
    if (array->type != VALUE_ARRAY)
    {
        type_error(name, "an array", array);
    }

    int count = array->array.count;
    Value *items = array->array.elements;
    int is_sum = strcmp(name, "sum") == 0;

    if (count == 0)
    {
        if (is_sum)
        {
            return value_int(0);
        }

        diagnostics_runtime_errorf("E3012", "check length(array) > 0 first", "%s requires a non-empty array", name);
    }

    if (!is_sum && items[0].type == VALUE_STRING)
    {
        int best = 0;

        for (int i = 0; i < count; i++)
        {
            if (items[i].type != VALUE_STRING)
            {
                type_error(name, "an array of numbers or of strings", &items[i]);
            }

            int order = strcmp(items[i].string, items[best].string);

            if ((name[1] == 'i' && order < 0) || (name[1] == 'a' && order > 0))
            {
                best = i;
            }
        }

        return value_copy(&items[best]);
    }

    int any_float = 0;
    long int_total = 0;
    double float_total = 0;
    int best = 0;

    for (int i = 0; i < count; i++)
    {
        double value = need_number(name, &items[i]);

        if (items[i].type == VALUE_FLOAT) any_float = 1;
        int_total += items[i].type == VALUE_INT ? items[i].integer : 0;
        float_total += value;

        double current = need_number(name, &items[best]);

        if ((name[1] == 'i' && value < current) || (name[1] == 'a' && value > current))
        {
            best = i;
        }
    }

    if (is_sum)
    {
        return any_float ? value_float(float_total) : value_int(int_total);
    }

    return value_copy(&items[best]);
}

static Value call(const char *name, Value *a, int count)
{
    if (strcmp(name, "contains") == 0 || strcmp(name, "index_of") == 0)
    {
        int is_contains = name[0] == 'c';

        if (a[0].type == VALUE_STRING)
        {
            const char *found = strstr(a[0].string, need_string(name, &a[1]));
            return is_contains ? value_bool(found != NULL) : value_int(found ? (long)(found - a[0].string) : -1);
        }

        if (a[0].type == VALUE_MAP && is_contains)
        {
            return value_bool(value_map_find(&a[0], &a[1]) != NULL);
        }

        if (a[0].type != VALUE_ARRAY)
        {
            type_error(name, is_contains ? "an array, map or string" : "an array or string", &a[0]);
        }

        for (int i = 0; i < a[0].array.count; i++)
        {
            if (value_equal(&a[0].array.elements[i], &a[1]))
            {
                return is_contains ? value_bool(1) : value_int(i);
            }
        }

        return is_contains ? value_bool(0) : value_int(-1);
    }

    if (strcmp(name, "sum") == 0 || strcmp(name, "min") == 0 || strcmp(name, "max") == 0)
    {
        return reduce(name, &a[0]);
    }

    if (strcmp(name, "type_of") == 0)
    {
        return value_string(value_type_name(a[0].type));
    }

    if (strcmp(name, "keys") == 0 || strcmp(name, "values") == 0)
    {
        if (a[0].type != VALUE_MAP)
        {
            type_error(name, "a map", &a[0]);
        }

        int count_entries = value_map_count(&a[0]);
        Value result = value_array(count_entries);

        for (int i = 0; i < count_entries; i++)
        {
            result.array.elements[i] = value_copy(name[0] == 'k' ? value_map_key_at(&a[0], i) : value_map_value_at(&a[0], i));
        }

        return result;
    }

    if (strcmp(name, "get") == 0)
    {
        if (a[0].type == VALUE_MAP)
        {
            Value *found = value_map_find(&a[0], &a[1]);
            return value_copy(found ? found : &a[2]);
        }

        if (a[0].type == VALUE_ARRAY)
        {
            long index = need_int(name, &a[1]);
            int inside = index >= 0 && index < a[0].array.count;
            return value_copy(inside ? &a[0].array.elements[index] : &a[2]);
        }

        type_error(name, "a map or array", &a[0]);
    }

    if (strcmp(name, "range") == 0)
    {
        long start = 0, end, step = 1;

        if (count == 1)
        {
            end = need_int(name, &a[0]);
        }
        else
        {
            start = need_int(name, &a[0]);
            end = need_int(name, &a[1]);
            if (count == 3) step = need_int(name, &a[2]);
        }

        if (step == 0)
        {
            diagnostics_runtime_errorf("E3011", "use a positive step to count up or a negative one to count down", "range step cannot be 0");
        }

        Value result = value_array(0);

        for (long i = start; step > 0 ? i < end : i > end; i += step)
        {
            value_array_reserve(&result, result.array.count + 1);
            result.array.elements[result.array.count++] = value_int(i);
        }

        return result;
    }

    if (strcmp(name, "to_int") == 0)
    {
        switch (a[0].type)
        {
            case VALUE_INT: return a[0];
            case VALUE_FLOAT: return int_from_double(name, trunc(a[0].number));
            case VALUE_BOOL: return value_int(a[0].boolean);
            case VALUE_STRING: return parse_number_string(name, a[0].string, 1);
            default: type_error(name, "a number, string or boolean", &a[0]);
        }
    }

    if (strcmp(name, "to_float") == 0)
    {
        switch (a[0].type)
        {
            case VALUE_INT: return value_float((double)a[0].integer);
            case VALUE_FLOAT: return a[0];
            case VALUE_STRING: return parse_number_string(name, a[0].string, 0);
            default: type_error(name, "a number or string", &a[0]);
        }
    }

    if (strcmp(name, "round") == 0)
    {
        double number = need_number(name, &a[0]);

        if (count == 1)
        {
            return a[0].type == VALUE_INT ? a[0] : int_from_double(name, round(number));
        }

        double scale = pow(10, (double)need_int(name, &a[1]));
        return value_float(round(number * scale) / scale);
    }

    if (strcmp(name, "floor") == 0 || strcmp(name, "ceil") == 0)
    {
        double number = need_number(name, &a[0]);

        if (a[0].type == VALUE_INT)
        {
            return a[0];
        }

        return int_from_double(name, name[0] == 'f' ? floor(number) : ceil(number));
    }

    if (strcmp(name, "abs") == 0)
    {
        need_number(name, &a[0]);
        return a[0].type == VALUE_INT ? value_int(labs(a[0].integer)) : value_float(fabs(a[0].number));
    }

    if (strcmp(name, "sqrt") == 0)
    {
        double number = need_number(name, &a[0]);

        if (number < 0)
        {
            diagnostics_runtime_errorf("E3030", NULL, "sqrt of a negative number (%g)", number);
        }

        return value_float(sqrt(number));
    }

    if (strcmp(name, "split") == 0)
    {
        const char *text = need_string(name, &a[0]);
        return split(text, value_string_length(&a[0]), count == 2 ? need_string(name, &a[1]) : NULL);
    }

    if (strcmp(name, "trim") == 0)
    {
        const char *text = need_string(name, &a[0]);
        size_t length = value_string_length(&a[0]);
        size_t start = 0;

        while (start < length && isspace((unsigned char)text[start])) start++;
        while (length > start && isspace((unsigned char)text[length - 1])) length--;

        return value_string_n(text + start, length - start);
    }

    if (strcmp(name, "upper") == 0 || strcmp(name, "lower") == 0)
    {
        need_string(name, &a[0]);
        Value result = value_string_n(a[0].string, value_string_length(&a[0]));

        for (char *p = result.string; *p; p++)
        {
            *p = (char)(name[0] == 'u' ? toupper((unsigned char)*p) : tolower((unsigned char)*p));
        }

        return result;
    }

    if (strcmp(name, "replace") == 0)
    {
        return replace_all(need_string(name, &a[0]), need_string(name, &a[1]), need_string(name, &a[2]));
    }

    if (strcmp(name, "starts_with") == 0 || strcmp(name, "ends_with") == 0)
    {
        const char *text = need_string(name, &a[0]);
        const char *part = need_string(name, &a[1]);
        size_t text_length = value_string_length(&a[0]);
        size_t part_length = value_string_length(&a[1]);

        if (part_length > text_length)
        {
            return value_bool(0);
        }

        const char *compare_at = name[0] == 's' ? text : text + text_length - part_length;
        return value_bool(memcmp(compare_at, part, part_length) == 0);
    }

    if (strcmp(name, "load") == 0)
    {
        return load_or_fail(need_string(name, &a[0]), "load paths are relative to the directory you run the program from");
    }

    if (strcmp(name, "load_error") == 0)
    {
        Value ignored = value_none();
        char message[512];

        if (load_file(need_string(name, &a[0]), &ignored, message, sizeof(message)) == LOAD_OK)
        {
            value_free(&ignored);
            return value_string("");
        }

        return value_string(message);
    }

    diagnostics_runtime_errorf("E9001", "this is a bug in Syger; please report it", "unknown builtin '%s'", name);
}

Value builtin_call(const char *name, Value *args, int count)
{
    builtin_check_arity(name, count);

    /* call() always returns its own value (copied or new), never an argument
       itself, so the arguments can all be released. */
    Value result = call(name, args, count);

    for (int i = 0; i < count; i++)
    {
        value_free(&args[i]);
    }

    return result;
}
