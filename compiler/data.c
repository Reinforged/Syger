#include "data.h"

#include <ctype.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- nodes ------------------------------------------------------------------ */

static void *data_alloc(size_t size)
{
    void *memory = calloc(1, size ? size : 1);

    if (memory == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    return memory;
}

static DataNode *node_new(DataType type)
{
    DataNode *node = data_alloc(sizeof(DataNode));
    node->type = type;
    return node;
}

static DataNode *node_string(const char *text, size_t length)
{
    DataNode *node = node_new(DATA_STRING);
    node->string = data_alloc(length + 1);
    memcpy(node->string, text, length);
    node->string[length] = '\0';
    node->length = length;
    return node;
}

static DataNode *node_int(long value)
{
    DataNode *node = node_new(DATA_INT);
    node->integer = value;
    return node;
}

static DataNode *node_float(double value)
{
    DataNode *node = node_new(DATA_FLOAT);
    node->number = value;
    return node;
}

static DataNode *node_bool(int value)
{
    DataNode *node = node_new(DATA_BOOL);
    node->boolean = value;
    return node;
}

static void node_reserve(DataNode *node)
{
    if (node->count < node->capacity)
    {
        return;
    }

    node->capacity = node->capacity ? node->capacity * 2 : 4;
    node->items = realloc(node->items, sizeof(DataNode *) * (size_t)node->capacity);

    if (node->type == DATA_MAP)
    {
        node->keys = realloc(node->keys, sizeof(DataNode *) * (size_t)node->capacity);
    }

    if (node->items == NULL || (node->type == DATA_MAP && node->keys == NULL))
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }
}

static void array_push(DataNode *array, DataNode *item)
{
    node_reserve(array);
    array->items[array->count++] = item;
}

static int keys_match(const DataNode *a, const DataNode *b)
{
    if (a->type != b->type)
    {
        return 0;
    }

    if (a->type == DATA_INT)
    {
        return a->integer == b->integer;
    }

    return a->length == b->length && memcmp(a->string, b->string, a->length) == 0;
}

/* Later duplicates replace earlier ones, like most JSON parsers. */
static void map_put(DataNode *map, DataNode *key, DataNode *value)
{
    for (int i = 0; i < map->count; i++)
    {
        if (keys_match(map->keys[i], key))
        {
            data_free(map->items[i]);
            data_free(key);
            map->items[i] = value;
            return;
        }
    }

    node_reserve(map);
    map->keys[map->count] = key;
    map->items[map->count] = value;
    map->count++;
}

void data_free(DataNode *node)
{
    if (node == NULL)
    {
        return;
    }

    for (int i = 0; i < node->count; i++)
    {
        data_free(node->items[i]);

        if (node->type == DATA_MAP)
        {
            data_free(node->keys[i]);
        }
    }

    free(node->items);
    free(node->keys);
    free(node->string);
    free(node);
}

/* ---- shared reader state ------------------------------------------------------ */

typedef struct {
    const char *text;
    size_t length;
    size_t position;
    int line;
    DataError *error;
    jmp_buf fail;
} Reader;

static void reader_fail(Reader *reader, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(reader->error->message, sizeof(reader->error->message), format, arguments);
    va_end(arguments);
    reader->error->line = reader->line;
    longjmp(reader->fail, 1);
}

static int peek(Reader *reader)
{
    return reader->position < reader->length ? (unsigned char)reader->text[reader->position] : -1;
}

static int peek_at(Reader *reader, size_t offset)
{
    size_t at = reader->position + offset;
    return at < reader->length ? (unsigned char)reader->text[at] : -1;
}

static int next(Reader *reader)
{
    int c = peek(reader);

    if (c != -1)
    {
        reader->position++;

        if (c == '\n')
        {
            reader->line++;
        }
    }

    return c;
}

/* Growable byte buffer for decoding strings. */
typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} Buffer;

static void buffer_add(Buffer *buffer, char c)
{
    if (buffer->length + 1 >= buffer->capacity)
    {
        buffer->capacity = buffer->capacity ? buffer->capacity * 2 : 32;
        buffer->data = realloc(buffer->data, buffer->capacity);

        if (buffer->data == NULL)
        {
            fprintf(stderr, "Syger: out of memory.\n");
            exit(1);
        }
    }

    buffer->data[buffer->length++] = c;
}

