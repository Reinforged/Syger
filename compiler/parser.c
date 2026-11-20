#include "parser.h"
#include "diagnostics.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void advance(Parser *parser)
{
    parser->previous = parser->current;
    parser->current = lexer_next(parser->lexer);
    ast_current_line = parser->previous.line;
}

/* Statements are tagged with the line they start on, even when (like if
   and while) the node is only created after parsing its whole body. */
static void add_statement(AstNode *block, int line, AstNode *statement)
{
    statement->line = line;
    ast_program_add(block, statement);
}

static void parser_error(Parser *parser, const char *message)
{
    Token at = parser->current;

    /* "Expected ..." at a line break or end of file reads better pointing
       just past the last token on the previous line. */
    if (parser->previous.start != NULL &&
        (at.type == TOKEN_EOF || at.type == TOKEN_INDENT || at.type == TOKEN_DEDENT ||
         at.line > parser->previous.line))
    {
        at = parser->previous;
        at.start = parser->previous.start + parser->previous.length;
        at.length = 1;
    }

    diagnostics_syntax_error(&at, "E2001", message, NULL);
}

static void consume(
    Parser *parser,
    TokenType type,
    const char *message
)
{
    if (parser->current.type == type)
    {
        advance(parser);
        return;
    }

    parser_error(parser, message);
}

