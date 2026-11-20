#ifndef SYGER_DATA_H
#define SYGER_DATA_H

#include <stddef.h>

/*
 * Parsers for the file formats `import` understands. They build a small
 * tree of DataNodes, which the interpreter and the native runtime each turn
 * into their own values. This file is shared by both, so every format is
 * implemented exactly once.
 *
 *   .json             objects become maps, arrays become arrays
 *   .csv, .tsv        an array with one map per row, keyed by the header row
 *   .env              a map of strings
 *   .py               a map of the top-level constants (NAME = literal);
 *                     the Python code is never run
 *   anything else     the file's text as one string
 */

typedef enum {
    DATA_NONE,
    DATA_BOOL,
    DATA_INT,
    DATA_FLOAT,
    DATA_STRING,
    DATA_ARRAY,
    DATA_MAP
} DataType;

typedef struct DataNode DataNode;

struct DataNode {
    DataType type;
    long integer;
    double number;
    int boolean;
    char *string;      /* DATA_STRING, NUL-terminated */
    size_t length;
    DataNode **items;  /* array items, or map values */
    DataNode **keys;   /* map keys: DATA_STRING or DATA_INT nodes */
    int count;
    int capacity;
};

typedef struct {
    int line;           /* line in the data file, 0 if unknown */
    char message[256];
} DataError;

/* "json", "csv", "tsv", "env", "py" or "text", from the file name. */
const char *data_format_for_path(const char *path);

/* Returns NULL and fills error if the text isn't valid for the format. */
DataNode *data_parse(const char *text, size_t length, const char *format, DataError *error);

void data_free(DataNode *node);

#endif
