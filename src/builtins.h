#ifndef HERMES_BUILTINS_H
#define HERMES_BUILTINS_H

#include "globals.h"

int builtin_export(String *args);
int builtin_exit(String *args);
int builtin_echo(String *args);
int builtin_cd(String *args);
int builtin_help(String *args);
int builtin_clear(String *args);
int builtin_fish(String *args);
int builtin_history(String *args);
int append_to_history(const char *command);

#endif
