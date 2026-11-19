#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy_string(const char *source) {
    size_t length = strlen(source);

    char *result = malloc(length + 1);

    if (result == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    memcpy(result, source, length + 1);

    return result;
}

/*
 * Name interning: every identifier in the AST points at one shared copy of
 * its text, so the interpreter can compare names by pointer. Interned names
 * live for the whole program and are never freed individually.
 */
typedef struct {
    char **slots;
    size_t capacity;
    size_t count;
} InternTable;

static InternTable intern_table = { NULL, 0, 0 };

static size_t hash_name(const char *name)
{
    size_t hash = 1469598103934665603u;

    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
    {
        hash = (hash ^ *p) * 1099511628211u;
    }

    return hash;
}

static void intern_grow(void)
{
    size_t capacity = intern_table.capacity ? intern_table.capacity * 2 : 256;
    char **slots = calloc(capacity, sizeof(char *));

    if (slots == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    for (size_t i = 0; i < intern_table.capacity; i++)
    {
        char *name = intern_table.slots[i];

        if (name == NULL)
        {
            continue;
        }

        size_t slot = hash_name(name) & (capacity - 1);

        while (slots[slot] != NULL)
        {
            slot = (slot + 1) & (capacity - 1);
        }

        slots[slot] = name;
    }

    free(intern_table.slots);
    intern_table.slots = slots;
    intern_table.capacity = capacity;
}

char *ast_intern(const char *name)
{
    if ((intern_table.count + 1) * 2 > intern_table.capacity)
    {
        intern_grow();
    }

    size_t slot = hash_name(name) & (intern_table.capacity - 1);

    while (intern_table.slots[slot] != NULL)
    {
        if (strcmp(intern_table.slots[slot], name) == 0)
        {
            return intern_table.slots[slot];
        }

        slot = (slot + 1) & (intern_table.capacity - 1);
    }

    intern_table.slots[slot] = copy_string(name);
    intern_table.count++;
    return intern_table.slots[slot];
}

int ast_current_line = 0;

static AstNode *ast_alloc_node(void)
{
    AstNode *node = malloc(sizeof(AstNode));

    if (node != NULL)
    {
        node->line = ast_current_line;
    }

    return node;
}

AstNode *ast_create_program(void) {
    AstNode *node = ast_alloc_node();

    if (node == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    node->type = AST_PROGRAM;
    node->program.statements = NULL;
    node->program.count = 0;

    return node;
}

void ast_program_add(AstNode *program, AstNode *statement) {
    int new_count = program->program.count + 1;

    AstNode **new_statements = realloc(
        program->program.statements,
        sizeof(AstNode *) * (size_t)new_count
    );

    if (new_statements == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    program->program.statements = new_statements;
    program->program.statements[new_count - 1] = statement;
    program->program.count = new_count;
}

AstNode *ast_create_string(const char *value) {
    AstNode *node = ast_alloc_node();

    if (node == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    node->type = AST_STRING;
    node->string.value = copy_string(value);

    return node;
}

AstNode *ast_create_integer(long value)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    node->type = AST_INTEGER;
    node->integer.value = value;

    return node;
}

AstNode *ast_create_boolean(int boolean)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    node->type = AST_BOOLEAN;
    node->boolean.value = boolean != 0;

    return node;
}

AstNode *ast_create_none(void)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    node->type = AST_NONE;

    return node;
}

AstNode *ast_create_call(
    const char *name,
    AstNode **arguments,
    int argument_count
)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    node->type = AST_CALL;
    node->call.kind = 0;

    node->call.name = ast_intern(name);

    node->call.arguments = arguments;
    node->call.argument_count = argument_count;

    return node;
}

AstNode *ast_create_variable_declaration(
    const char *name,
    AstNode *value
) {
    AstNode *node = ast_alloc_node();

    if (node == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    node->type = AST_VARIABLE_DECLARATION;
    node->variable_declaration.name = ast_intern(name);
    node->variable_declaration.value = value;

    return node;
}

AstNode *ast_create_variable_reference(const char *name) {
    AstNode *node = ast_alloc_node();

    if (node == NULL) {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    node->type = AST_VARIABLE_REFERENCE;
    node->variable_reference.name = ast_intern(name);

    return node;
}

AstNode *ast_create_binary(
    AstNode *left,
    TokenType operator,
    AstNode *right
)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    node->type = AST_BINARY;
    node->binary.left = left;
    node->binary.operator = operator;
    node->binary.right = right;

    return node;
}

AstNode *ast_create_unary(
    TokenType operator,
    AstNode *operand
)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    node->type = AST_UNARY;
    node->unary.operator = operator;
    node->unary.operand = operand;

    return node;
}

