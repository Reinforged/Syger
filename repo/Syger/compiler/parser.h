#ifndef SYGER_PARSER_H
#define SYGER_PARSER_H

#include "ast.h"
#include "lexer.h"

typedef struct {
    Lexer *lexer;
    Token current;
    Token previous;
} Parser;

void parser_init(Parser *parser, Lexer *lexer);

AstNode *parser_parse(Parser *parser);

#endif
