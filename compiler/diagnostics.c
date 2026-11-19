#include "diagnostics.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int diagnostics_line = 0;

static const char *source_path = NULL;
static const char *source_text = NULL;

#define MAX_FRAMES 4096

typedef struct {
    const char *name;
    int call_line;
} Frame;

static Frame frames[MAX_FRAMES];
static int frame_count = 0;

void diagnostics_set_source(const char *path, const char *source)
{
    source_path = path;
    source_text = source;
}

void diagnostics_push_frame(const char *function_name, int call_line)
{
    if (frame_count < MAX_FRAMES)
    {
        frames[frame_count].name = function_name;
        frames[frame_count].call_line = call_line;
    }

    frame_count++;
}

void diagnostics_pop_frame(void)
{
    if (frame_count > 0)
    {
        frame_count--;
    }
}

/* ---- output helpers ------------------------------------------------------ */

static int use_color(void)
{
    static int decided = 0;
    static int color = 0;

    if (!decided)
    {
        color = isatty(fileno(stderr)) && getenv("NO_COLOR") == NULL;
        decided = 1;
    }

    return color;
}

static const char *red(void) { return use_color() ? "\033[1;31m" : ""; }
static const char *blue(void) { return use_color() ? "\033[1;34m" : ""; }
static const char *cyan(void) { return use_color() ? "\033[1;36m" : ""; }
static const char *bold(void) { return use_color() ? "\033[1m" : ""; }
static const char *reset(void) { return use_color() ? "\033[0m" : ""; }

static int digits(int number)
{
    int count = 1;

    while (number >= 10)
    {
        number /= 10;
        count++;
    }

    return count;
}

/* Finds the text of a 1-based line in the source. */
static const char *find_line(int line, int *length)
{
    if (source_text == NULL || line <= 0)
    {
        return NULL;
    }

    const char *start = source_text;

    for (int current = 1; current < line; current++)
    {
        start = strchr(start, '\n');

        if (start == NULL)
        {
            return NULL;
        }

        start++;
    }

    const char *end = start;

    while (*end != '\0' && *end != '\n' && *end != '\r')
    {
        end++;
    }

    *length = (int)(end - start);
    return start;
}

static void print_header(const char *code, const char *message)
{
    size_t length = strlen(message);

    /* Older messages end in "\n"; drop it so the layout stays tidy. */
    while (length > 0 && (message[length - 1] == '\n' || message[length - 1] == ' ' ||
                          message[length - 1] == '.'))
    {
        length--;
    }

    fprintf(stderr, "%serror[%s]%s%s: %.*s%s\n", red(), code, reset(), bold(), (int)length, message, reset());
}

static void print_location(int line, int column, int caret_length, const char *help)
{
    int width = digits(line > 0 ? line : 1);

    if (source_path != NULL && line > 0)
    {
        if (column > 0)
        {
            fprintf(stderr, "%*s%s-->%s %s:%d:%d\n", width, "", blue(), reset(), source_path, line, column);
        }
        else
        {
            fprintf(stderr, "%*s%s-->%s %s:%d\n", width, "", blue(), reset(), source_path, line);
        }
    }

    int text_length = 0;
    const char *text = find_line(line, &text_length);

    if (text != NULL)
    {
        fprintf(stderr, "%*s %s|%s\n", width, "", blue(), reset());
        fprintf(stderr, "%s%*d |%s %.*s\n", blue(), width, line, reset(), text_length, text);
        fprintf(stderr, "%*s %s|%s", width, "", blue(), reset());

        if (column > 0)
        {
            fprintf(stderr, " %*s%s", column - 1, "", red());

            for (int i = 0; i < (caret_length > 0 ? caret_length : 1); i++)
            {
                fputc('^', stderr);
            }

            fputs(reset(), stderr);
        }

        fputc('\n', stderr);
    }

    if (help != NULL && help[0] != '\0')
    {
        fprintf(stderr, "%*s %s= help:%s %s\n", width, "", cyan(), reset(), help);
    }

    /* Innermost call first, at most 10 entries. */
    int shown = 0;

    for (int i = frame_count - 1; i >= 0 && shown < 10; i--, shown++)
    {
        if (i >= MAX_FRAMES)
        {
            continue;
        }

        fprintf(stderr, "%*s %s= note:%s in function '%s', called from line %d\n",
                width, "", cyan(), reset(), frames[i].name, frames[i].call_line);
    }

    if (frame_count > shown)
    {
        fprintf(stderr, "%*s %s= note:%s ... %d more calls\n", width, "", cyan(), reset(), frame_count - shown);
    }
}

