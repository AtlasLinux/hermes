#ifndef HERMES_GLOBAL_H
#define HERMES_GLOBAL_H

#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <dirent.h>
#include <stdbool.h>
#include <errno.h>

#define PARSE_TOKEN_DELIM " \t"
#define HERMES_SUCCESS 1
#define HERMES_FAILURE 0
#define CONFIG_FILE "/home/gingrspacecadet/hermes.conf"

extern const char *name;

typedef const enum sizes {
    BUFFER_MAX_SIZE = 1024,
    MAX_LINE = 512,
} sizes_t;

typedef enum chars
{
    CTRL_D = 4,
    TAB = 9,
    ENTER = 13,
    ESCAPE = 27,
    UP = 65,
    DOWN = 66,
    RIGHT = 67,
    LEFT = 68,
    BACKSPACE = 127,
} chars_t;

typedef struct String
{
    char *chars;
    int len;
} String;

#endif
