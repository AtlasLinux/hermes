#include "globals.h"
#include "builtins/cd.h"
#include "builtins/exit.h"
#include "builtins/echo.h"

const char* name = "hermes";
struct termios orig_termios;

static char PROMPT[MAX_LINE] = "\r";

void load_config(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Failed to open config file: %s: %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0]=='\0' || line[0]=='#') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        if (strcmp(key, "PROMPT") == 0) {
            strncat(PROMPT, val, MAX_LINE-1);
        }
    }
    fclose(file);
}

char* builtin_str[] = {
    "cd",
    "exit",
    "echo",
};

int (*builtin_func[]) (String*) = {
    &builtin_cd,
    &builtin_exit,
    &builtin_echo,
};

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
    return buffer;
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
        switch (token[0]) {
            case '$':
                token = getenv(token + 1);
                break;
        }
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

int num_builtins(void) {
    return sizeof(builtin_str) / sizeof(char*);
}

int launch(String *args, int argc) {
    pid_t pid, wpid;
    int status;

    pid = fork();
    if (pid == 0) {
        char **argv = to_argv(args, argc);
        if (execvp(argv[0], argv) == -1) {
            perror(name);
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror(name);
    } else {
        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return EXIT_FAILURE;
}

int execute(String* args, int argc) {
    if (args[0].chars == NULL) {
        return 1;
    }

    for (int i = 0; i < num_builtins(); i++) {
        if (strcmp(args[0].chars, builtin_str[i]) == 0) {
            return (*builtin_func[i])(args);
        }
    }

    return launch(args, argc);
}

int main(int argc, char** argv) {
    load_config(CONFIG_FILE);
    name = argv[0];

    int status = 0;
    while (true) {
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
            status = execute(args, argc);
        }

        free(line.chars);
        free(args);
    }

    return 0;
}
