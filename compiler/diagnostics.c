#include "diagnostics.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_source_line(
    const char *source,
    const Token *token
)
{
    if (source == NULL || token == NULL || token->start == NULL)
    {
        return;
    }

    const char *line_start = token->start;

    while (line_start > source && line_start[-1] != '\n')
    {
        line_start--;
    }

    const char *line_end = token->start;

    while (*line_end != '\0' && *line_end != '\n')
    {
        line_end++;
    }

    int column = (int)(token->start - line_start) + 1;
    int length = token->length > 0 ? token->length : 1;

    fprintf(stderr, "  %d | %.*s\n", token->line, (int)(line_end - line_start), line_start);
    fprintf(stderr, "      | ");

    for (int i = 1; i < column; i++)
    {
        fputc(' ', stderr);
    }

    for (int i = 0; i < length; i++)
    {
        fputc('^', stderr);
    }

    fputc('\n', stderr);
}

_Noreturn void diagnostics_error(
    const char *source,
    const Token *token,
    const char *code,
    const char *message,
    const char *help
)
{
    fprintf(
        stderr,
        "Syger error [%s] on line %d:\n",
        code,
        token->line
    );

    print_source_line(source, token);
    fprintf(stderr, "  %s\n", message);

    if (help != NULL)
    {
        fprintf(stderr, "  help: %s\n", help);
    }

    exit(1);
}

_Noreturn void diagnostics_runtime_error(
    const char *code,
    const char *message,
    const char *help
)
{
    fprintf(stderr, "Syger runtime error [%s]: %s\n", code, message);

    if (help != NULL)
    {
        fprintf(stderr, "help: %s\n", help);
    }

    exit(1);
}

_Noreturn void diagnostics_runtime_errorf(
    const char *code,
    const char *help,
    const char *format,
    ...
)
{
    fprintf(stderr, "Syger runtime error [%s]: ", code);

    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);

    fputc('\n', stderr);

    if (help != NULL)
    {
        fprintf(stderr, "  help: %s\n", help);
    }

    exit(1);
}

_Noreturn void diagnostics_fatal(
    const char *code,
    const char *message,
    const char *help
)
{
    fprintf(stderr, "Syger error [%s]: %s\n", code, message);

    if (help != NULL)
    {
        fprintf(stderr, "  help: %s\n", help);
    }

    exit(1);
}

