#include "builtins.h"

char *builtin_str[] = {
    "cd",
    "exit",
    "echo",
    "export",
    "help",
    "clear",
    "fish",
    "history"};

const int builtin_str_count = sizeof(builtin_str) / sizeof(char *);

builtin_function builtin_func[] = {
    &builtin_cd,
    &builtin_exit,
    &builtin_echo,
    &builtin_export,
    &builtin_help,
    &builtin_clear,
    &builtin_fish,
    &builtin_history
};

int builtin_export(String *args)
{
    setenv(strtok(args[1].chars, "="), strstr(args[1].chars, "=") + 1, true);
    return HERMES_SUCCESS;
}

int builtin_exit(String *args)
{
    exit(EXIT_SUCCESS);
}

int builtin_fish(String *args)
{
    printf("  /---\\   /| \n");
    printf(" /@   -__/ |  \n");
    printf(" \\    /--/\\|\n");
    printf("  ---/        \n");
    fflush(stdout);
    return HERMES_SUCCESS;
}

int builtin_echo(String *args)
{
    int i = 1;
    while (args[i].chars != NULL)
    {
        printf("%s\n", args[i++].chars);
    }
    fflush(stdout);
    return HERMES_SUCCESS;
}

int builtin_cd(String *args)
{
    if (args[1].chars == NULL)
    {
        fprintf(stderr, "sh: expected argument to \"cd\"\n\r");
        fflush(NULL);
    }
    else if (chdir(args[1].chars) != 0)
    {
        perror(name);
    }
    return HERMES_SUCCESS;
}

int builtin_help(String *args)
{
    puts("Welcome to Hermes Shell\n"
         "The following commands are built in:");
    for (int i = 0; i < builtin_str_count; i++)
    {
        printf("\t%s\n", builtin_str[i]);
    }
    return HERMES_SUCCESS;
}

int builtin_clear(String *args)
{
    puts("\033[2J\033[H");
    return HERMES_SUCCESS;
}