AstNode *ast_create_if(
    AstNode *condition,
    AstNode *body,
    AstNode *else_body
)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    node->type = AST_IF;
    node->if_statement.condition = condition;
    node->if_statement.body = body;
    node->if_statement.else_body = else_body;

    return node;
}

AstNode *ast_create_while(
    AstNode *condition,
    AstNode *body
)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    node->type = AST_WHILE;
    node->while_statement.condition = condition;
    node->while_statement.body = body;

    return node;
}

AstNode *ast_create_function(
    const char *name,
    char **parameters,
    int parameter_count,
    AstNode *body
)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    node->type = AST_FUNCTION;
    node->function.name = ast_intern(name);

    for (int i = 0; i < parameter_count; i++)
    {
        char *interned = ast_intern(parameters[i]);
        free(parameters[i]);
        parameters[i] = interned;
    }

    node->function.parameters = parameters;
    node->function.parameter_count = parameter_count;
    node->function.body = body;

    return node;
}

AstNode *ast_create_return(AstNode *value)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    node->type = AST_RETURN;
    node->return_statement.value = value;

    return node;
}

AstNode *ast_create_array(AstNode **elements, int count)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    node->type = AST_ARRAY;
    node->array.elements = elements;
    node->array.count = count;

    return node;
}

AstNode *ast_create_index(AstNode *array, AstNode *index)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    node->type = AST_INDEX;
    node->index.array = array;
    node->index.index = index;

    return node;
}

AstNode *ast_create_index_assignment(
    AstNode *array,
    AstNode *index,
    AstNode *value
)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    node->type = AST_INDEX_ASSIGNMENT;
    node->index_assignment.array = array;
    node->index_assignment.index = index;
    node->index_assignment.value = value;

    return node;
}

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

AstNode *ast_create_break(void)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    node->type = AST_BREAK;

    return node;
}

AstNode *ast_create_continue(void)
{
    AstNode *node = ast_alloc_node();

    if (node == NULL)
    {
        fprintf(stderr, "Syger: out of memory.\n");
        exit(1);
    }

    node->type = AST_CONTINUE;

    return node;
}

