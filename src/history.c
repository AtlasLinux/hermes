#include "builtins.h"

#define MAX_HISTORY_LINES 1000
#define HISTORY_FILE "history.txt"

typedef struct HistoryEntry
{
	int id;
	char *command;
} HistoryEntry;

static char *history_file = HISTORY_FILE;

// isspace implementation
static int own_isspace(int c)
{
	return (c == ' ' || (c >= '\t' && c <= '\r'));
}

// isdigit implementation
static int own_isdigit(int c)
{
	return (c >= '0' && c <= '9');
}

// Helper function to trim whitespace from a string
static char *trim_whitespace(char *str)
{
	while (own_isspace((unsigned char)*str))
		str++;
	if (*str == '\0')
		return str;

	char *end = str + strlen(str) - 1;
	while (end > str && own_isspace((unsigned char)*end))
		end--;
	end[1] = '\0';

	return str;
}

// Read history entries from file
static int read_history(HistoryEntry **entries)
{
	FILE *file = fopen(history_file, "r");
	if (!file)
		return 0;

	char buffer[MAX_LINE];
	int count = 0;
	*entries = malloc(MAX_HISTORY_LINES * sizeof(HistoryEntry));

	while (fgets(buffer, sizeof(buffer), file) && count < MAX_HISTORY_LINES)
	{
		char *line = trim_whitespace(buffer);
		if (*line == '\0')
			continue; // Skip empty lines

		(*entries)[count].id = count + 1;
		(*entries)[count].command = strdup(line);
		count++;
	}

	fclose(file);
	return count;
}

// Write history entries to file
static int write_history(HistoryEntry *entries, int count)
{
	FILE *file = fopen(history_file, "w");
	if (!file)
		return HERMES_FAILURE;

	for (int i = 0; i < count; i++)
	{
		fprintf(file, "%s\n", entries[i].command);
	}

	fclose(file);
	return HERMES_SUCCESS;
}

// Free history entries
static void free_history(HistoryEntry *entries, int count)
{
	for (int i = 0; i < count; i++)
	{
		free(entries[i].command);
	}
	free(entries);
}

// Check if string is a number
static int is_number(const char *str)
{
	if (!str || !*str)
		return 0;
	while (*str)
	{
		if (!own_isdigit((unsigned char)*str))
			return 0;
		str++;
	}
	return 1;
}

// Print history entries with optional filter
static void print_history(HistoryEntry *entries, int count, const char *const *filters, int filter_count, int start_id, int end_id)
{
	for (int i = 0; i < count; i++)
	{
		// Skip if outside range
		if (start_id > 0 && end_id > 0 && (entries[i].id < start_id || entries[i].id > end_id))
			continue;

		// If no filters, print all
		if (filter_count == 0)
		{
			printf("%5d  %s\n", entries[i].id, entries[i].command);
			continue;
		}

		// Check all filters (AND logic)
		int match = 1;
		for (int j = 0; j < filter_count; j++)
		{
			if (!filters[j] || !strstr(entries[i].command, filters[j]))
			{
				match = 0;
				break;
			}
		}

		if (match)
		{
			printf("%5d  %s\n", entries[i].id, entries[i].command);
		}
	}
}

// Delete entries matching filters or IDs
static int delete_entries(HistoryEntry **entries, int *count, const char *const *filters, int filter_count, int start_id, int end_id)
{
	int new_count = 0;
	int deleted = 0;

	for (int i = 0; i < *count; i++)
	{
		int should_delete = 0;

		// Check if entry is in range
		if (start_id > 0 && end_id > 0)
		{
			should_delete = ((*entries)[i].id >= start_id && (*entries)[i].id <= end_id);
		}
		// Check if entry matches all filters
		else if (filter_count > 0)
		{
			should_delete = 1;
			for (int j = 0; j < filter_count; j++)
			{
				if (!strstr((*entries)[i].command, filters[j]))
				{
					should_delete = 0;
					break;
				}
			}
		}

		if (should_delete)
		{
			free((*entries)[i].command);
			deleted++;
		}
		else
		{
			if (new_count != i)
			{
				(*entries)[new_count] = (*entries)[i];
			}
			(*entries)[new_count].id = new_count + 1;
			new_count++;
		}
	}

	*count = new_count;
	int result = write_history(*entries, *count);
	return result == HERMES_SUCCESS ? deleted : result;
}

// Append a command to the history file
int append_to_history(const char *command)
{
	if (!command || *command == '\0')
	{
		return HERMES_FAILURE;
	}

	FILE *file = fopen(history_file, "a+");
	if (!file)
	{
		return HERMES_FAILURE;
	}

	fprintf(file, "%s\n", command);
	fclose(file);
	return HERMES_SUCCESS;
}

// Helper function to count tokens in a string
static int count_tokens(const char *str)
{
	if (!str || *str == '\0')
		return 0;

	int count = 0;
	int in_quotes = 0;
	const char *p = str;

	while (*p)
	{
		// Skip whitespace
		while (own_isspace(*p))
			p++;
		if (!*p)
			break;

		// Found start of a token
		count++;

		// Skip token
		if (*p == '"')
		{
			p++;
			while (*p && (*p != '"' || in_quotes))
			{
				if (*p == '\\' && *(p + 1) == '"')
					p++;
				p++;
			}
			if (*p == '"')
				p++;
		}
		else
		{
			while (*p && !own_isspace(*p))
				p++;
		}
	}

	return count;
}