static void buffer_add_utf8(Buffer *buffer, unsigned long code)
{
    if (code < 0x80)
    {
        buffer_add(buffer, (char)code);
    }
    else if (code < 0x800)
    {
        buffer_add(buffer, (char)(0xC0 | (code >> 6)));
        buffer_add(buffer, (char)(0x80 | (code & 0x3F)));
    }
    else if (code < 0x10000)
    {
        buffer_add(buffer, (char)(0xE0 | (code >> 12)));
        buffer_add(buffer, (char)(0x80 | ((code >> 6) & 0x3F)));
        buffer_add(buffer, (char)(0x80 | (code & 0x3F)));
    }
    else
    {
        buffer_add(buffer, (char)(0xF0 | (code >> 18)));
        buffer_add(buffer, (char)(0x80 | ((code >> 12) & 0x3F)));
        buffer_add(buffer, (char)(0x80 | ((code >> 6) & 0x3F)));
        buffer_add(buffer, (char)(0x80 | (code & 0x3F)));
    }
}

static DataNode *buffer_to_node(Buffer *buffer)
{
    DataNode *node = node_string(buffer->data ? buffer->data : "", buffer->length);
    free(buffer->data);
    return node;
}

static int hex_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static unsigned long read_hex(Reader *reader, int digits)
{
    unsigned long value = 0;

    for (int i = 0; i < digits; i++)
    {
        int digit = hex_value(peek(reader));

        if (digit < 0)
        {
            reader_fail(reader, "expected %d hex digits in escape", digits);
        }

        next(reader);
        value = value * 16 + (unsigned long)digit;
    }

    return value;
}

/* Parses an integer or float written in decimal. Returns 0 if the text
   between start and end is not a whole number literal. */
static int parse_number_text(const char *start, size_t length, DataNode **out)
{
    char text[128];
    size_t used = 0;
    int is_float = 0;

    if (length == 0 || length >= sizeof(text))
    {
        return 0;
    }

    for (size_t i = 0; i < length; i++)
    {
        char c = start[i];

        if (c == '_')
        {
            continue;
        }

        if (c == '.' || c == 'e' || c == 'E')
        {
            is_float = 1;
        }

        text[used++] = c;
    }

    text[used] = '\0';
    char *end = NULL;
    errno = 0;

    if (!is_float)
    {
        long value = strtol(text, &end, 10);

        if (*end == '\0' && errno == 0 && end != text)
        {
            *out = node_int(value);
            return 1;
        }
    }

    double value = strtod(text, &end);

    if (*end == '\0' && end != text)
    {
        *out = node_float(value);
        return 1;
    }

    return 0;
}

/* ---- JSON --------------------------------------------------------------------- */

static void json_space(Reader *reader)
{
    while (peek(reader) == ' ' || peek(reader) == '\t' || peek(reader) == '\n' || peek(reader) == '\r')
    {
        next(reader);
    }
}

static int match_word(Reader *reader, const char *word)
{
    size_t length = strlen(word);

    if (reader->position + length <= reader->length &&
        memcmp(reader->text + reader->position, word, length) == 0 &&
        !isalnum(peek_at(reader, length)) && peek_at(reader, length) != '_')
    {
        reader->position += length;
        return 1;
    }

    return 0;
}

static DataNode *json_string(Reader *reader)
{
    Buffer buffer = {0};
    next(reader); /* opening quote */

    while (1)
    {
        int c = next(reader);

        if (c == -1 || c == '\n')
        {
            reader_fail(reader, "unterminated string");
        }

        if (c == '"')
        {
            break;
        }

        if (c != '\\')
        {
            buffer_add(&buffer, (char)c);
            continue;
        }

        int e = next(reader);

        switch (e)
        {
            case '"': buffer_add(&buffer, '"'); break;
            case '\\': buffer_add(&buffer, '\\'); break;
            case '/': buffer_add(&buffer, '/'); break;
            case 'b': buffer_add(&buffer, '\b'); break;
            case 'f': buffer_add(&buffer, '\f'); break;
            case 'n': buffer_add(&buffer, '\n'); break;
            case 'r': buffer_add(&buffer, '\r'); break;
            case 't': buffer_add(&buffer, '\t'); break;
            case 'u':
            {
                unsigned long code = read_hex(reader, 4);

                if (code >= 0xD800 && code <= 0xDBFF && peek(reader) == '\\' && peek_at(reader, 1) == 'u')
                {
                    next(reader);
                    next(reader);
                    unsigned long low = read_hex(reader, 4);
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }

                if (code == 0)
                {
                    reader_fail(reader, "\\u0000 is not supported in Syger strings");
                }

                buffer_add_utf8(&buffer, code);
                break;
            }
            default:
                reader_fail(reader, "invalid escape '\\%c' in string", e == -1 ? ' ' : e);
        }
    }

    return buffer_to_node(&buffer);
}