static char *token_to_string(Token token)
{
    char *text = malloc((size_t)token.length + 1);

    if (text == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    memcpy(text, token.start, (size_t)token.length);
    text[token.length] = '\0';

    return text;
}

static AstNode *parse_string(Parser *parser)
{
    int length = parser->previous.length - 2;

    if (length < 0)
    {
        parser_error(parser, "Invalid string literal.");
    }

    char *value = malloc((size_t)length + 1);

    if (value == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    /* Decode escapes; the lexer has already rejected unknown ones. */
    const char *raw = parser->previous.start + 1;
    int out = 0;

    for (int i = 0; i < length; i++)
    {
        char c = raw[i];

        if (c == '\\' && i + 1 < length)
        {
            char e = raw[++i];
            c = e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e;
        }

        value[out++] = c;
    }

    value[out] = '\0';

    AstNode *node = ast_create_string(value);

    free(value);

    return node;
}

/* Number text without the _ separators (1_000_000). */
static char *number_text(Token token)
{
    char *text = token_to_string(token);
    int out = 0;

    for (int i = 0; text[i] != '\0'; i++)
    {
        if (text[i] != '_')
        {
            text[out++] = text[i];
        }
    }

    text[out] = '\0';
    return text;
}

static AstNode *parse_integer(Parser *parser)
{
    char *text = number_text(parser->previous);

    long value = strtol(text, NULL, 10);

    free(text);

    return ast_create_integer(value);
}

static AstNode *parse_float(Parser *parser)
{
    char *text = number_text(parser->previous);

    double value = strtod(text, NULL);

    free(text);

    return ast_create_float(value);
}

static AstNode *parse_call(Parser *parser);
static AstNode *parse_expression(Parser *parser);
static AstNode *parse_index(Parser *parser, AstNode *array);

static AstNode *parse_array(Parser *parser)
{
    AstNode **elements = NULL;
    int count = 0;

    while (parser->current.type != TOKEN_RIGHT_BRACKET)
    {
        {
            AstNode *element = parse_expression(parser);

            AstNode **new_elements = realloc(
                elements,
                sizeof(AstNode *) * (size_t)(count + 1)
            );

            if (new_elements == NULL)
            {
                fprintf(stderr, "Syger: out of memory.\n");
                exit(1);
            }

            elements = new_elements;
            elements[count] = element;
            count++;

            if (parser->current.type != TOKEN_COMMA)
            {
                break;
            }

            advance(parser);
        }
    }

    consume(
        parser,
        TOKEN_RIGHT_BRACKET,
        "Expected ']' after array elements."
    );

    return ast_create_array(elements, count);
}

static void *grow_array(void *items, int count, size_t size)
{
    void *grown = realloc(items, size * (size_t)(count + 1));

    if (grown == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    return grown;
}

/* {key: value, ...}, possibly spread over several lines, trailing comma allowed. */
static AstNode *parse_map(Parser *parser)
{
    AstNode **keys = NULL;
    AstNode **values = NULL;
    int count = 0;

    while (parser->current.type != TOKEN_RIGHT_BRACE)
    {
        AstNode *key = parse_expression(parser);
        consume(parser, TOKEN_COLON, "Expected ':' after map key.");
        AstNode *value = parse_expression(parser);

        keys = grow_array(keys, count, sizeof(AstNode *));
        values = grow_array(values, count, sizeof(AstNode *));
        keys[count] = key;
        values[count] = value;
        count++;

        if (parser->current.type != TOKEN_COMMA)
        {
            break;
        }

        advance(parser);
    }

    consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after map entries.");

    return ast_create_map(keys, values, count);
}

static AstNode *parse_postfix(Parser *parser, AstNode *node)
{
    while (parser->current.type == TOKEN_LEFT_BRACKET)
    {
        advance(parser);
        node = parse_index(parser, node);
    }

    return node;
}

static AstNode *parse_primary(Parser *parser)
{
    AstNode *node = NULL;

    if (parser->current.type == TOKEN_LEFT_PAREN)
    {
        advance(parser);

        node = parse_expression(parser);

        consume(
            parser,
            TOKEN_RIGHT_PAREN,
            "Expected ')' after expression."
        );

        return parse_postfix(parser, node);
    }

    if (parser->current.type == TOKEN_LEFT_BRACKET)
    {
        advance(parser);
        node = parse_array(parser);
        return parse_postfix(parser, node);
    }

    if (parser->current.type == TOKEN_STRING)
    {
        advance(parser);
        node = parse_string(parser);
        return parse_postfix(parser, node);
    }

    if (parser->current.type == TOKEN_NUMBER)
    {
        advance(parser);
        node = parse_integer(parser);
        return parse_postfix(parser, node);
    }

    if (parser->current.type == TOKEN_FLOAT)
    {
        advance(parser);
        node = parse_float(parser);
        return parse_postfix(parser, node);
    }

    if (parser->current.type == TOKEN_LEFT_BRACE)
    {
        advance(parser);
        node = parse_map(parser);
        return parse_postfix(parser, node);
    }

    if (parser->current.type == TOKEN_TRUE)
    {
        advance(parser);
        node = ast_create_boolean(1);
        return parse_postfix(parser, node);
    }

    if (parser->current.type == TOKEN_FALSE)
    {
        advance(parser);
        node = ast_create_boolean(0);
        return parse_postfix(parser, node);
    }

    if (parser->current.type == TOKEN_NONE)
    {
        advance(parser);
        node = ast_create_none();
        return parse_postfix(parser, node);
    }

    if (parser->current.type == TOKEN_MINUS)
    {
        TokenType operator = parser->current.type;
        advance(parser);

        node = ast_create_unary(
            operator,
            parse_primary(parser)
        );

        return parse_postfix(parser, node);
    }

    if (parser->current.type == TOKEN_IDENTIFIER)
    {
        advance(parser);

        if (parser->current.type == TOKEN_LEFT_PAREN)
        {
            node = parse_call(parser);
        }
        else
        {
            char *name = token_to_string(parser->previous);
            node = ast_create_variable_reference(name);
            free(name);
        }

        return parse_postfix(parser, node);
    }

    parser_error(parser, "Expected an expression.");
    return NULL;
}

static AstNode *parse_index(Parser *parser, AstNode *array)
{
    AstNode *index = parse_expression(parser);

    consume(
        parser,
        TOKEN_RIGHT_BRACKET,
        "Expected ']' after array index."
    );

    return ast_create_index(array, index);
}

static AstNode *parse_multiplication(Parser *parser)
{
    AstNode *left = parse_primary(parser);

    while (
        parser->current.type == TOKEN_STAR ||
        parser->current.type == TOKEN_SLASH ||
        parser->current.type == TOKEN_PERCENT
    )
    {
        TokenType operator = parser->current.type;
        advance(parser);

        AstNode *right = parse_primary(parser);

        left = ast_create_binary(left, operator, right);
    }

    return left;
}

static AstNode *parse_addition(Parser *parser)
{
    AstNode *left = parse_multiplication(parser);

    while (
        parser->current.type == TOKEN_PLUS ||
        parser->current.type == TOKEN_MINUS
    )
    {
        TokenType operator = parser->current.type;
        advance(parser);

        AstNode *right = parse_multiplication(parser);

        left = ast_create_binary(left, operator, right);
    }

    return left;
}

static AstNode *parse_comparison(Parser *parser)
{
    AstNode *left = parse_addition(parser);

    while (
        parser->current.type == TOKEN_EQUAL_EQUAL ||
        parser->current.type == TOKEN_BANG_EQUAL ||
        parser->current.type == TOKEN_LESS ||
        parser->current.type == TOKEN_LESS_EQUAL ||
        parser->current.type == TOKEN_GREATER ||
        parser->current.type == TOKEN_GREATER_EQUAL
    )
    {
        TokenType operator = parser->current.type;
        advance(parser);

        AstNode *right = parse_addition(parser);

        left = ast_create_binary(left, operator, right);
    }

    return left;
}

static AstNode *parse_not(Parser *parser)
{
    if (parser->current.type == TOKEN_NOT)
    {
        TokenType operator = parser->current.type;
        advance(parser);

        AstNode *operand = parse_not(parser);

        return ast_create_unary(
            operator,
            operand
        );
    }

    return parse_comparison(parser);
}

static AstNode *parse_and(Parser *parser)
{
    AstNode *left = parse_not(parser);

    while (parser->current.type == TOKEN_AND)
    {
        TokenType operator = parser->current.type;
        advance(parser);

        AstNode *right = parse_not(parser);

        left = ast_create_binary(
            left,
            operator,
            right
        );
    }

    return left;
}

static AstNode *parse_or(Parser *parser)
{
    AstNode *left = parse_and(parser);

    while (parser->current.type == TOKEN_OR)
    {
        TokenType operator = parser->current.type;
        advance(parser);

        AstNode *right = parse_and(parser);

        left = ast_create_binary(
            left,
            operator,
            right
        );
    }

    return left;
}

static AstNode *parse_expression(Parser *parser)
{
    return parse_or(parser);
}

static AstNode *parse_variable_declaration(Parser *parser)
{
    Token name_token = parser->previous;
    char *name = token_to_string(name_token);

    consume(
        parser,
        TOKEN_EQUAL,
        "Expected '=' after variable name."
    );

    AstNode *value = parse_expression(parser);

    AstNode *declaration =
        ast_create_variable_declaration(name, value);

    free(name);

    return declaration;
}

static AstNode *parse_index_assignment(Parser *parser, AstNode *array)
{
    while (parser->current.type == TOKEN_LEFT_BRACKET)
    {
        advance(parser);
        array = parse_index(parser, array);
    }

    consume(
        parser,
        TOKEN_EQUAL,
        "Expected '=' after array index."
    );

    AstNode *value = parse_expression(parser);

    if (array->type != AST_INDEX)
    {
        ast_free(array);
        ast_free(value);
        parser_error(parser, "Expected an array index assignment.");
    }

    AstNode *assignment = ast_create_index_assignment(
        array->index.array,
        array->index.index,
        value
    );

    free(array);

    return assignment;
}

static AstNode *parse_call(Parser *parser)
{
    char *name = token_to_string(parser->previous);

    consume(
        parser,
        TOKEN_LEFT_PAREN,
        "Expected '(' after function name."
    );

    AstNode **arguments = NULL;
    int argument_count = 0;

    while (parser->current.type != TOKEN_RIGHT_PAREN)
    {
        {
            AstNode *argument = parse_expression(parser);

            AstNode **new_arguments = realloc(
                arguments,
                sizeof(AstNode *) * (argument_count + 1)
            );

            if (new_arguments == NULL)
            {
                fprintf(stderr, "Out of memory.\n");
                exit(1);
            }

            arguments = new_arguments;
            arguments[argument_count] = argument;
            argument_count++;

            if (parser->current.type != TOKEN_COMMA)
            {
                break;
            }

            advance(parser);
        }
    }

    consume(
        parser,
        TOKEN_RIGHT_PAREN,
        "Expected ')' after function arguments."
    );

    AstNode *node = ast_create_call(
        name,
        arguments,
        argument_count
    );

    free(name);

    return node;
}



void parser_init(Parser *parser, Lexer *lexer)
{
    parser->lexer = lexer;

    parser->current.type = TOKEN_EOF;
    parser->previous.type = TOKEN_EOF;
    parser->previous.start = NULL;
    parser->previous.length = 0;
    parser->previous.line = 0;


    advance(parser);
}

static AstNode *parse_if(Parser *parser);
static AstNode *parse_or(Parser *parser);
static AstNode *parse_and(Parser *parser);
static AstNode *parse_not(Parser *parser);
static AstNode *parse_if(Parser *parser);
static AstNode *parse_while(Parser *parser);
static AstNode *parse_function(Parser *parser);
static AstNode *parse_return(Parser *parser);
static AstNode *parse_break(void);
static AstNode *parse_continue(void);

static AstNode *parse_block(Parser *parser);

/* for name in iterable */
static AstNode *parse_for(Parser *parser)
{
    if (parser->current.type != TOKEN_IDENTIFIER)
    {
        parser_error(parser, "Expected a variable name after 'for'.");
    }

    advance(parser);
    char *name = token_to_string(parser->previous);

    consume(parser, TOKEN_IN, "Expected 'in' after the for loop variable.");

    AstNode *iterable = parse_expression(parser);
    AstNode *body = parse_block(parser);
    AstNode *node = ast_create_for(name, iterable, body);

    free(name);
    return node;
}

/* import "path" [as name]  ->  name = <import "path"> */
static AstNode *parse_import(Parser *parser)
{
    if (parser->current.type != TOKEN_STRING)
    {
        parser_error(parser, "Expected a file path in quotes after 'import'.");
    }

    advance(parser);
    AstNode *path_node = parse_string(parser);
    char *path = path_node->string.value;
    char *name = NULL;

    if (parser->current.type == TOKEN_AS)
    {
        advance(parser);

        if (parser->current.type != TOKEN_IDENTIFIER)
        {
            parser_error(parser, "Expected a name after 'as'.");
        }

        advance(parser);
        name = token_to_string(parser->previous);
    }
    else
    {
        /* Default name: the file name without directory or extension. */
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        size_t length = strcspn(base, ".");
        int valid = length > 0 && !isdigit((unsigned char)base[0]);

        for (size_t i = 0; i < length; i++)
        {
            if (!isalnum((unsigned char)base[i]) && base[i] != '_')
            {
                valid = 0;
            }
        }

        if (!valid)
        {
            parser_error(parser, "This file name can't be used as a variable name; add 'as name'.");
        }

        name = malloc(length + 1);

        if (name == NULL)
        {
            fprintf(stderr, "Syger: out of memory.\n");
            exit(1);
        }

        memcpy(name, base, length);
        name[length] = '\0';
    }

    AstNode *node = ast_create_variable_declaration(name, ast_create_import(path));
    free(name);
    ast_free(path_node);
    return node;
}

static AstNode *parse_block(Parser *parser)
{
    if (parser->current.type != TOKEN_INDENT)
    {
        parser_error(
            parser,
            "Expected an indented block."
        );
    }

    advance(parser);

    AstNode *block = ast_create_program();

    while (
        parser->current.type != TOKEN_DEDENT &&
        parser->current.type != TOKEN_EOF
    )
    {
        int line = parser->current.line;
        
        if (parser->current.type == TOKEN_RETURN)
        {
            advance(parser);
            add_statement(block, line, parse_return(parser));
            continue;
        }

        if (parser->current.type == TOKEN_BREAK)
        {
            advance(parser);
            add_statement(block, line, parse_break());
            continue;
        }

        if (parser->current.type == TOKEN_CONTINUE)
        {
            advance(parser);
            add_statement(block, line, parse_continue());
            continue;
        }
        
        if (parser->current.type == TOKEN_FUNCTION)
        {
            advance(parser);
            add_statement(block, line, parse_function(parser));
            continue;
        }
        
        if (parser->current.type == TOKEN_IF)
        {
            advance(parser);
            add_statement(block, line, parse_if(parser));
            continue;
        }
        
        if (parser->current.type == TOKEN_WHILE)
        {
            advance(parser);
            add_statement(block, line, parse_while(parser));
            continue;
        }

        if (parser->current.type == TOKEN_FOR)
        {
            advance(parser);
            add_statement(block, line, parse_for(parser));
            continue;
        }

        if (parser->current.type == TOKEN_IMPORT)
        {
            advance(parser);
            add_statement(block, line, parse_import(parser));
            continue;
        }

        if (parser->current.type != TOKEN_IDENTIFIER)
        {
            parser_error(parser, "Expected a statement.");
        }

        advance(parser);

        AstNode *statement = NULL;

        if (parser->current.type == TOKEN_EQUAL)
        {
            statement = parse_variable_declaration(parser);
        }
        else if (parser->current.type == TOKEN_LEFT_PAREN)
        {
            statement = parse_call(parser);
        }
        else if (parser->current.type == TOKEN_LEFT_BRACKET)
        {
            char *name = token_to_string(parser->previous);
            AstNode *array = ast_create_variable_reference(name);
            free(name);
            statement = parse_index_assignment(parser, array);
        }
        else
        {
            parser_error(
                parser,
                "Expected '=' or '(' after identifier."
            );
        }

        add_statement(block, line, statement);
    }

    if (parser->current.type == TOKEN_DEDENT)
    {
        advance(parser);
    }

    return block;
}

static AstNode *parse_if(Parser *parser)
{
    AstNode *condition = parse_expression(parser);

    AstNode *body = parse_block(parser);

    AstNode *else_body = NULL;

    if (parser->current.type == TOKEN_ELSE)
    {
        advance(parser);

        if (parser->current.type == TOKEN_IF)
        {
            advance(parser);
            else_body = parse_if(parser);
        }
        else
        {
            else_body = parse_block(parser);
        }
    }
    else if (parser->current.type == TOKEN_ELIF)
    {
        advance(parser);
        else_body = parse_if(parser);
    }

    return ast_create_if(
        condition,
        body,
        else_body
    );
}

static AstNode *parse_while(Parser *parser)
{
    AstNode *condition = parse_expression(parser);
    AstNode *body = parse_block(parser);

    return ast_create_while(
        condition,
        body
    );
}

static AstNode *parse_function(Parser *parser)
{
    if (parser->current.type != TOKEN_IDENTIFIER)
    {
        parser_error(parser, "Expected function name.");
    }

    advance(parser);

    char *name = token_to_string(parser->previous);

    char **parameters = NULL;
    int parameter_count = 0;

    if (parser->current.type == TOKEN_LEFT_PAREN)
    {
        advance(parser);

        if (parser->current.type != TOKEN_RIGHT_PAREN)
        {
        while (1)
        {
            if (parser->current.type != TOKEN_IDENTIFIER)
            {
                parser_error(parser, "Expected parameter name.");
            }

            char **new_parameters = realloc(
                parameters,
                sizeof(char *) * (parameter_count + 1)
            );

            if (new_parameters == NULL)
            {
                fprintf(stderr, "Out of memory.\n");
                exit(1);
            }

            parameters = new_parameters;
            parameters[parameter_count] =
                token_to_string(parser->current);

            parameter_count++;
            advance(parser);

            if (parser->current.type != TOKEN_COMMA)
            {
                break;
            }

            advance(parser);
            }
        }

        consume(
            parser,
            TOKEN_RIGHT_PAREN,
            "Expected ')' after function parameters."
        );
    }

    AstNode *body = parse_block(parser);

    AstNode *function = ast_create_function(
        name,
        parameters,
        parameter_count,
        body
    );

    free(name);

    return function;
}

static AstNode *parse_return(Parser *parser)
{
    AstNode *value = parse_expression(parser);

    return ast_create_return(value);
}

static AstNode *parse_break(void)
{
    return ast_create_break();
}

static AstNode *parse_continue(void)
{
    return ast_create_continue();
}


AstNode *parser_parse(Parser *parser)
{
    AstNode *program = ast_create_program();

    while (parser->current.type != TOKEN_EOF)
    {
        int line = parser->current.line;
        
        if (parser->current.type == TOKEN_RETURN)
        {
            advance(parser);
            add_statement(program, line, parse_return(parser));
            continue;
        }

        if (parser->current.type == TOKEN_BREAK)
        {
            advance(parser);
            add_statement(program, line, parse_break());
            continue;
        }

        if (parser->current.type == TOKEN_CONTINUE)
        {
            advance(parser);
            add_statement(program, line, parse_continue());
            continue;
        }
        
        if (parser->current.type == TOKEN_FUNCTION)
        {
            advance(parser);
            add_statement(program, line, parse_function(parser));
            continue;
        }
        
        if (parser->current.type == TOKEN_IF)
        {
            advance(parser);
            add_statement(program, line, parse_if(parser));
            continue;
        }
        
        if (parser->current.type == TOKEN_WHILE)
        {
            advance(parser);
            add_statement(program, line, parse_while(parser));
            continue;
        }

        if (parser->current.type == TOKEN_FOR)
        {
            advance(parser);
            add_statement(program, line, parse_for(parser));
            continue;
        }

        if (parser->current.type == TOKEN_IMPORT)
        {
            advance(parser);
            add_statement(program, line, parse_import(parser));
            continue;
        }
    

        if (parser->current.type != TOKEN_IDENTIFIER)
        {
            parser_error(parser, "Expected a statement.");
        }

        advance(parser);

        AstNode *statement = NULL;

        if (parser->current.type == TOKEN_EQUAL)
        {
            statement = parse_variable_declaration(parser);
        }
        else if (parser->current.type == TOKEN_LEFT_PAREN)
        {
            statement = parse_call(parser);
        }
        else if (parser->current.type == TOKEN_LEFT_BRACKET)
        {
            char *name = token_to_string(parser->previous);
            AstNode *array = ast_create_variable_reference(name);
            free(name);
            statement = parse_index_assignment(parser, array);
        }
        else
        {
            parser_error(
                parser,
                "Expected '=' or '(' after identifier."
            );
        }

        add_statement(program, line, statement);
    }

    return program;
}
