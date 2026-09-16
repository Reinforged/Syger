#include <stdio.h>
#include <stdlib.h>

#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "interpreter.h"

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        fprintf(stderr, "Could not open file: %s\n", path);
        exit(1);
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    if (size < 0) {
        fclose(file);
        fprintf(stderr, "Could not determine file size.\n");
        exit(1);
    }

    char *buffer = malloc((size_t)size + 1);

    if (buffer == NULL) {
        fclose(file);
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    size_t bytes_read = fread(buffer, 1, (size_t)size, file);
    buffer[bytes_read] = '\0';

    fclose(file);

    return buffer;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: Syger <file.sg>\n");
        return 1;
    }

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
