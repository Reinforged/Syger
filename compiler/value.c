#include "value.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
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

typedef struct {
    Value key;
    Value value;
} MapEntry;

struct Map {
    long refcount;
    int count;
    int capacity;
    MapEntry *entries; /* in insertion order */
    int *slots;        /* open-addressing index into entries, -1 = empty */
    int slot_capacity; /* power of two */
};

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
    if (value->type == VALUE_MAP)
    {
        value->map->refcount++;
        return *value;
    }

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

static void map_destroy(Map *map);

void value_free(Value *value)
{
    if (value->type == VALUE_MAP)
    {
        if (value->map != NULL && --value->map->refcount == 0)
        {
            map_destroy(value->map);
        }

        value->map = NULL;
        return;
    }

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

        case VALUE_FLOAT:
        {
            char buffer[64];
            value_format_float(value->number, buffer, sizeof(buffer));
            fputs(buffer, out);
            break;
        }

        case VALUE_MAP:
            fputc('{', out);

            for (int i = 0; i < value->map->count; i++)
            {
                if (i > 0)
                {
                    fputs(", ", out);
                }

                value_print_inline(out, &value->map->entries[i].key);
                fputs(": ", out);
                value_print_inline(out, &value->map->entries[i].value);
            }

            fputc('}', out);
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

/* ---- floats ---------------------------------------------------------------- */

void value_format_float(double number, char *buffer, size_t size)
{
    if (isnan(number))
    {
        snprintf(buffer, size, "nan");
        return;
    }

    if (isinf(number))
    {
        snprintf(buffer, size, number < 0 ? "-inf" : "inf");
        return;
    }

    double magnitude = fabs(number);

    if (magnitude == 0 || (magnitude >= 1e-4 && magnitude < 1e16))
    {
        /* Plain decimal with the fewest digits that read back exactly. */
        for (int decimals = 1; decimals <= 17; decimals++)
        {
            snprintf(buffer, size, "%.*f", decimals, number);

            if (strtod(buffer, NULL) == number)
            {
                return;
            }
        }

        return;
    }

    for (int precision = 1; precision <= 17; precision++)
    {
        snprintf(buffer, size, "%.*g", precision, number);

        if (strtod(buffer, NULL) == number)
        {
            break;
        }
    }

    if (strpbrk(buffer, ".en") == NULL && strlen(buffer) + 2 < size)
    {
        strcat(buffer, ".0");
    }
}

/* ---- type names and equality ------------------------------------------------ */

const char *value_type_name(ValueType type)
{
    switch (type)
    {
        case VALUE_STRING: return "string";
        case VALUE_INT: return "integer";
        case VALUE_FLOAT: return "float";
        case VALUE_BOOL: return "boolean";
        case VALUE_NONE: return "none";
        case VALUE_ARRAY: return "array";
        case VALUE_MAP: return "map";
    }

    return "value";
}

int value_equal(const Value *left, const Value *right)
{
    if (left->type == VALUE_INT && right->type == VALUE_FLOAT)
    {
        return (double)left->integer == right->number;
    }

    if (left->type == VALUE_FLOAT && right->type == VALUE_INT)
    {
        return left->number == (double)right->integer;
    }

    if (left->type != right->type)
    {
        return 0;
    }

    switch (left->type)
    {
        case VALUE_STRING:
            return left->string == right->string ||
                   (value_string_length(left) == value_string_length(right) &&
                    memcmp(left->string, right->string, value_string_length(left)) == 0);

        case VALUE_INT: return left->integer == right->integer;
        case VALUE_FLOAT: return left->number == right->number;
        case VALUE_BOOL: return left->boolean == right->boolean;
        case VALUE_NONE: return 1;

        case VALUE_ARRAY:
            if (left->array.count != right->array.count)
            {
                return 0;
            }

            for (int i = 0; i < left->array.count; i++)
            {
                if (!value_equal(&left->array.elements[i], &right->array.elements[i]))
                {
                    return 0;
                }
            }

            return 1;

        case VALUE_MAP:
        {
            if (left->map == right->map)
            {
                return 1;
            }

            if (left->map->count != right->map->count)
            {
                return 0;
            }

            /* Same keys with equal values, in any order. */
            for (int i = 0; i < left->map->count; i++)
            {
                Value *other = value_map_find(right, &left->map->entries[i].key);

                if (other == NULL || !value_equal(&left->map->entries[i].value, other))
                {
                    return 0;
                }
            }

            return 1;
        }
    }

    return 0;
}

/* ---- maps ------------------------------------------------------------------- */

int value_is_map_key(const Value *key)
{
    return key->type == VALUE_STRING || key->type == VALUE_INT;
}

static size_t hash_key(const Value *key)
{
    if (key->type == VALUE_INT)
    {
        unsigned long x = (unsigned long)key->integer;
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdUL;
        x ^= x >> 33;
        return (size_t)x;
    }

    size_t hash = 1469598103934665603u;
    size_t length = value_string_length(key);

    for (size_t i = 0; i < length; i++)
    {
        hash = (hash ^ (unsigned char)key->string[i]) * 1099511628211u;
    }

    return hash;
}

static int keys_equal(const Value *a, const Value *b)
{
    if (a->type != b->type)
    {
        return 0;
    }

    if (a->type == VALUE_INT)
    {
        return a->integer == b->integer;
    }

    return value_equal(a, b);
}

static void map_reindex(Map *map, int slot_capacity)
{
    free(map->slots);
    map->slots = malloc(sizeof(int) * (size_t)slot_capacity);

    if (map->slots == NULL)
    {
        out_of_memory();
    }

    for (int i = 0; i < slot_capacity; i++)
    {
        map->slots[i] = -1;
    }

    map->slot_capacity = slot_capacity;

    for (int i = 0; i < map->count; i++)
    {
        size_t slot = hash_key(&map->entries[i].key) & (size_t)(slot_capacity - 1);

        while (map->slots[slot] != -1)
        {
            slot = (slot + 1) & (size_t)(slot_capacity - 1);
        }

        map->slots[slot] = i;
    }
}

static Map *map_alloc(int capacity)
{
    Map *map = malloc(sizeof(Map));

    if (map == NULL)
    {
        out_of_memory();
    }

    if (capacity < 4)
    {
        capacity = 4;
    }

    map->refcount = 1;
    map->count = 0;
    map->capacity = capacity;
    map->entries = malloc(sizeof(MapEntry) * (size_t)capacity);
    map->slots = NULL;

    if (map->entries == NULL)
    {
        out_of_memory();
    }

    int slots = 8;

    while (slots < capacity * 2)
    {
        slots *= 2;
    }

    map_reindex(map, slots);
    return map;
}

static void map_destroy(Map *map)
{
    for (int i = 0; i < map->count; i++)
    {
        value_free(&map->entries[i].key);
        value_free(&map->entries[i].value);
    }

    free(map->entries);
    free(map->slots);
    free(map);
}

Value value_map_new(int capacity)
{
    Value value;
    value.type = VALUE_MAP;
    value.map = map_alloc(capacity);
    return value;
}

int value_map_count(const Value *map)
{
    return map->map->count;
}

static int map_index_of(const Map *map, const Value *key)
{
    size_t slot = hash_key(key) & (size_t)(map->slot_capacity - 1);

    while (map->slots[slot] != -1)
    {
        int index = map->slots[slot];

        if (keys_equal(&map->entries[index].key, key))
        {
            return index;
        }

        slot = (slot + 1) & (size_t)(map->slot_capacity - 1);
    }

    return -1;
}

Value *value_map_find(const Value *map, const Value *key)
{
    if (!value_is_map_key(key))
    {
        return NULL;
    }

    int index = map_index_of(map->map, key);
    return index < 0 ? NULL : &map->map->entries[index].value;
}

void value_map_make_unique(Value *value)
{
    Map *shared = value->map;

    if (shared->refcount == 1)
    {
        return;
    }

    Map *copy = map_alloc(shared->count);

    for (int i = 0; i < shared->count; i++)
    {
        copy->entries[i].key = value_copy(&shared->entries[i].key);
        copy->entries[i].value = value_copy(&shared->entries[i].value);
    }

    copy->count = shared->count;
    map_reindex(copy, copy->slot_capacity);
    shared->refcount--;
    value->map = copy;
}

Value *value_map_slot(Value *value, Value key)
{
    value_map_make_unique(value);
    Map *map = value->map;
    int index = map_index_of(map, &key);

    if (index >= 0)
    {
        value_free(&key);
        return &map->entries[index].value;
    }

    if (map->count == map->capacity)
    {
        map->capacity *= 2;
        map->entries = realloc(map->entries, sizeof(MapEntry) * (size_t)map->capacity);

        if (map->entries == NULL)
        {
            out_of_memory();
        }
    }

    map->entries[map->count].key = key;
    map->entries[map->count].value = value_none();
    map->count++;

    if (map->count * 2 > map->slot_capacity)
    {
        map_reindex(map, map->slot_capacity * 2);
    }
    else
    {
        size_t slot = hash_key(&key) & (size_t)(map->slot_capacity - 1);

        while (map->slots[slot] != -1)
        {
            slot = (slot + 1) & (size_t)(map->slot_capacity - 1);
        }

        map->slots[slot] = map->count - 1;
    }

    return &map->entries[map->count - 1].value;
}

void value_map_set(Value *map, Value key, Value value)
{
    Value *slot = value_map_slot(map, key);
    value_free(slot);
    *slot = value;
}

int value_map_remove(Value *value, const Value *key, Value *removed)
{
    if (!value_is_map_key(key))
    {
        return 0;
    }

    value_map_make_unique(value);
    Map *map = value->map;
    int index = map_index_of(map, key);

    if (index < 0)
    {
        return 0;
    }

    value_free(&map->entries[index].key);
    *removed = map->entries[index].value;
    memmove(&map->entries[index], &map->entries[index + 1], sizeof(MapEntry) * (size_t)(map->count - index - 1));
    map->count--;
    map_reindex(map, map->slot_capacity);
    return 1;
}

const Value *value_map_key_at(const Value *map, int index)
{
    return &map->map->entries[index].key;
}

const Value *value_map_value_at(const Value *map, int index)
{
    return &map->map->entries[index].value;
}