void ast_print(AstNode *node, int indent) {
    if (node == NULL) {
        return;
    }

    print_indent(indent);

    switch (node->type) {
        case AST_PROGRAM:
            printf("Program\n");

            for (int i = 0; i < node->program.count; i++) {
                ast_print(node->program.statements[i], indent + 1);
            }
            break;

        case AST_STRING:
            printf("StringLiteral: \"%s\"\n", node->string.value);
            break;
            
        case AST_INTEGER:
            printf("IntegerLiteral: %ld\n", node->integer.value);
            break;
            
        case AST_BOOLEAN:
            printf(
                "Boolean: %s\n",
                node->boolean.value ? "true" : "false"
            );
            break;

        case AST_NONE:
            printf("None\n");
            break;

        case AST_CALL:
            print_indent(indent);
            printf("CALL %s\n", node->call.name);

            for (int i = 0; i < node->call.argument_count; i++)
            {
                ast_print(node->call.arguments[i], indent + 2);
            }

            break;
            
        case AST_VARIABLE_DECLARATION:
            printf("VariableDeclaration: %s\n",
                   node->variable_declaration.name);

            print_indent(indent + 1);
            printf("value:\n");

            ast_print(node->variable_declaration.value, indent + 2);
            break;

        case AST_VARIABLE_REFERENCE:
            printf("VariableReference: %s\n",
                   node->variable_reference.name);
            break;
            
        case AST_BINARY:
            printf("BinaryExpression: ");

            switch (node->binary.operator)
            {
                case TOKEN_PLUS:
                    printf("+\n");
                    break;

                case TOKEN_MINUS:
                    printf("-\n");
                    break;

                case TOKEN_STAR:
                    printf("*\n");
                    break;

                case TOKEN_SLASH:
                    printf("/\n");
                    break;

                case TOKEN_PERCENT:
                    printf("%%\n");
                    break;

                case TOKEN_EQUAL_EQUAL:
                    printf("==\n");
                    break;

                case TOKEN_BANG_EQUAL:
                    printf("!=\n");
                    break;

                case TOKEN_LESS:
                    printf("<\n");
                    break;

                case TOKEN_LESS_EQUAL:
                    printf("<=\n");
                    break;

                case TOKEN_GREATER:
                    printf(">\n");
                    break;

                case TOKEN_GREATER_EQUAL:
                    printf(">=\n");
                    break;

                case TOKEN_AND:
                    printf("and\n");
                    break;

                case TOKEN_OR:
                    printf("or\n");
                    break;

                default:
                    printf("unknown\n");
                    break;
            }

            ast_print(node->binary.left, indent + 1);
            ast_print(node->binary.right, indent + 1);
            break;

        case AST_UNARY:
            printf("UnaryExpression: ");

            switch (node->unary.operator)
            {
                case TOKEN_NOT:
                    printf("not\n");
                    break;

                default:
                    printf("unknown\n");
                    break;
            }

            ast_print(
                node->unary.operand,
                indent + 1
            );
            break;

        case AST_ARRAY:
            printf("ArrayLiteral\n");

            for (int i = 0; i < node->array.count; i++)
            {
                ast_print(node->array.elements[i], indent + 1);
            }
            break;

        case AST_INDEX:
            printf("IndexExpression\n");
            ast_print(node->index.array, indent + 1);
            ast_print(node->index.index, indent + 1);
            break;

        case AST_INDEX_ASSIGNMENT:
            printf("IndexAssignment\n");
            ast_print(node->index_assignment.array, indent + 1);
            ast_print(node->index_assignment.index, indent + 1);
            ast_print(node->index_assignment.value, indent + 1);
            break;

        case AST_IF:
            printf("IfStatement\n");

            ast_print(
                node->if_statement.condition,
                indent + 1
            );

            ast_print(
                node->if_statement.body,
                indent + 1
            );

            if (node->if_statement.else_body != NULL)
            {
                ast_print(
                    node->if_statement.else_body,
                    indent + 1
                );
            }

            break;
            
        case AST_WHILE:
            printf("WhileStatement\n");

            ast_print(
                node->while_statement.condition,
                indent + 1
            );

            ast_print(
                node->while_statement.body,
                indent + 1
            );

            break;
            
        case AST_FUNCTION:
            printf("FunctionDeclaration: %s\n", node->function.name);
            ast_print(node->function.body, indent + 1);
            break;
            
        case AST_RETURN:
            print_indent(indent);
            printf("RETURN\n");
            ast_print(node->return_statement.value, indent + 2);
            break;

        case AST_BREAK:
            print_indent(indent);
            printf("BREAK\n");
            break;

        case AST_CONTINUE:
            print_indent(indent);
            printf("CONTINUE\n");
            break;
    }
}

void ast_free(AstNode *node)
{
    if (node == NULL)
    {
        return;
    }

    switch (node->type)
    {
        case AST_PROGRAM:
            for (int i = 0; i < node->program.count; i++)
            {
                ast_free(node->program.statements[i]);
            }

            free(node->program.statements);
            break;

        case AST_STRING:
            free(node->string.value);
            break;

        case AST_INTEGER:
            break;

        case AST_BOOLEAN:
            break;

        case AST_NONE:
            break;

        case AST_CALL:
            for (int i = 0; i < node->call.argument_count; i++)
            {
                ast_free(node->call.arguments[i]);
            }

            free(node->call.arguments);
            break;

        case AST_VARIABLE_DECLARATION:
            ast_free(node->variable_declaration.value);
            break;

        case AST_VARIABLE_REFERENCE:
            break;

        case AST_BINARY:
            ast_free(node->binary.left);
            ast_free(node->binary.right);
            break;

        case AST_UNARY:
            ast_free(node->unary.operand);
            break;

        case AST_IF:
            ast_free(node->if_statement.condition);
            ast_free(node->if_statement.body);
            ast_free(node->if_statement.else_body);
            break;

        case AST_WHILE:
            ast_free(node->while_statement.condition);
            ast_free(node->while_statement.body);
            break;

        case AST_FUNCTION:
            free(node->function.parameters);
            ast_free(node->function.body);
            break;

        case AST_ARRAY:
            for (int i = 0; i < node->array.count; i++)
            {
                ast_free(node->array.elements[i]);
            }
            free(node->array.elements);
            break;

        case AST_INDEX:
            ast_free(node->index.array);
            ast_free(node->index.index);
            break;

        case AST_INDEX_ASSIGNMENT:
            ast_free(node->index_assignment.array);
            ast_free(node->index_assignment.index);
            ast_free(node->index_assignment.value);
            break;
            
        case AST_RETURN:
            ast_free(node->return_statement.value);
            break;

        case AST_BREAK:
            break;

        case AST_CONTINUE:
            break;
    }

    free(node);
}