static DataNode *json_value(Reader *reader, int depth);

static DataNode *json_number(Reader *reader)
{
    size_t start = reader->position;

    if (peek(reader) == '-')
    {
        next(reader);
    }

    if (!isdigit(peek(reader)))
    {
        reader_fail(reader, "invalid number");
    }

    while (isdigit(peek(reader)) || peek(reader) == '.' || peek(reader) == 'e' || peek(reader) == 'E' ||
           ((peek(reader) == '+' || peek(reader) == '-') &&
            (reader->text[reader->position - 1] == 'e' || reader->text[reader->position - 1] == 'E')))
    {
        next(reader);
    }

    DataNode *node = NULL;

    if (!parse_number_text(reader->text + start, reader->position - start, &node))
    {
        reader_fail(reader, "invalid number '%.*s'", (int)(reader->position - start), reader->text + start);
    }

    return node;
}

static DataNode *json_value(Reader *reader, int depth)
{
    if (depth > 500)
    {
        reader_fail(reader, "nested too deeply");
    }

    json_space(reader);
    int c = peek(reader);

    if (c == '{')
    {
        next(reader);
        DataNode *map = node_new(DATA_MAP);
        json_space(reader);

        if (peek(reader) == '}')
        {
            next(reader);
            return map;
        }

        while (1)
        {
            json_space(reader);

            if (peek(reader) != '"')
            {
                reader_fail(reader, "expected a string key in quotes");
            }

            DataNode *key = json_string(reader);
            json_space(reader);

            if (next(reader) != ':')
            {
                reader_fail(reader, "expected ':' after key \"%s\"", key->string);
            }

            map_put(map, key, json_value(reader, depth + 1));
            json_space(reader);
            c = next(reader);

            if (c == '}')
            {
                return map;
            }

            if (c != ',')
            {
                reader_fail(reader, "expected ',' or '}' in object");
            }
        }
    }

    if (c == '[')
    {
        next(reader);
        DataNode *array = node_new(DATA_ARRAY);
        json_space(reader);

        if (peek(reader) == ']')
        {
            next(reader);
            return array;
        }

        while (1)
        {
            array_push(array, json_value(reader, depth + 1));
            json_space(reader);
            c = next(reader);

            if (c == ']')
            {
                return array;
            }

            if (c != ',')
            {
                reader_fail(reader, "expected ',' or ']' in array");
            }
        }
    }

    if (c == '"')
    {
        return json_string(reader);
    }

    if (c == '-' || isdigit(c))
    {
        return json_number(reader);
    }

    if (match_word(reader, "true"))
    {
        return node_bool(1);
    }

    if (match_word(reader, "false"))
    {
        return node_bool(0);
    }

    if (match_word(reader, "null"))
    {
        return node_new(DATA_NONE);
    }

    if (c == -1)
    {
        reader_fail(reader, "unexpected end of file");
    }

    reader_fail(reader, "unexpected character '%c'", c);
    return NULL;
}

static DataNode *parse_json(Reader *reader)
{
    DataNode *value = json_value(reader, 0);
    json_space(reader);

    if (peek(reader) != -1)
    {
        data_free(value);
        reader_fail(reader, "unexpected text after the JSON value");
    }

    return value;
}

/* ---- CSV / TSV ---------------------------------------------------------------- */

/* Fields that look exactly like numbers become numbers; "007" stays text. */
static DataNode *csv_field_value(const char *text, size_t length)
{
    size_t i = 0;

    if (length > 0 && text[0] == '-')
    {
        i = 1;
    }

    int looks_numeric = i < length && isdigit((unsigned char)text[i]) &&
                        !(text[i] == '0' && i + 1 < length && isdigit((unsigned char)text[i + 1]));

    for (size_t j = i; looks_numeric && j < length; j++)
    {
        char c = text[j];

        if (!isdigit((unsigned char)c) && c != '.' && c != 'e' && c != 'E' && c != '-' && c != '+')
        {
            looks_numeric = 0;
        }
    }

    DataNode *node = NULL;

    if (looks_numeric && parse_number_text(text, length, &node))
    {
        return node;
    }

    return node_string(text, length);
}

