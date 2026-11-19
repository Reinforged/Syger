#ifndef SYGER_DIAGNOSTICS_H
#define SYGER_DIAGNOSTICS_H

#include "lexer.h"

/*
 * Every error Syger reports goes through this module, so they all share one
 * format:
 *
 *   error[E3001]: variable 'totl' is not defined
 *    --> examples/demo.sg:12
 *      |
 *   12 |     print(totl)
 *      |
 *      = help: did you mean 'total'?
 *      = note: in function 'report', called from line 20
 */

/* The file being run, used to print locations and source lines. */
void diagnostics_set_source(const char *path, const char *source);

/* Line of the statement currently executing; set by the interpreter. */
extern int diagnostics_line;

/* Call stack used to print where a failing function was called from. */
void diagnostics_push_frame(const char *function_name, int call_line);
void diagnostics_pop_frame(void);

/* Lexer and parser errors, pointing at a token with a caret. */
_Noreturn void diagnostics_syntax_error(
    const Token *token,
    const char *code,
    const char *message,
    const char *help
);

/* Kept for older callers: same as diagnostics_syntax_error. */
_Noreturn void diagnostics_error(
    const char *source,
    const Token *token,
    const char *code,
    const char *message,
    const char *help
);

/* Runtime errors, reported at diagnostics_line. */
_Noreturn void diagnostics_runtime_error(
    const char *code,
    const char *message,
    const char *help
);

_Noreturn void diagnostics_runtime_errorf(
    const char *code,
    const char *help,
    const char *format,
    ...
);

/* Errors with no source location (missing file, out of memory). */
_Noreturn void diagnostics_fatal(
    const char *code,
    const char *message,
    const char *help
);

/* Closest candidate to name by edit distance, or NULL if none is close. */
const char *diagnostics_closest_name(
    const char *name,
    const char *const *candidates,
    int candidate_count
);

#endif
