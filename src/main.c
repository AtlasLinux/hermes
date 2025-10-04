#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include "globals.h"
#include "builtins.h"

const char *name = "hermes";
struct termios orig_termios;

static char PROMPT[MAX_LINE] = "\r$ ";

static pid_t fg_pid = -1; // current foreground process

static void die(const int code) {
    perror(name);
    exit(code);
}

static void sigint_handler(int sig) {
    (void)sig;
    if (fg_pid > 0) {
        // send to the process group so all children in that group get it 
        kill(-fg_pid, SIGINT);
    }
}

void load_config(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Failed to open config file: %s: %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0' || line[0] == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        if (strcmp(key, "PROMPT") == 0) strncat(PROMPT, val, MAX_LINE - 1);
    }
    fclose(file);
}

void disableRawMode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    // raw.c_oflag &= ~(OPOST);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}


String handle_tab(String buffer) {
    int cap = 16, count = 0;
    String *matches = malloc(cap * sizeof(*matches));
    if (!matches) {
        die(EXIT_FAILURE);
    }

    // Determine current token
    char *last_space = strrchr(buffer.chars, ' ');
    char *token_start = last_space ? last_space + 1 : buffer.chars;
    size_t token_len = strlen(token_start);

    // Determine first command (first token)
    char *first_space = strchr(buffer.chars, ' ');
    size_t first_len = first_space ? (size_t)(first_space - buffer.chars) : buffer.len;
    char first_cmd[BUFFER_MAX_SIZE];
    strncpy(first_cmd, buffer.chars, first_len);
    first_cmd[first_len] = '\0';

    int complete_dirs_only = strcmp(first_cmd, "cd") == 0;

    int first_token = !last_space;

    if (first_token) {
        // Complete builtins + executables in PATH
        for (int b = 0; b < builtin_str_count; b++) {
            if (strncmp(builtin_str[b], token_start, token_len) == 0) {
                if (count == cap) {
                    cap *= 2;
                    matches = realloc(matches, cap * sizeof(*matches));
                    if (!matches) {
                        die(EXIT_FAILURE);
                    }
                }
                matches[count].chars = strdup(builtin_str[b]);
                count++;
            }
        }

        char *path_env = getenv("PATH");
        if (path_env) {
            char *path_copy = strdup(path_env);
            if (!path_copy) {
                die(EXIT_FAILURE);
            }

            for (char *dirp = strtok(path_copy, ":"); dirp; dirp = strtok(NULL, ":")) {
                DIR *d = opendir(dirp);
                if (!d) continue;

                struct dirent *de;
                while ((de = readdir(d)) != NULL) {
                    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                        continue;

                    if (strncmp(de->d_name, token_start, token_len) == 0) {
                        char fullpath[PATH_MAX];
                        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirp, de->d_name);
                        if (access(fullpath, X_OK) == 0) {
                            if (count == cap) {
                                cap *= 2;
                                matches = realloc(matches, cap * sizeof(*matches));
                                if (!matches) {
                                    closedir(d);
                                    free(path_copy);
                                    die(EXIT_FAILURE);
                                }
                            }
                            matches[count].chars = strdup(de->d_name);
                            count++;
                        }
                    }
                }
                closedir(d);
            }
            free(path_copy);
        }
    } else {
        // Split token_start into dir and base parts
        char dirpart[PATH_MAX];
        char basepart[PATH_MAX];

        const char *slash = strrchr(token_start, '/');
        if (slash) {
            size_t dirlen = slash - token_start + 1; // include '/'
            if (dirlen >= sizeof(dirpart))
                dirlen = sizeof(dirpart) - 1;
            memcpy(dirpart, token_start, dirlen);
            dirpart[dirlen] = '\0';
            strncpy(basepart, slash + 1, sizeof(basepart) - 1);
            basepart[sizeof(basepart) - 1] = '\0';
        } else {
            strcpy(dirpart, "./"); // current dir
            strncpy(basepart, token_start, sizeof(basepart) - 1);
            basepart[sizeof(basepart) - 1] = '\0';
        }

        DIR *d = opendir(dirpart);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                    continue;

                if (strncmp(de->d_name, basepart, strlen(basepart)) == 0) {
                    char fullpath[PATH_MAX];
                    snprintf(fullpath, sizeof(fullpath), "%s%s", dirpart, de->d_name);

                    struct stat sb;
                    if (stat(fullpath, &sb) == -1)
                        continue;

                    if (complete_dirs_only && !S_ISDIR(sb.st_mode))
                        continue;

                    if (count == cap) {
                        cap *= 2;
                        matches = realloc(matches, cap * sizeof(*matches));
                        if (!matches) {
                            closedir(d);
                            die(EXIT_FAILURE);
                        }
                    }

                    // Build candidate including dirpart, add '/' if directory
                    if (S_ISDIR(sb.st_mode)) {
                        size_t len = strlen(dirpart) + strlen(de->d_name);
                        char *with_slash = malloc(len + 2);
                        sprintf(with_slash, "%s%s/", dirpart, de->d_name);
                        matches[count].chars = with_slash;
                    } else {
                        size_t len = strlen(dirpart) + strlen(de->d_name);
                        char *cand = malloc(len + 1);
                        sprintf(cand, "%s%s", dirpart, de->d_name);
                        matches[count].chars = cand;
                    }
                    count++;
                }
            }
            closedir(d);
        }
    }

    if (count == 1) {
        // Replace token in buffer
        size_t pre_len = token_start - buffer.chars;
        size_t new_len = pre_len + strlen(matches[0].chars);
        char *new_buf = malloc(new_len + 1);
        memcpy(new_buf, buffer.chars, pre_len);
        strcpy(new_buf + pre_len, matches[0].chars);
        new_buf[new_len] = '\0';

        free(buffer.chars);
        buffer.chars = new_buf;
        buffer.len = (int)new_len;
    } else if (count > 1) {
        printf("\n");
        for (int i = 0; i < count; i++) {
            printf("%s\t", matches[i].chars);
        }
        printf("\n");
    }

    for (int i = 0; i < count; i++)
        free(matches[i].chars);
    free(matches);
    fflush(stdout);
    return buffer;
}

