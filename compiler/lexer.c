#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

static int is_at_end(Lexer *lexer)
{
    return *lexer->current == '\0';
}

static char advance_char(Lexer *lexer)
{
    lexer->current++;
    return lexer->current[-1];
}

static char peek_char(Lexer *lexer)
{
    return *lexer->current;
}

static Token make_token(Lexer *lexer, TokenType type)
{
    Token token;

    token.type = type;
    token.start = lexer->start;
    token.length = (int)(lexer->current - lexer->start);
    token.line = lexer->line;

    return token;
}

static void skip_whitespace(Lexer *lexer)
{
    while (1)
    {
        char c = peek_char(lexer);

        if (c == ' ' || c == '\t' || c == '\r')
        {
            advance_char(lexer);
        }
        else if (c == '\n')
        {
            lexer->line++;
            advance_char(lexer);
            lexer->at_line_start = 1;
            return;
        }
        else
        {
            return;
        }
    }
}

static Token string_token(Lexer *lexer)
{
    while (!is_at_end(lexer) && peek_char(lexer) != '"')
    {
        if (peek_char(lexer) == '\n')
        {
            lexer->line++;
        }

        advance_char(lexer);
    }

    if (!is_at_end(lexer))
    {
        advance_char(lexer);
    }

    return make_token(lexer, TOKEN_STRING);
}

static Token identifier_token(Lexer *lexer)
{
    while (
        isalnum((unsigned char)peek_char(lexer)) ||
        peek_char(lexer) == '_'
    )
    {
        advance_char(lexer);
    }

    int length = (int)(lexer->current - lexer->start);

    if (length == 2 &&
        lexer->start[0] == 'i' &&
        lexer->start[1] == 'f')
    {
        return make_token(lexer, TOKEN_IF);
    }

    if (length == 4 &&
        lexer->start[0] == 'e' &&
        lexer->start[1] == 'l' &&
        lexer->start[2] == 's' &&
        lexer->start[3] == 'e')
    {
        return make_token(lexer, TOKEN_ELSE);
    }

    if (length == 4 &&
        lexer->start[0] == 'e' &&
        lexer->start[1] == 'l' &&
        lexer->start[2] == 'i' &&
        lexer->start[3] == 'f')
    {
        return make_token(lexer, TOKEN_ELIF);
    }
    
    if (length == 4 &&
        lexer->start[0] == 't' &&
        lexer->start[1] == 'r' &&
        lexer->start[2] == 'u' &&
        lexer->start[3] == 'e')
    {
        return make_token(lexer, TOKEN_TRUE);
    }

    if (length == 5 &&
        lexer->start[0] == 'f' &&
        lexer->start[1] == 'a' &&
        lexer->start[2] == 'l' &&
        lexer->start[3] == 's' &&
        lexer->start[4] == 'e')
    {
        return make_token(lexer, TOKEN_FALSE);
    }
    
    if (length == 4 &&
        lexer->start[0] == 'n' &&
        lexer->start[1] == 'o' &&
        lexer->start[2] == 'n' &&
        lexer->start[3] == 'e')
    {
        return make_token(lexer, TOKEN_NONE);
    }

    if (length == 3 &&
        lexer->start[0] == 'a' &&
        lexer->start[1] == 'n' &&
        lexer->start[2] == 'd')
    {
        return make_token(lexer, TOKEN_AND);
    }

    if (length == 2 &&
        lexer->start[0] == 'o' &&
        lexer->start[1] == 'r')
    {
        return make_token(lexer, TOKEN_OR);
    }

    if (length == 3 &&
        lexer->start[0] == 'n' &&
        lexer->start[1] == 'o' &&
        lexer->start[2] == 't')
    {
        return make_token(lexer, TOKEN_NOT);
    }
    
    if (length == 5 &&
        lexer->start[0] == 'w' &&
        lexer->start[1] == 'h' &&
        lexer->start[2] == 'i' &&
        lexer->start[3] == 'l' &&
        lexer->start[4] == 'e')
    {
        return make_token(lexer, TOKEN_WHILE);
    }
    
    if (length == 8 &&
        lexer->start[0] == 'f' &&
        lexer->start[1] == 'u' &&
        lexer->start[2] == 'n' &&
        lexer->start[3] == 'c' &&
        lexer->start[4] == 't' &&
        lexer->start[5] == 'i' &&
        lexer->start[6] == 'o' &&
        lexer->start[7] == 'n')
    {
        return make_token(lexer, TOKEN_FUNCTION);
    }

    if (length == 3 &&
        lexer->start[0] == 'd' &&
        lexer->start[1] == 'e' &&
        lexer->start[2] == 'f')
    {
        return make_token(lexer, TOKEN_FUNCTION);
    }

    if (length == 3 &&
        lexer->start[0] == 'f' &&
        lexer->start[1] == 'u' &&
        lexer->start[2] == 'n')
    {
        return make_token(lexer, TOKEN_FUNCTION);
    }
    
    if (length == 6 &&
        lexer->start[0] == 'r' &&
        lexer->start[1] == 'e' &&
        lexer->start[2] == 't' &&
        lexer->start[3] == 'u' &&
        lexer->start[4] == 'r' &&
        lexer->start[5] == 'n')
    {
        return make_token(lexer, TOKEN_RETURN);
    }

    if (length == 5 &&
        lexer->start[0] == 'b' &&
        lexer->start[1] == 'r' &&
        lexer->start[2] == 'e' &&
        lexer->start[3] == 'a' &&
        lexer->start[4] == 'k')
    {
        return make_token(lexer, TOKEN_BREAK);
    }

    if (length == 8 &&
        lexer->start[0] == 'c' &&
        lexer->start[1] == 'o' &&
        lexer->start[2] == 'n' &&
        lexer->start[3] == 't' &&
        lexer->start[4] == 'i' &&
        lexer->start[5] == 'n' &&
        lexer->start[6] == 'u' &&
        lexer->start[7] == 'e')
    {
        return make_token(lexer, TOKEN_CONTINUE);
    }

    return make_token(lexer, TOKEN_IDENTIFIER);
}

