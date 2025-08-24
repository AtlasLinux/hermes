#ifndef HERMES_GLOBAL_H
#define HERMES_GLOBAL_H

#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <stdbool.h>
#include <errno.h>

#define BUFFER_MAX_SIZE 1024
#define MAX_LINE 512
#define PARSE_TOKEN_DELIM " \t"
#define HERMES_SUCCESS 1
#define CONFIG_FILE "/home/gingrspacecadet/hermes.conf"

extern const char* name;

typedef enum chars {
    ENTER = 13,
    BACKSPACE = 127,
    CTRL_D = 4,
    TAB = 9,
} chars_t;

typedef struct String {
    char* chars;
    int len;
} String;

#endif