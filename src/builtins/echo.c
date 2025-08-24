#include "echo.h"

int builtin_echo(String* args) {
    int i = 1;
    while (args[i].chars != NULL) {
        printf("%s\n", args[i++].chars);
    }
    fflush(stdout);
    return EXIT_SUCCESS;
}