static int csv_read_row(Reader *reader, char separator, DataNode *row)
{
    if (peek(reader) == -1)
    {
        return 0;
    }

    while (1)
    {
        Buffer buffer = {0};
        int quoted = 0;

        if (peek(reader) == '"')
        {
            quoted = 1;
            next(reader);

            while (1)
            {
                int c = next(reader);

                if (c == -1)
                {
                    reader_fail(reader, "unterminated quoted field");
                }

                if (c == '"')
                {
                    if (peek(reader) == '"')
                    {
                        next(reader);
                        buffer_add(&buffer, '"');
                        continue;
                    }

                    break;
                }

                buffer_add(&buffer, (char)c);
            }
        }
        else
        {
            while (peek(reader) != -1 && peek(reader) != separator && peek(reader) != '\n' && peek(reader) != '\r')
            {
                buffer_add(&buffer, (char)next(reader));
            }
        }

        DataNode *field = quoted
            ? node_string(buffer.data ? buffer.data : "", buffer.length)
            : csv_field_value(buffer.data ? buffer.data : "", buffer.length);
        free(buffer.data);
        array_push(row, field);

        int c = peek(reader);

        if (c == separator)
        {
            next(reader);
            continue;
        }

        if (c == '\r')
        {
            next(reader);
        }

        if (peek(reader) == '\n')
        {
            next(reader);
        }
        else if (c != -1 && c != '\r')
        {
            reader_fail(reader, "unexpected text after a quoted field");
        }

        return 1;
    }
}

static DataNode *parse_csv(Reader *reader, char separator)
{
    DataNode *header = node_new(DATA_ARRAY);
    DataNode *rows = node_new(DATA_ARRAY);

    if (!csv_read_row(reader, separator, header))
    {
        data_free(header);
        return rows;
    }

    for (int i = 0; i < header->count; i++)
    {
        if (header->items[i]->type != DATA_STRING)
        {
            /* A numeric-looking header is still a column name. */
            char text[64];
            DataNode *old = header->items[i];

            if (old->type == DATA_INT)
            {
                snprintf(text, sizeof(text), "%ld", old->integer);
            }
            else
            {
                snprintf(text, sizeof(text), "%g", old->number);
            }

            header->items[i] = node_string(text, strlen(text));
            data_free(old);
        }
    }

    while (1)
    {
        int row_line = reader->line;
        DataNode *fields = node_new(DATA_ARRAY);

        if (!csv_read_row(reader, separator, fields))
        {
            data_free(fields);
            break;
        }

        /* Skip blank lines. */
        if (fields->count == 1 && fields->items[0]->type == DATA_STRING && fields->items[0]->length == 0)
        {
            data_free(fields);
            continue;
        }

        if (fields->count != header->count)
        {
            reader->line = row_line;
            reader_fail(reader, "row has %d fields but the header has %d", fields->count, header->count);
        }

        DataNode *row = node_new(DATA_MAP);

        for (int i = 0; i < fields->count; i++)
        {
            DataNode *name = header->items[i];
            map_put(row, node_string(name->string, name->length), fields->items[i]);
        }

        fields->count = 0;
        data_free(fields);
        array_push(rows, row);
    }

    data_free(header);
    return rows;
}

/* ---- .env --------------------------------------------------------------------- */

