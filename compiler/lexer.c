#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diagnostics.h"

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

static Token make_token(Lexer *lexer, TokenType type);

_Noreturn static void lexer_error(
    Lexer *lexer,
    const char *code,
    const char *message,
    const char *help
)
{
    Token token;
    token.type = TOKEN_EOF;
    token.start = lexer->start;
    token.length = (int)(lexer->current - lexer->start);
    token.line = lexer->line;
    diagnostics_syntax_error(&token, code, message, help);
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
        else if (c == '#')
        {
            while (!is_at_end(lexer) && peek_char(lexer) != '\n')
            {
                advance_char(lexer);
            }
        }
        else if (c == '\n')
        {
            lexer->line++;
            advance_char(lexer);

            if (lexer->bracket_depth > 0)
            {
                continue;
            }

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
    int start_line = lexer->line;

    while (!is_at_end(lexer) && peek_char(lexer) != '"' && peek_char(lexer) != '\n')
    {
        if (peek_char(lexer) == '\\')
        {
            const char *escape_start = lexer->current;
            advance_char(lexer);
            char e = peek_char(lexer);

            if (e != 'n' && e != 't' && e != 'r' && e != '"' && e != '\\')
            {
                lexer->start = escape_start;
                lexer->current = escape_start + (e != '\0' && e != '\n' ? 2 : 1);
                lexer_error(lexer, "E1006", "unknown escape sequence in string", "supported escapes are \\n \\t \\r \\\" and \\\\ (a backslash)");
            }
        }

        advance_char(lexer);
    }

    if (is_at_end(lexer) || peek_char(lexer) == '\n')
    {
        lexer->current = lexer->start + 1;
        lexer->line = start_line;
        lexer_error(lexer, "E1003", "unterminated string", "close the string with a matching \"");
    }

    advance_char(lexer);

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

    if (length == 3 && memcmp(lexer->start, "for", 3) == 0)
    {
        return make_token(lexer, TOKEN_FOR);
    }

    if (length == 2 && memcmp(lexer->start, "in", 2) == 0)
    {
        return make_token(lexer, TOKEN_IN);
    }

    if (length == 6 && memcmp(lexer->start, "import", 6) == 0)
    {
        return make_token(lexer, TOKEN_IMPORT);
    }

    if (length == 2 && memcmp(lexer->start, "as", 2) == 0)
    {
        return make_token(lexer, TOKEN_AS);
    }

    return make_token(lexer, TOKEN_IDENTIFIER);
}

static Token number_token(Lexer *lexer)
{
    TokenType type = TOKEN_NUMBER;

    while (isdigit((unsigned char)peek_char(lexer)) || peek_char(lexer) == '_')
    {
        advance_char(lexer);
    }

    if (peek_char(lexer) == '.' && isdigit((unsigned char)lexer->current[1]))
    {
        type = TOKEN_FLOAT;
        advance_char(lexer);

        while (isdigit((unsigned char)peek_char(lexer)) || peek_char(lexer) == '_')
        {
            advance_char(lexer);
        }
    }

    if (peek_char(lexer) == 'e' || peek_char(lexer) == 'E')
    {
        const char *after = lexer->current + 1;

        if (*after == '+' || *after == '-')
        {
            after++;
        }

        if (isdigit((unsigned char)*after))
        {
            type = TOKEN_FLOAT;
            lexer->current = after;

            while (isdigit((unsigned char)peek_char(lexer)))
            {
                advance_char(lexer);
            }
        }
    }

    if (isalpha((unsigned char)peek_char(lexer)))
    {
        while (isalnum((unsigned char)peek_char(lexer)))
        {
            advance_char(lexer);
        }

        lexer_error(lexer, "E1007", "invalid number", "numbers look like 42, 3.14 or 1.5e3; names can't start with a digit");
    }

    return make_token(lexer, type);
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
    lexer->bracket_depth = 0;
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

    if (peek_char(lexer) == '#')
    {
        while (!is_at_end(lexer) && peek_char(lexer) != '\n')
        {
            advance_char(lexer);
        }
    }

    if (peek_char(lexer) == '\n' || peek_char(lexer) == '\r' || is_at_end(lexer))
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
            lexer_error(lexer, "E1001", "maximum indentation depth exceeded", "move deeply nested code into a function");
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
            lexer_error(lexer, "E1002", "inconsistent indentation", "this line's indentation does not match any enclosing block; use the same number of spaces as the block it belongs to");
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
            lexer->bracket_depth++;
            return make_token(lexer, TOKEN_LEFT_PAREN);

        case ')':
            if (lexer->bracket_depth > 0) lexer->bracket_depth--;
            return make_token(lexer, TOKEN_RIGHT_PAREN);

        case '{':
            lexer->bracket_depth++;
            return make_token(lexer, TOKEN_LEFT_BRACE);

        case '}':
            if (lexer->bracket_depth > 0) lexer->bracket_depth--;
            return make_token(lexer, TOKEN_RIGHT_BRACE);

        case ':':
            return make_token(lexer, TOKEN_COLON);
            
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
                    (lexer_error(lexer, "E1004", "unexpected character '!'", "use 'not' to negate a boolean, or '!=' to compare"), TOKEN_EOF)
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
            lexer->bracket_depth++;
            return make_token(lexer, TOKEN_LEFT_BRACKET);

        case ']':
            if (lexer->bracket_depth > 0) lexer->bracket_depth--;
            return make_token(lexer, TOKEN_RIGHT_BRACKET);
    }

    char message[64];
    if ((unsigned char)c >= 32 && (unsigned char)c < 127)
    {
        snprintf(message, sizeof(message), "unexpected character '%c'", c);
    }
    else
    {
        snprintf(message, sizeof(message), "unexpected character (byte 0x%02x)", (unsigned char)c);
    }
    lexer_error(lexer, "E1005", message, NULL);
}