/* ---- public API ---------------------------------------------------------- */

_Noreturn void diagnostics_syntax_error(
    const Token *token,
    const char *code,
    const char *message,
    const char *help
)
{
    fflush(stdout);
    print_header(code, message);

    int column = 0;
    int length = 1;
    int line = token != NULL ? token->line : diagnostics_line;

    if (token != NULL && token->start != NULL && source_text != NULL &&
        token->start >= source_text)
    {
        const char *line_start = token->start;

        while (line_start > source_text && line_start[-1] != '\n')
        {
            line_start--;
        }

        column = (int)(token->start - line_start) + 1;
        length = token->length > 0 ? token->length : 1;
    }

    frame_count = 0;
    print_location(line, column, length, help);
    exit(1);
}

_Noreturn void diagnostics_error(
    const char *source,
    const Token *token,
    const char *code,
    const char *message,
    const char *help
)
{
    if (source_text == NULL)
    {
        source_text = source;
    }

    diagnostics_syntax_error(token, code, message, help);
}

_Noreturn void diagnostics_runtime_error(
    const char *code,
    const char *message,
    const char *help
)
{
    fflush(stdout);
    print_header(code, message);
    print_location(diagnostics_line, 0, 0, help);
    exit(1);
}

_Noreturn void diagnostics_runtime_errorf(
    const char *code,
    const char *help,
    const char *format,
    ...
)
{
    char message[1024];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    diagnostics_runtime_error(code, message, help);
}

_Noreturn void diagnostics_fatal(
    const char *code,
    const char *message,
    const char *help
)
{
    fflush(stdout);
    print_header(code, message);

    if (help != NULL)
    {
        fprintf(stderr, "  %s= help:%s %s\n", cyan(), reset(), help);
    }

    exit(1);
}

/* ---- suggestions --------------------------------------------------------- */

static int edit_distance(const char *a, const char *b)
{
    int a_length = (int)strlen(a);
    int b_length = (int)strlen(b);

    if (a_length > 63 || b_length > 63)
    {
        return 1000;
    }

    int row[64];

    for (int j = 0; j <= b_length; j++)
    {
        row[j] = j;
    }

    for (int i = 1; i <= a_length; i++)
    {
        int diagonal = row[0];
        row[0] = i;

        for (int j = 1; j <= b_length; j++)
        {
            int above = row[j];
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            int best = diagonal + cost;

            if (row[j] + 1 < best) best = row[j] + 1;
            if (row[j - 1] + 1 < best) best = row[j - 1] + 1;

            row[j] = best;
            diagonal = above;
        }
    }

    return row[b_length];
}

const char *diagnostics_closest_name(
    const char *name,
    const char *const *candidates,
    int candidate_count
)
{
    int length = (int)strlen(name);
    int limit = length <= 3 ? 1 : (length <= 6 ? 2 : 3);
    const char *best = NULL;
    int best_distance = limit + 1;

    for (int i = 0; i < candidate_count; i++)
    {
        if (candidates[i] == NULL || strcmp(candidates[i], name) == 0)
        {
            continue;
        }

        int distance = edit_distance(name, candidates[i]);

        if (distance < best_distance)
        {
            best = candidates[i];
            best_distance = distance;
        }
    }

    return best;
}