static DataNode *parse_env(Reader *reader)
{
    DataNode *map = node_new(DATA_MAP);

    while (peek(reader) != -1)
    {
        while (peek(reader) == ' ' || peek(reader) == '\t')
        {
            next(reader);
        }

        if (peek(reader) == '#' || peek(reader) == '\n' || peek(reader) == '\r')
        {
            while (peek(reader) != -1 && next(reader) != '\n')
            {
            }

            continue;
        }

        if (reader->position + 7 <= reader->length && memcmp(reader->text + reader->position, "export ", 7) == 0)
        {
            reader->position += 7;
        }

        size_t key_start = reader->position;

        while (isalnum(peek(reader)) || peek(reader) == '_' || peek(reader) == '.' || peek(reader) == '-')
        {
            next(reader);
        }

        size_t key_end = reader->position;

        while (peek(reader) == ' ' || peek(reader) == '\t')
        {
            next(reader);
        }

        if (key_end == key_start || next(reader) != '=')
        {
            reader_fail(reader, "expected NAME=value");
        }

        while (peek(reader) == ' ' || peek(reader) == '\t')
        {
            next(reader);
        }

        Buffer buffer = {0};
        int quote = peek(reader);

        if (quote == '"' || quote == '\'')
        {
            next(reader);

            while (1)
            {
                int c = next(reader);

                if (c == -1)
                {
                    reader_fail(reader, "unterminated quoted value");
                }

                if (c == quote)
                {
                    break;
                }

                if (c == '\\' && quote == '"')
                {
                    int e = next(reader);
                    buffer_add(&buffer, e == 'n' ? '\n' : e == 't' ? '\t' : (char)e);
                    continue;
                }

                buffer_add(&buffer, (char)c);
            }

            while (peek(reader) != -1 && next(reader) != '\n')
            {
            }
        }
        else
        {
            while (peek(reader) != -1 && peek(reader) != '\n' && peek(reader) != '\r')
            {
                /* " #" starts a comment in an unquoted value */
                if (peek(reader) == '#' && buffer.length > 0 &&
                    (buffer.data[buffer.length - 1] == ' ' || buffer.data[buffer.length - 1] == '\t'))
                {
                    break;
                }

                buffer_add(&buffer, (char)next(reader));
            }

            while (buffer.length > 0 && (buffer.data[buffer.length - 1] == ' ' || buffer.data[buffer.length - 1] == '\t'))
            {
                buffer.length--;
            }

            while (peek(reader) != -1 && next(reader) != '\n')
            {
            }
        }

        map_put(map, node_string(reader->text + key_start, key_end - key_start), buffer_to_node(&buffer));
    }

    return map;
}

/* ---- Python constants ------------------------------------------------------------ */

/* Skips spaces, and inside brackets also newlines, comments and continuations. */
static void py_space(Reader *reader, int in_brackets)
{
    while (1)
    {
        int c = peek(reader);

        if (c == ' ' || c == '\t' || c == '\f')
        {
            next(reader);
        }
        else if (c == '\\' && (peek_at(reader, 1) == '\n' || peek_at(reader, 1) == '\r'))
        {
            next(reader);
            if (peek(reader) == '\r') next(reader);
            if (peek(reader) == '\n') next(reader);
        }
        else if (in_brackets && (c == '\n' || c == '\r'))
        {
            next(reader);
        }
        else if (in_brackets && c == '#')
        {
            while (peek(reader) != -1 && peek(reader) != '\n')
            {
                next(reader);
            }
        }
        else
        {
            return;
        }
    }
}

/* Marker for "this is valid Python but not a constant": the statement is skipped. */
static jmp_buf *py_skip;

static void py_not_constant(void)
{
    longjmp(*py_skip, 1);
}

/* skipping: true while skipping a statement, where f-strings are just strings. */
static int py_string_start_mode(Reader *reader, int *raw, int *quote, int *triple, int skipping)
{
    size_t offset = 0;
    *raw = 0;

    /* Prefixes: r, b, u, rb, br (f-strings are not constants). */
    while (offset < 2)
    {
        int c = tolower(peek_at(reader, offset));

        if (c == 'r')
        {
            *raw = 1;
            offset++;
        }
        else if (c == 'b' || c == 'u')
        {
            offset++;
        }
        else if (c == 'f')
        {
            int q = peek_at(reader, offset + 1);

            if (skipping)
            {
                offset++;
                continue;
            }

            if (q == '"' || q == '\'') py_not_constant();
            return 0;
        }
        else
        {
            break;
        }
    }

    int q = peek_at(reader, offset);

    if (q != '"' && q != '\'')
    {
        return 0;
    }

    reader->position += offset;
    *quote = q;
    *triple = peek_at(reader, 1) == q && peek_at(reader, 2) == q;
    return 1;
}

static int py_string_start(Reader *reader, int *raw, int *quote, int *triple)
{
    return py_string_start_mode(reader, raw, quote, triple, 0);
}

