#ifndef SYGER_BUILTINS_H
#define SYGER_BUILTINS_H

#include "value.h"

/*
 * Builtins that only need their argument values (no variables to change).
 * The interpreter evaluates the arguments and calls builtin_call; the
 * arguments are consumed.
 */

int builtin_exists(const char *name);

/* Reports E3003 if count is outside what name accepts. */
void builtin_check_arity(const char *name, int count);

/* "split(text, separator)" style usage line, or NULL if unknown. */
const char *builtin_usage_text(const char *name);

Value builtin_call(const char *name, Value *args, int count);

/* The script being run; import paths are relative to its directory. */
void builtins_set_script(const char *path);

/* import "path": the file's contents as Syger values. */
Value builtin_import(const char *path);

/* Sorts numbers or strings in place (the array must be unique). */
void builtin_sort(Value *array);

#endif
