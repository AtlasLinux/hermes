#include "cd.h"

int builtin_cd(String* args) {
    if (args[1].chars == NULL) {
        fprintf(stderr, "sh: expected argument to \"cd\"\n\r");
        fflush(NULL);
    } else if (chdir(args[1].chars) != 0) {
        perror(name);
    }
    return HERMES_SUCCESS;
}