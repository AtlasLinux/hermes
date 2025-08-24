#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>

#define BUFFER_MAX_SIZE 1024
#define PARSE_TOKEN_DELIM " \t"
#define PROMPT "> "

const char* name;

struct termios orig_termios;

void disableRawMode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);        
    raw.c_oflag &= ~(OPOST);               

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

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

String handle_tab(String buffer) {
    DIR *d;
    struct dirent *dir;
    int i = 0;
    int cap = 16;
    String *dirs = malloc(cap * sizeof(*dirs));
    if (!dirs) {
        perror(name);
        exit(EXIT_FAILURE);
    }

    d = opendir(".");
    printf("\n\r");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, buffer.chars)) {
                if (i == cap) {
                    cap *= 2;
                    dirs = realloc(dirs, cap * sizeof(*dirs));
                    if (!dirs) {
                        perror(name);
                        closedir(d);
                        exit(EXIT_FAILURE);
                    }
                }
                dirs[i].chars = strdup(dir->d_name);
                if (!dirs[i].chars) {
                    perror(name);
                    closedir(d);
                    exit(EXIT_FAILURE);
                }
                i++;
            }
        }

        if (i == 1) {
            buffer.chars = dirs[0].chars;
            return buffer;
        } else {
            for (int j = 0; j < i; j++) {
                printf("%s\t", dirs[j].chars);
            }
        }
        closedir(d);
        return buffer;
    }

    // cleanup
    for (int j = 0; j < i; j++) {
        free(dirs[j].chars);
    }
    free(dirs);

    fflush(stdout);
}

String read_line(void) {
    chars_t c;
    String buffer = { .chars = calloc(BUFFER_MAX_SIZE, sizeof(char)), .len = 0};
    if (!buffer.chars) {
        perror(name);
        exit(1);
    }

    while (read(STDIN_FILENO, &c, 1) == 1 && c != ENTER) {
        switch (c) {
            case CTRL_D:
                if (buffer.len == 0) {
                    printf("\n\r");
                    disableRawMode();
                    exit(SIGINT);
                }
                break;
            case BACKSPACE:
                if (buffer.len > 0) {
                    printf("\b \b");
                    fflush(NULL);
                    buffer.chars[--buffer.len] = '\0';
                }
                break;
            case TAB:
                buffer = handle_tab(buffer);
                printf("\n\r%s%s", PROMPT, buffer.chars);
                fflush(NULL);
                break;
            default:
                buffer.chars[buffer.len++] = (char)c;
                printf("\r%s%s", PROMPT, buffer.chars);
                fflush(stdout);
                break;
        }
        if (buffer.len >= BUFFER_MAX_SIZE - 1) break;
    }
    buffer.chars[buffer.len] = '\0';
    return buffer;
}

int parse_line(String line, String **out) {
    char *token = strtok(line.chars, PARSE_TOKEN_DELIM);
    String *buffer = malloc(sizeof(String) * BUFFER_MAX_SIZE);
    if (!buffer) {
        perror(name);
        exit(1);
    }
    int i = 0;

    while (token != NULL) {
        buffer[i].chars = token;
        buffer[i].len = strlen(token);
        token = strtok(NULL, PARSE_TOKEN_DELIM);
        i++;
    }

    *out = buffer;
    return i;
}

char **to_argv(String *args, int count) {
    char **argv = malloc((count + 1) * sizeof(char *));
    if (!argv) {
        perror(name);
        exit(1);
    }
    for (int i = 0; i < count; i++) {
        argv[i] = args[i].chars;
    }
    argv[count] = NULL;
    return argv;
}

void launch(String *args, int argc) {
    pid_t pid = fork();
    if (pid == -1) {
        perror(name);
        return;
    }
    if (pid == 0) {
        char **argv = to_argv(args, argc);
        execvp(argv[0], argv);
        perror(name);
        exit(1);
    } else {
        wait(NULL);
    }
}

int main(int argc, char** argv) {
    
    name = argv[0];

    while (1) {
        printf(PROMPT);
        fflush(NULL);

        enableRawMode();

        String line = read_line();

        printf("\n\r");
        fflush(NULL);

        String *args;
        int argc = parse_line(line, &args);
        if (argc > 0) {
            disableRawMode();
            launch(args, argc);
        }

        free(line.chars);
        free(args);
    }

    return 0;
}