static void py_string_body(Reader *reader, Buffer *buffer, int raw, int quote, int triple)
{
    next(reader);

    if (triple)
    {
        next(reader);
        next(reader);
    }

    while (1)
    {
        int c = next(reader);

        if (c == -1 || (c == '\n' && !triple))
        {
            reader_fail(reader, "unterminated string");
        }

        if (c == quote && (!triple || (peek(reader) == quote && peek_at(reader, 1) == quote)))
        {
            if (triple)
            {
                next(reader);
                next(reader);
            }

            return;
        }

        if (c == '\\' && !raw)
        {
            int e = next(reader);

            switch (e)
            {
                case 'n': buffer_add(buffer, '\n'); break;
                case 't': buffer_add(buffer, '\t'); break;
                case 'r': buffer_add(buffer, '\r'); break;
                case '\\': buffer_add(buffer, '\\'); break;
                case '\'': buffer_add(buffer, '\''); break;
                case '"': buffer_add(buffer, '"'); break;
                case '\n': break;
                case 'x': buffer_add_utf8(buffer, read_hex(reader, 2)); break;
                case 'u': buffer_add_utf8(buffer, read_hex(reader, 4)); break;
                case 'U': buffer_add_utf8(buffer, read_hex(reader, 8)); break;
                default:
                    buffer_add(buffer, '\\');
                    buffer_add(buffer, (char)e);
            }

            continue;
        }

        if (c == '\\' && raw)
        {
            buffer_add(buffer, '\\');
            c = next(reader);
        }

        buffer_add(buffer, (char)c);
    }
}

static DataNode *py_value(Reader *reader, int in_brackets, int depth);

static DataNode *py_number(Reader *reader, int negative)
{
    size_t start = reader->position;

    if (peek(reader) == '0' && strchr("xXoObB", peek_at(reader, 1)) != NULL && peek_at(reader, 1) != -1)
    {
        int base = strchr("xX", peek_at(reader, 1)) ? 16 : strchr("oO", peek_at(reader, 1)) ? 8 : 2;
        next(reader);
        next(reader);
        long value = 0;

        while (isalnum(peek(reader)) || peek(reader) == '_')
        {
            int c = next(reader);

            if (c == '_')
            {
                continue;
            }

            int digit = hex_value(c);

            if (digit < 0 || digit >= base)
            {
                reader_fail(reader, "invalid digit in number");
            }

            value = value * base + digit;
        }

        return node_int(negative ? -value : value);
    }

    while (isdigit(peek(reader)) || peek(reader) == '_' || peek(reader) == '.' ||
           peek(reader) == 'e' || peek(reader) == 'E' ||
           ((peek(reader) == '+' || peek(reader) == '-') &&
            (reader->text[reader->position - 1] == 'e' || reader->text[reader->position - 1] == 'E')))
    {
        next(reader);
    }

    if (peek(reader) == 'j' || peek(reader) == 'J')
    {
        py_not_constant(); /* complex numbers */
    }

    DataNode *node = NULL;

    if (!parse_number_text(reader->text + start, reader->position - start, &node))
    {
        reader_fail(reader, "invalid number");
    }

    if (negative)
    {
        if (node->type == DATA_INT) node->integer = -node->integer;
        else node->number = -node->number;
    }

    return node;
}

/* A comma-separated sequence ending in close; returns 1 if a comma was seen. */
static int py_sequence(Reader *reader, int close, DataNode *array, int depth)
{
    int saw_comma = 0;

    while (1)
    {
        py_space(reader, 1);

        if (peek(reader) == close)
        {
            next(reader);
            return saw_comma;
        }

        array_push(array, py_value(reader, 1, depth + 1));
        py_space(reader, 1);

        if (peek(reader) == ',')
        {
            next(reader);
            saw_comma = 1;
            continue;
        }

        if (peek(reader) != close)
        {
            py_not_constant();
        }
    }
}