static Token number_token(Lexer *lexer)
{
    while (isdigit((unsigned char)peek_char(lexer)))
    {
        advance_char(lexer);
    }

    return make_token(lexer, TOKEN_NUMBER);
}

void lexer_init(Lexer *lexer, const char *source)
{
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
    lexer->indent_stack[0] = 0;
    lexer->indent_count = 1;
    lexer->pending_dedents = 0;
    lexer->at_line_start = 1;
}

static int match_char(Lexer *lexer, char expected)
{
    if (is_at_end(lexer) || *lexer->current != expected)
    {
        return 0;
    }

    lexer->current++;
    return 1;
}

Token lexer_next(Lexer *lexer);

static Token handle_indentation(Lexer *lexer)
{
    int spaces = 0;

    while (peek_char(lexer) == ' ')
    {
        advance_char(lexer);
        spaces++;
    }

    if (peek_char(lexer) == '\n' || is_at_end(lexer))
    {
        lexer->at_line_start = 0;
        return lexer_next(lexer);
    }

    int current_indent =
        lexer->indent_stack[lexer->indent_count - 1];

    if (spaces > current_indent)
    {
        if (lexer->indent_count >= 64)
        {
            fprintf(
                stderr,
                "Syger lexer error: maximum indentation depth exceeded.\n"
            );
            exit(1);
        }

        lexer->indent_stack[lexer->indent_count] = spaces;
        lexer->indent_count++;
        lexer->at_line_start = 0;

        return make_token(lexer, TOKEN_INDENT);
    }

    if (spaces < current_indent)
    {
        int target_count = lexer->indent_count;

        while (
            target_count > 1 &&
            spaces < lexer->indent_stack[target_count - 1]
        )
        {
            target_count--;
        }

        if (spaces != lexer->indent_stack[target_count - 1])
        {
            fprintf(
                stderr,
                "Syger lexer error: inconsistent indentation.\n"
            );
            exit(1);
        }

        lexer->pending_dedents =
            lexer->indent_count - target_count;

        lexer->indent_count = target_count;
        lexer->at_line_start = 0;

        lexer->pending_dedents--;

        return make_token(lexer, TOKEN_DEDENT);
    }

    lexer->at_line_start = 0;
    return lexer_next(lexer);
}

Token lexer_next(Lexer *lexer)
{
    if (lexer->pending_dedents > 0)
    {
        lexer->start = lexer->current;
        lexer->pending_dedents--;
        return make_token(lexer, TOKEN_DEDENT);
    }

    if (lexer->at_line_start)
    {
        lexer->start = lexer->current;
        return handle_indentation(lexer);
    }

    skip_whitespace(lexer);

    if (lexer->at_line_start)
    {
        lexer->start = lexer->current;
        return handle_indentation(lexer);
    }

    lexer->start = lexer->current;

    if (is_at_end(lexer))
    {
        return make_token(lexer, TOKEN_EOF);
    }

    char c = advance_char(lexer);

    if (isalpha((unsigned char)c) || c == '_')
    {
        return identifier_token(lexer);
    }
    
    if (isdigit((unsigned char)c))
    {
        return number_token(lexer);
    }

    switch (c)
    {
        case '(':
            return make_token(lexer, TOKEN_LEFT_PAREN);

        case ')':
            return make_token(lexer, TOKEN_RIGHT_PAREN);
            
        case ',':
            return make_token(lexer, TOKEN_COMMA);

        case '"':
            return string_token(lexer);
            
        case '=':
            return make_token(
                lexer,
                match_char(lexer, '=') ?
                    TOKEN_EQUAL_EQUAL :
                    TOKEN_EQUAL
            );
            
        case '!':
            return make_token(
                lexer,
                match_char(lexer, '=') ?
                    TOKEN_BANG_EQUAL :
                    TOKEN_EOF
            );

        case '<':
            return make_token(
                lexer,
                match_char(lexer, '=') ?
                    TOKEN_LESS_EQUAL :
                    TOKEN_LESS
            );

        case '>':
            return make_token(
                lexer,
                match_char(lexer, '=') ?
                    TOKEN_GREATER_EQUAL :
                    TOKEN_GREATER
            );
            
        case '+':
            return make_token(lexer, TOKEN_PLUS);
            
        case '-':
            return make_token(lexer, TOKEN_MINUS);

        case '*':
            return make_token(lexer, TOKEN_STAR);

        case '/':
            return make_token(lexer, TOKEN_SLASH);

        case '%':
            return make_token(lexer, TOKEN_PERCENT);

        case '[':
            return make_token(lexer, TOKEN_LEFT_BRACKET);

        case ']':
            return make_token(lexer, TOKEN_RIGHT_BRACKET);
    }

    return make_token(lexer, TOKEN_EOF);
}