String read_line(HistoryEntry *history, int history_count) {
    String buffer = {.chars = calloc(BUFFER_MAX_SIZE, 1), .len = 0};
    if (!buffer.chars) {
        die(EXIT_FAILURE);
    }

    int cursor = 0;            // current cursor position in buffer
    int history_index = history_count; // start at "after last entry"
    chars_t c;

    while (read(STDIN_FILENO, &c, 1) == 1 && c != ENTER) {
        if (c == ESCAPE) {
            read(STDIN_FILENO, &c, 1);
            if (c == '[') {
                read(STDIN_FILENO, &c, 1);
                switch (c) {
                case 'A': // UP
                    if (history_count == 0) break;
                    if (history_index > 0) history_index--;
                    else break;
                    buffer.len = snprintf(buffer.chars, BUFFER_MAX_SIZE, "%s", history[history_index].command);
                    cursor = buffer.len;
                    break;

                case 'B': // DOWN
                    if (history_count == 0) break;
                    if (history_index < history_count - 1) {
                        history_index++;
                        buffer.len = snprintf(buffer.chars, BUFFER_MAX_SIZE, "%s", history[history_index].command);
                    } else {
                        history_index = history_count;
                        buffer.len = 0;
                        buffer.chars[0] = '\0';
                    }
                    cursor = buffer.len;
                    break;

                case 'C': // RIGHT
                    if (cursor < buffer.len) 
                        cursor++;
                    break;

                case 'D': // LEFT
                    if (cursor > 0) 
                        cursor--;
                    break;
                }
            }
        } else if (c == BACKSPACE || c == 127) {
            if (cursor > 0) {
                memmove(&buffer.chars[cursor - 1], &buffer.chars[cursor], buffer.len - cursor);
                buffer.len--;
                buffer.chars[buffer.len] = '\0';
                cursor--;
            }
        } else if (c == CTRL_D) {
            if (buffer.len == 0) {
                printf("\n");
                disableRawMode();
                exit(SIGINT);
            }
        } else if (c == TAB) {
            buffer = handle_tab(buffer);
            cursor = buffer.len;
        } else {
            if (buffer.len < BUFFER_MAX_SIZE - 1) {
                memmove(&buffer.chars[cursor + 1], &buffer.chars[cursor], buffer.len - cursor);
                buffer.chars[cursor] = (char)c;
                cursor++;
                buffer.len++;
            }
        }

        // redraw line
        printf("\r%s%s\033[K", PROMPT, buffer.chars);
        // move cursor to correct position
        printf("\r%s", PROMPT);
        if (cursor > 0) printf("\033[%dC", cursor);
        fflush(stdout);
    }

    buffer.chars[buffer.len] = '\0';
    printf("\n");
    return buffer;
}