static DataNode *py_value(Reader *reader, int in_brackets, int depth)
{
    if (depth > 200)
    {
        reader_fail(reader, "nested too deeply");
    }

    py_space(reader, in_brackets);
    int c = peek(reader);
    int raw, quote, triple;

    if (py_string_start(reader, &raw, &quote, &triple))
    {
        Buffer buffer = {0};
        py_string_body(reader, &buffer, raw, quote, triple);

        /* Adjacent literals join: "a" "b" is "ab". */
        while (1)
        {
            size_t saved = reader->position;
            int saved_line = reader->line;
            py_space(reader, in_brackets);

            if (!py_string_start(reader, &raw, &quote, &triple))
            {
                reader->position = saved;
                reader->line = saved_line;
                break;
            }

            py_string_body(reader, &buffer, raw, quote, triple);
        }

        return buffer_to_node(&buffer);
    }

    if (c == '-' || c == '+')
    {
        next(reader);
        py_space(reader, in_brackets);

        if (!isdigit(peek(reader)) && peek(reader) != '.')
        {
            py_not_constant();
        }

        return py_number(reader, c == '-');
    }

    if (isdigit(c) || (c == '.' && isdigit(peek_at(reader, 1))))
    {
        return py_number(reader, 0);
    }

    if (c == '[')
    {
        next(reader);
        DataNode *array = node_new(DATA_ARRAY);
        py_sequence(reader, ']', array, depth);
        return array;
    }

    if (c == '(')
    {
        next(reader);
        DataNode *array = node_new(DATA_ARRAY);
        int saw_comma = py_sequence(reader, ')', array, depth);

        /* (x) is just x; (x,) and () are tuples. */
        if (array->count == 1 && !saw_comma)
        {
            DataNode *inner = array->items[0];
            array->count = 0;
            data_free(array);
            return inner;
        }

        return array;
    }

    if (c == '{')
    {
        next(reader);
        py_space(reader, 1);

        if (peek(reader) == '}')
        {
            next(reader);
            return node_new(DATA_MAP);
        }

        DataNode *first = py_value(reader, 1, depth + 1);
        py_space(reader, 1);

        if (peek(reader) != ':')
        {
            /* A set: keep it as an array. */
            DataNode *array = node_new(DATA_ARRAY);
            array_push(array, first);

            if (peek(reader) == ',')
            {
                next(reader);
                py_sequence(reader, '}', array, depth);
            }
            else if (next(reader) != '}')
            {
                py_not_constant();
            }

            return array;
        }

        DataNode *map = node_new(DATA_MAP);
        DataNode *key = first;

        while (1)
        {
            if (key->type != DATA_STRING && key->type != DATA_INT)
            {
                reader_fail(reader, "dict keys must be strings or integers to import");
            }

            py_space(reader, 1);

            if (next(reader) != ':')
            {
                py_not_constant();
            }

            map_put(map, key, py_value(reader, 1, depth + 1));
            py_space(reader, 1);
            c = next(reader);

            if (c == '}')
            {
                return map;
            }

            if (c != ',')
            {
                py_not_constant();
            }

            py_space(reader, 1);

            if (peek(reader) == '}')
            {
                next(reader);
                return map;
            }

            key = py_value(reader, 1, depth + 1);
        }
    }

    if (match_word(reader, "True"))
    {
        return node_bool(1);
    }

    if (match_word(reader, "False"))
    {
        return node_bool(0);
    }

    if (match_word(reader, "None"))
    {
        return node_new(DATA_NONE);
    }

    py_not_constant();
    return NULL;
}

/* Skips to the end of the current logical line, minding brackets and strings. */
static void py_skip_statement(Reader *reader)
{
    int depth = 0;

    while (peek(reader) != -1)
    {
        int raw, quote, triple;

        int c0 = tolower(peek(reader));
        int starts_string = c0 == '"' || c0 == '\'' ||
            ((c0 == 'f' || c0 == 'r' || c0 == 'b' || c0 == 'u') &&
             (reader->position == 0 || !(isalnum((unsigned char)reader->text[reader->position - 1]) || reader->text[reader->position - 1] == '_')));

        if (starts_string && py_string_start_mode(reader, &raw, &quote, &triple, 1))
        {
            Buffer ignored = {0};
            DataError saved = *reader->error;
            jmp_buf saved_fail;
            memcpy(saved_fail, reader->fail, sizeof(jmp_buf));

            if (setjmp(reader->fail) == 0)
            {
                py_string_body(reader, &ignored, raw, quote, triple);
            }

            memcpy(reader->fail, saved_fail, sizeof(jmp_buf));
            *reader->error = saved;
            free(ignored.data);
            continue;
        }

        int c = next(reader);

        if (c == '#')
        {
            while (peek(reader) != -1 && peek(reader) != '\n')
            {
                next(reader);
            }
        }
        else if (c == '(' || c == '[' || c == '{')
        {
            depth++;
        }
        else if ((c == ')' || c == ']' || c == '}') && depth > 0)
        {
            depth--;
        }
        else if (c == '\\' && peek(reader) == '\n')
        {
            next(reader);
        }
        else if (c == '\n' && depth == 0)
        {
            return;
        }
    }
}

