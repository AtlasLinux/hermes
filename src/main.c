#include "globals.h"
#include "builtins.h"
#include <string.h>
#include <unistd.h>

const char *name = "hermes";
struct termios orig_termios;

static char PROMPT[MAX_LINE] = "\r$ ";

void load_config(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
    {
        fprintf(stderr, "Failed to open config file: %s: %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), file))
    {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0' || line[0] == '#')
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        if (strcmp(key, "PROMPT") == 0)
        {
            strncat(PROMPT, val, MAX_LINE - 1);
        }
    }
    fclose(file);
}

char *builtin_str[] = {
    "cd",
    "exit",
    "echo",
    "export",
    "help",
    "clear",
    "fish",
    "history"};

int (*builtin_func[])(String *) = {
    &builtin_cd,
    &builtin_exit,
    &builtin_echo,
    &builtin_export,
    &builtin_help,
    &builtin_clear,
    &builtin_fish,
    &builtin_history};

int num_builtins(void)
{
    return sizeof(builtin_str) / sizeof(char *);
}

void disableRawMode(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode(void)
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

#include <limits.h>
#include <sys/stat.h>

String handle_tab(String buffer)
{
    int cap = 16, count = 0;
    String *matches = malloc(cap * sizeof(*matches));
    if (!matches)
    {
        perror(name);
        exit(EXIT_FAILURE);
    }

    printf("\n\r");

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

    if (first_token)
    {
        // Complete builtins + executables in PATH
        for (int b = 0; b < num_builtins(); b++)
        {
            if (strncmp(builtin_str[b], token_start, token_len) == 0)
            {
                if (count == cap)
                {
                    cap *= 2;
                    matches = realloc(matches, cap * sizeof(*matches));
                    if (!matches)
                    {
                        perror(name);
                        exit(EXIT_FAILURE);
                    }
                }
                matches[count].chars = strdup(builtin_str[b]);
                count++;
            }
        }

        char *path_env = getenv("PATH");
        if (path_env)
        {
            char *path_copy = strdup(path_env);
            if (!path_copy)
            {
                perror(name);
                exit(EXIT_FAILURE);
            }

            for (char *dirp = strtok(path_copy, ":"); dirp; dirp = strtok(NULL, ":"))
            {
                DIR *d = opendir(dirp);
                if (!d)
                    continue;

                struct dirent *de;
                while ((de = readdir(d)) != NULL)
                {
                    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                        continue;

                    if (strncmp(de->d_name, token_start, token_len) == 0)
                    {
                        char fullpath[PATH_MAX];
                        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirp, de->d_name);
                        if (access(fullpath, X_OK) == 0)
                        {
                            if (count == cap)
                            {
                                cap *= 2;
                                matches = realloc(matches, cap * sizeof(*matches));
                                if (!matches)
                                {
                                    perror(name);
                                    closedir(d);
                                    free(path_copy);
                                    exit(EXIT_FAILURE);
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
    }
    else
    {
        // Complete filenames / directories in CWD
        DIR *d = opendir(".");
        if (d)
        {
            struct dirent *de;
            while ((de = readdir(d)) != NULL)
            {
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                    continue;

                if (strncmp(de->d_name, token_start, token_len) == 0)
                {
                    char fullpath[PATH_MAX];
                    snprintf(fullpath, sizeof(fullpath), "./%s", de->d_name);

                    struct stat sb;
                    if (stat(fullpath, &sb) == -1)
                        continue;

                    if (complete_dirs_only && !S_ISDIR(sb.st_mode))
                        continue;

                    if (count == cap)
                    {
                        cap *= 2;
                        matches = realloc(matches, cap * sizeof(*matches));
                        if (!matches)
                        {
                            perror(name);
                            closedir(d);
                            exit(EXIT_FAILURE);
                        }
                    }

                    // add trailing / if directory
                    if (S_ISDIR(sb.st_mode))
                    {
                        size_t len = strlen(de->d_name);
                        char *with_slash = malloc(len + 2);
                        strcpy(with_slash, de->d_name);
                        with_slash[len] = '/';
                        with_slash[len + 1] = '\0';
                        matches[count].chars = with_slash;
                    }
                    else
                    {
                        matches[count].chars = strdup(de->d_name);
                    }
                    count++;
                }
            }
            closedir(d);
        }
    }

    if (count == 1)
    {
        // Replace token in buffer
        size_t pre_len = token_start - buffer.chars;
        size_t new_len = pre_len + strlen(matches[0].chars);
        char *new_buf = malloc(new_len + 1);
        memcpy(new_buf, buffer.chars, pre_len);
        strcpy(new_buf + pre_len, matches[0].chars);
        new_buf[new_len] = '\0';

        free(buffer.chars);
        buffer.chars = new_buf;
        buffer.len = new_len;
    }
    else if (count > 1)
    {
        for (int i = 0; i < count; i++)
        {
            printf("%s\t", matches[i].chars);
        }
    }

    for (int i = 0; i < count; i++)
        free(matches[i].chars);
    free(matches);
    fflush(stdout);
    return buffer;
}

String read_line(void)
{
    chars_t c;
    String buffer = {.chars = calloc(BUFFER_MAX_SIZE, sizeof(char)), .len = 0};
    if (!buffer.chars)
    {
        perror(name);
        exit(1);
    }

    while (read(STDIN_FILENO, &c, 1) == 1 && c != ENTER)
    {
        switch (c)
        {
        case ESCAPE:
            read(STDIN_FILENO, &c, 1);
            switch (c)
            {
            case '[':
                read(STDIN_FILENO, &c, 1);
                switch (c)
                {
                case UP:
                    printf("\033[A");
                    fflush(stdout);
                    break;
                case DOWN:
                    printf("\033[B");
                    fflush(stdout);
                    break;
                case RIGHT:
                    printf("\033[C");
                    fflush(stdout);
                    break;
                case LEFT:
                    printf("\033[D");
                    fflush(stdout);
                    break;
                }
            }
            break;
        case CTRL_D:
            if (buffer.len == 0)
            {
                printf("\n\r");
                disableRawMode();
                exit(SIGINT);
            }
            break;
        case BACKSPACE:
            if (buffer.len > 0)
            {
                printf("\b \b");
                fflush(stdout);
                buffer.chars[--buffer.len] = '\0';
            }
            break;
        case TAB:
            buffer = handle_tab(buffer);
            printf("\n\r%s%s", PROMPT, buffer.chars);
            fflush(stdout);
            break;
        default:
            buffer.chars[buffer.len++] = (char)c;
            printf("\r%s%s", PROMPT, buffer.chars);
            fflush(stdout);
            break;
        }
        if (buffer.len >= BUFFER_MAX_SIZE - 1)
            break;
    }
    buffer.chars[buffer.len] = '\0';
    return buffer;
}

int parse_line(String line, String **out)
{
    char *token_str = strtok(line.chars, PARSE_TOKEN_DELIM);
    String token = {.chars = token_str, .len = token_str ? strlen(token_str) : 0};
    String *buffer = malloc(sizeof(String) * BUFFER_MAX_SIZE);
    if (!buffer)
    {
        perror(name);
        exit(1);
    }
    int i = 0;

    while (token.chars != NULL)
    {
        switch (token.chars[0])
        {
        case '$':
            token.chars = getenv(token.chars + 1);
            break;
        }
        buffer[i].chars = token.chars;
        buffer[i].len = token.len;
        token_str = strtok(NULL, PARSE_TOKEN_DELIM);
        token.chars = token_str;
        token.len = token_str ? strlen(token_str) : 0;
        i++;
    }

    *out = buffer;
    return i;
}

char **to_argv(String *args, int count)
{
    char **argv = malloc((count + 1) * sizeof(char *));
    if (!argv)
    {
        perror(name);
        exit(1);
    }
    for (int i = 0; i < count; i++)
    {
        argv[i] = args[i].chars;
    }
    argv[count] = NULL;
    return argv;
}

int launch(String *args, int argc)
{
    pid_t pid, wpid;
    int status;

    pid = fork();
    if (pid == 0)
    {
        char **argv = to_argv(args, argc);
        if (execvp(argv[0], argv) == -1)
        {
            perror(name);
        }
        exit(EXIT_FAILURE);
    }
    else if (pid < 0)
    {
        perror(name);
    }
    else
    {
        do
        {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return EXIT_FAILURE;
}

int execute(String *args, int argc)
{
    if (args[0].chars == NULL || argc == 0)
    {
        return 1;
    }

    // Make a copy of the command name for comparison
    char *cmd = strdup(args[0].chars);
    if (!cmd)
    {
        return 1;
    }

    for (int i = 0; i < num_builtins(); i++)
    {
        if (strcmp(cmd, builtin_str[i]) == 0)
        {
            // For builtin commands, we need to create a proper NULL-terminated array
            String *cmd_args = malloc((argc + 1) * sizeof(String));
            if (!cmd_args)
            {
                free(cmd);
                return 1;
            }

            // Copy the arguments
            for (int j = 0; j < argc; j++)
            {
                cmd_args[j] = args[j];
            }
            cmd_args[argc].chars = NULL; // NULL-terminate the array

            int result = (*builtin_func[i])(cmd_args);
            free(cmd_args);
            free(cmd);
            return result;
        }
    }

    free(cmd);
    return launch(args, argc);
}

int main(int argc, char **argv)
{
    if (access(CONFIG_FILE, F_OK) == 0)
    {
        load_config(CONFIG_FILE);
    }

    name = argv[0];

    int status;
    printf("\x1b[2J");
    while (true)
    {
        printf("\x1b[H\x1b[90B");
        fflush(stdout);

        printf("%s", PROMPT);
        fflush(stdout);

        enableRawMode();

        String line = read_line();

        append_to_history(line.chars);

        printf("\x1b[2J\x1b[H");
        fflush(stdout);

        String *args;
        int argc = parse_line(line, &args);
        if (argc > 0)
        {
            disableRawMode();
            status = execute(args, argc);
        }

        free(line.chars);
        free(args);
    }

    return 0;
}