// Helper function to extract tokens from a string
static char **tokenize_args(const char *str, int *token_count)
{
	if (!str || *str == '\0')
	{
		*token_count = 0;
		return NULL;
	}

	int count = count_tokens(str);
	if (count == 0)
	{
		*token_count = 0;
		return NULL;
	}

	char **tokens = malloc(count * sizeof(char *));
	const char *p = str;
	int idx = 0;

	while (*p && idx < count)
	{
		// Skip whitespace
		while (own_isspace(*p))
			p++;
		if (!*p)
			break;

		// Handle quoted strings
		int quoted = (*p == '"');
		const char *start = p + quoted;

		// Find end of token
		if (quoted)
		{
			p++;
			while (*p && (*p != '"' || (p > start && *(p - 1) == '\\')))
				p++;
		}
		else
		{
			while (*p && !own_isspace(*p))
				p++;
		}

		size_t len = p - start - (quoted ? 0 : 0);
		tokens[idx] = malloc(len + 1);
		strncpy(tokens[idx], start, len);
		tokens[idx][len] = '\0';
		idx++;

		// Skip closing quote if present
		if (quoted && *p == '"')
			p++;
	}

	*token_count = idx;
	return tokens;
}

// Display help information for history command
static void show_history_help(void)
{
	printf("Usage: history [OPTIONS] [FILTERS]\n");
	printf("Display or manipulate command history.\n\n");
	printf("Options:\n");
	printf("  help                 Show this help message\n");
	printf("  --clear              Clear the entire command history\n");
	printf("  --delete [ID] [END]  Delete specific history entries by ID or range\n");
	printf("  [ID] [END]          Show entries from ID to END (if specified)\n");
	printf("\nExamples:\n");
	printf("  history              Show all history entries\n");
	printf("  history 5            Show history entry with ID 5\n");
	printf("  history 10 20       Show history entries from 10 to 20\n");
	printf("  history grep        Show entries containing 'grep'\n");
	printf("  history --delete 5  Delete entry with ID 5\n");
	printf("  history --clear     Clear all history\n");
}

int builtin_history(String *args)
{
	if (!args || !args[0].chars)
		return HERMES_FAILURE;

	// Count non-NULL arguments first
	int arg_count = 0;
	while (args[arg_count].chars != NULL)
	{
		arg_count++;
	}

	// Skip the command name (args[0]) only if it's not the only argument
	if (arg_count > 0 && strcmp(args[0].chars, "history") == 0)
	{
		args++;
		arg_count--;
	}

	// Handle help command
	if (arg_count > 0 && strcmp(args[0].chars, "help") == 0)
	{
		show_history_help();
		return HERMES_SUCCESS;
	}

	// Parse arguments
	int delete_mode = 0;
	int start_id = 0, end_id = 0;
	int range_mode = 0;
	const char **filters = NULL;
	int filter_count = 0;

	// Handle clear command (no other arguments allowed)
	if (arg_count > 0 && strcmp(args[0].chars, "--clear") == 0)
	{
		if (arg_count > 1)
		{
			fprintf(stderr, "history: --clear does not accept any arguments\n");
			return HERMES_FAILURE;
		}

		FILE *file = fopen(history_file, "w");
		if (!file)
		{
			return HERMES_FAILURE;
		}
		fclose(file);
		return HERMES_SUCCESS;
	}

	// Handle delete mode
	if (arg_count > 0 && strcmp(args[0].chars, "--delete") == 0)
	{
		delete_mode = 1;
		args++;
		arg_count--;
	}

	// Parse remaining arguments as filters or IDs
	if (arg_count > 0)
	{
		// Check for ID or range
		if (is_number(args[0].chars))
		{
			start_id = atoi(args[0].chars);
			args++;
			arg_count--;

			// Check for second number (range)
			if (arg_count > 0 && is_number(args[0].chars))
			{
				end_id = atoi(args[0].chars);
				args++;
				arg_count--;
				range_mode = 1;
			}
		}

		// Remaining arguments are filters
		if (arg_count > 0)
		{
			filters = malloc(arg_count * sizeof(const char *));
			for (int i = 0; i < arg_count; i++)
			{
				filters[i] = args[i].chars;
			}
			filter_count = arg_count;
		}
	}

	// Read history
	HistoryEntry *entries = NULL;
	int count = read_history(&entries);

	// Handle delete mode
	if (delete_mode)
	{
		int result = delete_entries(&entries, &count, filters, filter_count, start_id, end_id);
		free_history(entries, count);
		if (filters)
			free(filters);
		return result >= 0 ? HERMES_SUCCESS : HERMES_FAILURE;
	}

	// Print mode - handle different cases
	if (range_mode)
	{
		print_history(entries, count, NULL, 0, start_id, end_id);
	}
	else if (start_id > 0)
	{
		print_history(entries, count, NULL, 0, start_id, start_id);
	}
	else if (filter_count > 0)
	{
		// Convert String array to const char* array for print_history
		const char **filter_array = malloc(filter_count * sizeof(const char *));
		if (!filter_array)
		{
			free_history(entries, count);
			return HERMES_FAILURE;
		}

		for (int i = 0; i < filter_count; i++)
		{
			filter_array[i] = args[i].chars;
		}

		print_history(entries, count, filter_array, filter_count, 0, 0);
		free(filter_array);
	}
	else
	{
		// Default: show all
		print_history(entries, count, NULL, 0, 0, 0);
	}

	if (filters)
		free(filters);

	// Cleanup
	free_history(entries, count);

	return HERMES_SUCCESS;
}
