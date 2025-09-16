#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HISTORY_LINES 1000
#define MAX_LINE 1024

typedef struct HistoryEntry
{
    int id;
    char *command;
} HistoryEntry;

// Safe function to get history file path
static inline char *history_file(void) {
    const char *home = getenv("HOME");
    if (!home) return NULL;
    size_t len = strlen(home) + strlen("/.history") + 1;
    char *path = malloc(len);
    if (!path) return NULL;
    strcpy(path, home);
    strcat(path, "/.history");
    return path;
}

// isspace implementation
static int own_isspace(int c) { return (c == ' ' || (c >= '\t' && c <= '\r')); }

// isdigit implementation
static int own_isdigit(int c) { return (c >= '0' && c <= '9'); }

// Trim whitespace
static char *trim_whitespace(char *str) {
    while (own_isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && own_isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

// Read history
static int read_history(HistoryEntry **entries) {
    char *hf = history_file();
    if (!hf) return 0;
    FILE *file = fopen(hf, "r");
    free(hf);
    if (!file) return 0;

    char buffer[MAX_LINE];
    int count = 0;
    *entries = malloc(MAX_HISTORY_LINES * sizeof(HistoryEntry));
    if (!*entries) { fclose(file); return 0; }

    while (fgets(buffer, sizeof(buffer), file) && count < MAX_HISTORY_LINES) {
        char *line = trim_whitespace(buffer);
        if (*line == '\0') continue;
        (*entries)[count].id = count + 1;
        (*entries)[count].command = strdup(line);
        count++;
    }

    fclose(file);
    return count;
}

// Write history
static int write_history(HistoryEntry *entries, int count) {
    char *hf = history_file();
    if (!hf) return HERMES_FAILURE;
    FILE *file = fopen(hf, "w");
    free(hf);
    if (!file) return HERMES_FAILURE;
    for (int i = 0; i < count; i++) fprintf(file, "%s\n", entries[i].command);
    fclose(file);
    return HERMES_SUCCESS;
}

// Free history
static void free_history(HistoryEntry *entries, int count) {
    for (int i = 0; i < count; i++) free(entries[i].command);
    free(entries);
}

// Check number
static int is_number(const char *str) {
    if (!str || !*str) return 0;
    while (*str) { if (!own_isdigit((unsigned char)*str)) return 0; str++; }
    return 1;
}

// Print history with optional filters
static void print_history(HistoryEntry *entries, int count, const char *const *filters, int filter_count, int start_id, int end_id) {
    for (int i = 0; i < count; i++) {
        if (start_id > 0 && end_id > 0 && (entries[i].id < start_id || entries[i].id > end_id)) continue;
        if (filter_count == 0) { printf("%5d  %s\n", entries[i].id, entries[i].command); continue; }
        int match = 1;
        for (int j = 0; j < filter_count; j++)
            if (!filters[j] || !strstr(entries[i].command, filters[j])) { match = 0; break; }
        if (match) printf("%5d  %s\n", entries[i].id, entries[i].command);
    }
}

// Delete entries
static int delete_entries(HistoryEntry **entries, int *count, const char *const *filters, int filter_count, int start_id, int end_id) {
    int new_count = 0, deleted = 0;
    for (int i = 0; i < *count; i++) {
        int should_delete = 0;
        if (start_id > 0 && end_id > 0) should_delete = ((*entries)[i].id >= start_id && (*entries)[i].id <= end_id);
        else if (filter_count > 0) {
            should_delete = 1;
            for (int j = 0; j < filter_count; j++)
                if (!strstr((*entries)[i].command, filters[j])) { should_delete = 0; break; }
        }
        if (should_delete) { free((*entries)[i].command); deleted++; }
        else { if (new_count != i) (*entries)[new_count] = (*entries)[i]; (*entries)[new_count].id = new_count + 1; new_count++; }
    }
    *count = new_count;
    int result = write_history(*entries, *count);
    return result == HERMES_SUCCESS ? deleted : result;
}

// Append command
int append_to_history(const char *command) {
    if (!command || *command == '\0') return HERMES_FAILURE;
    char *hf = history_file();
    if (!hf) return HERMES_FAILURE;
    FILE *file = fopen(hf, "a+");
    free(hf);
    if (!file) return HERMES_FAILURE;
    fprintf(file, "%s\n", command);
    fclose(file);
    return HERMES_SUCCESS;
}

// Show help
static void show_history_help(void) {
    printf("Usage: history [OPTIONS] [FILTERS]\n");
    printf("Display or manipulate command history.\n\n");
    printf("Options:\n");
    printf("  help                 Show this help message\n");
    printf("  --clear              Clear the entire command history\n");
    printf("  --delete [ID] [END]  Delete specific history entries by ID or range\n");
    printf("  [ID] [END]          Show entries from ID to END (if specified)\n");
}

// The main builtin_history function
int builtin_history(String *args) {
    if (!args || !args[0].chars) return HERMES_FAILURE;

    // Count arguments
    int arg_count = 0;
    while (args[arg_count].chars != NULL) arg_count++;

    if (arg_count > 0 && strcmp(args[0].chars, "history") == 0) { args++; arg_count--; }

    // Handle help
    if (arg_count > 0 && strcmp(args[0].chars, "help") == 0) { show_history_help(); return HERMES_SUCCESS; }

    int delete_mode = 0, start_id = 0, end_id = 0, range_mode = 0;
    const char **filters = NULL; int filter_count = 0;

    // Handle --clear
    if (arg_count > 0 && strcmp(args[0].chars, "--clear") == 0) {
        if (arg_count > 1) { fprintf(stderr, "history: --clear does not accept arguments\n"); return HERMES_FAILURE; }
        char *hf = history_file(); if (!hf) return HERMES_FAILURE;
        FILE *file = fopen(hf, "w"); free(hf); if (!file) return HERMES_FAILURE;
        fclose(file); return HERMES_SUCCESS;
    }

    // Handle --delete
    if (arg_count > 0 && strcmp(args[0].chars, "--delete") == 0) { delete_mode = 1; args++; arg_count--; }

    // Parse remaining args
    if (arg_count > 0) {
        if (is_number(args[0].chars)) {
            start_id = atoi(args[0].chars); args--; arg_count--;
            if (arg_count > 0 && is_number(args[0].chars)) { end_id = atoi(args[0].chars); range_mode = 1; }
        }
        if (arg_count > 0) { filters = malloc(arg_count * sizeof(const char *)); for (int i = 0; i < arg_count; i++) filters[i] = args[i].chars; filter_count = arg_count; }
    }

    HistoryEntry *entries = NULL;
    int count = read_history(&entries);

    if (delete_mode) {
        int result = delete_entries(&entries, &count, filters, filter_count, start_id, end_id);
        free_history(entries, count);
        if (filters) free(filters);
        return result >= 0 ? HERMES_SUCCESS : HERMES_FAILURE;
    }

    if (range_mode) print_history(entries, count, NULL, 0, start_id, end_id);
    else if (start_id > 0) print_history(entries, count, NULL, 0, start_id, start_id);
    else if (filter_count > 0) print_history(entries, count, filters, filter_count, 0, 0);
    else print_history(entries, count, NULL, 0, 0, 0);

    if (filters) free(filters);
    free_history(entries, count);
    return HERMES_SUCCESS;
}
