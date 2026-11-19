#ifndef SYGER_DIAGNOSTICS_H
#define SYGER_DIAGNOSTICS_H

#include "lexer.h"

_Noreturn void diagnostics_error(
    const char *source,
    const Token *token,
    const char *code,
    const char *message,
    const char *help
);

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

_Noreturn void diagnostics_fatal(
    const char *code,
    const char *message,
    const char *help
);


#endif
