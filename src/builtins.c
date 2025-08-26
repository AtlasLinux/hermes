#include "builtins.h"

int builtin_export(String* args) {
    setenv(strtok(args[1].chars, "="), strstr(args[1].chars, "=") + 1, true);
    return HERMES_SUCCESS;
}

int builtin_exit(String* args) {
    exit(EXIT_SUCCESS);
}

int builtin_echo(String* args) {
    int i = 1;
    while (args[i].chars != NULL) {
        printf("%s\n", args[i++].chars);
    }
    fflush(stdout);
    return HERMES_SUCCESS;
}

int builtin_cd(String* args) {
    if (args[1].chars == NULL) {
        fprintf(stderr, "sh: expected argument to \"cd\"\n\r");
        fflush(NULL);
    } else if (chdir(args[1].chars) != 0) {
        perror(name);
    }
    return HERMES_SUCCESS;
}