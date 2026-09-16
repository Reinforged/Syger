#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void advance(Parser *parser)
{
    parser->previous = parser->current;
    parser->current = lexer_next(parser->lexer);
}

static void parser_error(Parser *parser, const char *message)
{
    fprintf(
        stderr,
        "Syger parser error on line %d: %s\n",
        parser->current.line,
        message
    );

    exit(1);
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

    memcpy(
        value,
        parser->previous.start + 1,
        (size_t)length
    );

    value[length] = '\0';

    AstNode *node = ast_create_string(value);

    free(value);

    return node;
}

static AstNode *parse_integer(Parser *parser)
{
    char *text = token_to_string(parser->previous);

    long value = strtol(text, NULL, 10);

    free(text);

    return ast_create_integer(value);
}

static AstNode *parse_call(Parser *parser);
static AstNode *parse_expression(Parser *parser);
static AstNode *parse_index(Parser *parser, AstNode *array);

static AstNode *parse_array(Parser *parser)
{
    AstNode **elements = NULL;
    int count = 0;

    if (parser->current.type != TOKEN_RIGHT_BRACKET)
    {
        while (1)
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

    if (parser->current.type != TOKEN_RIGHT_PAREN)
    {
        while (1)
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
        
        if (parser->current.type == TOKEN_RETURN)
        {
            advance(parser);
            ast_program_add(block, parse_return(parser));
            continue;
        }

        if (parser->current.type == TOKEN_BREAK)
        {
            advance(parser);
            ast_program_add(block, parse_break());
            continue;
        }

        if (parser->current.type == TOKEN_CONTINUE)
        {
            advance(parser);
            ast_program_add(block, parse_continue());
            continue;
        }
        
        if (parser->current.type == TOKEN_FUNCTION)
        {
            advance(parser);
            ast_program_add(block, parse_function(parser));
            continue;
        }
        
        if (parser->current.type == TOKEN_IF)
        {
            advance(parser);
            ast_program_add(block, parse_if(parser));
            continue;
        }
        
        if (parser->current.type == TOKEN_WHILE)
        {
            advance(parser);
            ast_program_add(block, parse_while(parser));
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

        ast_program_add(block, statement);
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
        
        if (parser->current.type == TOKEN_RETURN)
        {
            advance(parser);
            ast_program_add(program, parse_return(parser));
            continue;
        }

        if (parser->current.type == TOKEN_BREAK)
        {
            advance(parser);
            ast_program_add(program, parse_break());
            continue;
        }

        if (parser->current.type == TOKEN_CONTINUE)
        {
            advance(parser);
            ast_program_add(program, parse_continue());
            continue;
        }
        
        if (parser->current.type == TOKEN_FUNCTION)
        {
            advance(parser);
            ast_program_add(program, parse_function(parser));
            continue;
        }
        
        if (parser->current.type == TOKEN_IF)
        {
            advance(parser);
            ast_program_add(program, parse_if(parser));
            continue;
        }
        
        if (parser->current.type == TOKEN_WHILE)
        {
            advance(parser);
            ast_program_add(program, parse_while(parser));
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

        ast_program_add(program, statement);
    }

    return program;
}
