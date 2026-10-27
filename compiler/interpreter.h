#ifndef SYGER_INTERPRETER_H
#define SYGER_INTERPRETER_H

#include "ast.h"

void interpreter_run(AstNode *program);
void interpreter_set_arguments(int count, char **arguments);

#endif
