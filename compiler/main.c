#include <stdio.h>
#include <stdlib.h>

#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "interpreter.h"
#include "diagnostics.h"

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");

    if (file == NULL)
    {
        diagnostics_runtime_errorf("E0001", "Check that the file exists and is readable.", "Could not open file: %s", path);
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    if (size < 0) {
        fclose(file);
        diagnostics_fatal("E0002", "Could not determine file size.", "Check that the input is a regular readable file.");
    }

    char *buffer = malloc((size_t)size + 1);

    if (buffer == NULL) {
        fclose(file);
        diagnostics_fatal("E0003", "Out of memory.", "Close other applications or reduce the size of the input.");
    }

    size_t bytes_read = fread(buffer, 1, (size_t)size, file);
    buffer[bytes_read] = '\0';

    fclose(file);

    return buffer;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        diagnostics_fatal("E0004", "Expected a source file.", "Usage: Syger <file.sg> [program arguments...]");
    }

    interpreter_set_arguments(argc - 2, argv + 2);
    char *source = read_file(argv[1]);

    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    parser_init(&parser, &lexer);

    AstNode *ast = parser_parse(&parser);

    interpreter_run(ast);

    ast_free(ast);
    free(source);

    return 0;
}