static DataNode *parse_py(Reader *reader)
{
    DataNode *constants = node_new(DATA_MAP);
    jmp_buf skip;
    jmp_buf *outer = py_skip;
    py_skip = &skip;

    while (peek(reader) != -1)
    {
        size_t statement_start = reader->position;
        int statement_line = reader->line;
        int c = peek(reader);

        /* Only unindented NAME = literal statements are constants. */
        if (!(isalpha(c) || c == '_'))
        {
            py_skip_statement(reader);
            continue;
        }

        while (isalnum(peek(reader)) || peek(reader) == '_')
        {
            next(reader);
        }

        size_t name_end = reader->position;
        py_space(reader, 0);

        /* NAME: type = value */
        if (peek(reader) == ':')
        {
            while (peek(reader) != -1 && peek(reader) != '=' && peek(reader) != '\n')
            {
                next(reader);
            }
        }

        if (peek(reader) != '=' || peek_at(reader, 1) == '=')
        {
            reader->position = statement_start;
            reader->line = statement_line;
            py_skip_statement(reader);
            continue;
        }

        next(reader);

        if (setjmp(skip) == 0)
        {
            DataNode *value = py_value(reader, 0, 0);
            py_space(reader, 0);

            if (peek(reader) == '#')
            {
                while (peek(reader) != -1 && peek(reader) != '\n')
                {
                    next(reader);
                }
            }

            if (peek(reader) == ';' || (peek(reader) != '\n' && peek(reader) != '\r' && peek(reader) != -1))
            {
                data_free(value);
                py_not_constant();
            }

            map_put(constants, node_string(reader->text + statement_start, name_end - statement_start), value);
            py_skip_statement(reader);
        }
        else
        {
            /* Not a literal (a call, an expression...): skip the whole statement. */
            reader->position = statement_start;
            reader->line = statement_line;
            py_skip_statement(reader);
        }
    }

    py_skip = outer;
    return constants;
}

/* ---- entry points ---------------------------------------------------------------- */

static int ends_with(const char *text, const char *suffix)
{
    size_t a = strlen(text), b = strlen(suffix);

    if (b > a)
    {
        return 0;
    }

    for (size_t i = 0; i < b; i++)
    {
        if (tolower((unsigned char)text[a - b + i]) != suffix[i])
        {
            return 0;
        }
    }

    return 1;
}

const char *data_format_for_path(const char *path)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    if (ends_with(base, ".json")) return "json";
    if (ends_with(base, ".csv")) return "csv";
    if (ends_with(base, ".tsv")) return "tsv";
    if (ends_with(base, ".py")) return "py";
    if (ends_with(base, ".env") || strcmp(base, ".env") == 0 || strncmp(base, ".env.", 5) == 0) return "env";
    return "text";
}

DataNode *data_parse(const char *text, size_t length, const char *format, DataError *error)
{
    Reader *reader = data_alloc(sizeof(Reader));
    reader->text = text;
    reader->length = length;
    reader->line = 1;
    reader->error = error;
    error->line = 0;
    error->message[0] = '\0';

    /* A UTF-8 byte order mark is not part of the data. */
    if (length >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0)
    {
        reader->position = 3;
    }

    DataNode *result = NULL;

    if (setjmp(reader->fail) == 0)
    {
        if (strcmp(format, "json") == 0) result = parse_json(reader);
        else if (strcmp(format, "csv") == 0) result = parse_csv(reader, ',');
        else if (strcmp(format, "tsv") == 0) result = parse_csv(reader, '\t');
        else if (strcmp(format, "env") == 0) result = parse_env(reader);
        else if (strcmp(format, "py") == 0) result = parse_py(reader);
        else
        {
            if (memchr(text, '\0', length) != NULL)
            {
                reader_fail(reader, "file contains a zero byte (binary files can't be imported)");
            }

            result = node_string(text, length);
        }
    }
    else
    {
        result = NULL; /* nodes allocated before the error are leaked; errors end the program */
    }

    free(reader);
    return result;
}
