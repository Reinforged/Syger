#ifndef SYGER_LEXER_H
#define SYGER_LEXER_H

typedef enum {
    TOKEN_EOF,
    TOKEN_INDENT,
    TOKEN_DEDENT,
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_COMMA,
    TOKEN_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_BANG_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_ELIF,
    TOKEN_WHILE,
    TOKEN_FUNCTION,
    TOKEN_RETURN,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    TOKEN_NUMBER,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_LEFT_BRACKET,
    TOKEN_RIGHT_BRACKET
} TokenType;

typedef struct {
    TokenType type;

    const char *start;
    int length;

    int line;
} Token;

typedef struct {
    const char *start;
    const char *current;
    int line;
    int indent_stack[64];
    int indent_count;
    int pending_dedents;
    int at_line_start;
} Lexer;

void lexer_init(Lexer *lexer, const char *source);
Token lexer_next(Lexer *lexer);

#endif