int parse_line(String line, String **out) {
    char *token_str = strtok(line.chars, PARSE_TOKEN_DELIM);
    String token = {.chars = token_str, .len = token_str ? (int)strlen(token_str) : 0};
    String *buffer = malloc(sizeof(String) * BUFFER_MAX_SIZE);
    if (!buffer) {
        die(EXIT_FAILURE);
    }
    int i = 0;

    while (token.chars != NULL) {
        switch (token.chars[0]) {
        case '$':
            token.chars = getenv(token.chars + 1);
            break;
        }
        buffer[i].chars = token.chars;
        buffer[i].len = token.len;
        token_str = strtok(NULL, PARSE_TOKEN_DELIM);
        token.chars = token_str;
        token.len = token_str ? (int)strlen(token_str) : 0;
        i++;
    }

    *out = buffer;
    return i;
}

char **to_argv(String *args, int count) {
    char **argv = malloc((count + 1) * sizeof(char *));
    if (!argv) {
        die(EXIT_FAILURE);
    }
    for (int i = 0; i < count; i++) {
        argv[i] = args[i].chars;
    }
    argv[count] = NULL;
    return argv;
}

// launch child in its own process group, wait robustly 
int launch(String *args, int argc) {
    pid_t pid = fork();
    if (pid == 0) {
        // child: create new process group so signals can be targeted at the group 
        if (setpgid(0, 0) < 0) {
            // not fatal - continue 
        }
        char **argv = to_argv(args, argc);
        execvp(argv[0], argv);

        // exec failed: print error and exit _immediately_ without running parent's atexit handlers 
        perror(argv[0]);
        _exit(127); // common "command not found" code 
    } else if (pid < 0) {
        perror(name);
        return -1;
    } else {
        // parent: set child's pgid (best-effort) and wait 
        if (setpgid(pid, pid) < 0 && errno != EACCES && errno != EPERM) {
            // ignore: some platforms may not allow, but it's best-effort 
        }

        fg_pid = pid;

        int status;
        pid_t w;
        do {
            w = waitpid(pid, &status, 0);
        } while (w == -1 && errno == EINTR);

        // reset fg pid regardless of wait result 
        fg_pid = -1;

        // optionally return child's exit status 
        if (w == -1) {
            return -1;
        }
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return -1;
    }
}

int execute(String *args, int argc) {
    if (args[0].chars == NULL || argc == 0) {
        return 1;
    }

    for (int i = 0; i < builtin_str_count; i++) {
        if (strcmp(args[0].chars, builtin_str[i]) == 0) {
            // For builtin commands, a proper NULL-terminated array is needed
            String *cmd_args = malloc((argc + 1) * sizeof(String));
            if (!cmd_args) {
                return 1;
            }

            for (int j = 0; j < argc; j++) {
                cmd_args[j] = args[j];
            }
            cmd_args[argc].chars = NULL;

            int result = (*builtin_func[i])(cmd_args);
            free(cmd_args);
            return result;
        }
    }

    return launch(args, argc);
}

int main(int argc, char **argv) {
    if (access(CONFIG_FILE, F_OK) == 0) {
        load_config(CONFIG_FILE);
    }

    name = strdup(argv[0]);

    // Load history
    HistoryEntry *history = NULL;
    int history_count = read_history(&history);

    signal(SIGINT, sigint_handler); // enables SIGINT to kill child

    chdir(getenv("HOME"));

    printf("\x1b[2J"); // clear screen
    while (true) {
        printf("\x1b[H\x1b[90B"); // move cursor
        fflush(stdout);

        printf("%s", PROMPT);
        fflush(stdout);

        enableRawMode();

        // Pass the history buffer to the line reader
        String line = read_line(history, history_count);

        // Append new line to history
        if (line.len > 0) {
            append_to_history(line.chars);

            // Add to in-memory array for immediate navigation
            history = realloc(history, (history_count + 1) * sizeof(HistoryEntry));
            history[history_count].command = strdup(line.chars);
            history_count++;
        }

        printf("\x1b[2J\x1b[H"); // clear screen again
        fflush(stdout);

        String *args;
        int argc = parse_line(line, &args);
        if (argc > 0)  {
            disableRawMode();
            execute(args, argc);
        }

        free(line.chars);
        free(args);
    }

    // Cleanup
    for (int i = 0; i < history_count; i++) {
        free(history[i].command);
    }
    free(history);
    free(name);

    return 0;
}
