#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64
#define MAX_PIPES 10

// Signal handler for Ctrl+C
void signal_handler(int sig) {
    printf("\n> ");
    fflush(stdout);
}

// Parse command line into tokens
int parse_command(char *line, char **args, int *background) {
    *background = 0;
    int arg_count = 0;

    char *token = strtok(line, " \t\n");
    while (token != NULL && arg_count < MAX_ARGS - 1) {
        if (strcmp(token, "&") == 0) {
            *background = 1;
        } else {
            args[arg_count++] = token;
        }
        token = strtok(NULL, " \t\n");
    }
    args[arg_count] = NULL;

    return arg_count;
}

// Check if command line contains pipes
int count_pipes(const char *line) {
    int count = 0;
    for (int i = 0; line[i] != '\0'; i++) {
        if (line[i] == '|') count++;
    }
    return count;
}

// Execute a single command
// If redir_in >= 0: redirect stdin from redir_in
// If redir_out >= 0: redirect stdout to redir_out
void execute_single_command(char **args, int redir_in, int redir_out) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // Child process
        if (redir_in >= 0) {
            dup2(redir_in, STDIN_FILENO);
            close(redir_in);
        }
        if (redir_out >= 0) {
            dup2(redir_out, STDOUT_FILENO);
            close(redir_out);
        }

        execvp(args[0], args);
        // If execvp returns, it failed
        perror("execvp failed");
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        waitpid(pid, NULL, 0);
        if (redir_in >= 0) close(redir_in);
        if (redir_out >= 0) close(redir_out);
    }
}

// Execute piped commands
// Example: "ls | grep .c | wc -l"
void execute_piped_commands(char **commands_array, int num_commands) {
    pid_t pids[MAX_PIPES + 1];
    int pipes[MAX_PIPES][2];

    // Create all pipes
    for (int i = 0; i < num_commands - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe failed");
            exit(EXIT_FAILURE);
        }
    }

    // Fork child processes
    for (int i = 0; i < num_commands; i++) {
        pids[i] = fork();

        if (pids[i] < 0) {
            perror("fork failed");
            exit(EXIT_FAILURE);
        }

        if (pids[i] == 0) {
            // Child process

            // Redirect stdin from previous pipe (if not first command)
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            // Redirect stdout to next pipe (if not last command)
            if (i < num_commands - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            // Close all pipe file descriptors
            for (int j = 0; j < num_commands - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // Parse and execute this command
            char *args[MAX_ARGS];
            args[0] = strtok(commands_array[i], " \t");
            int arg_idx = 1;
            while (arg_idx < MAX_ARGS - 1) {
                char *token = strtok(NULL, " \t");
                if (token == NULL) break;
                args[arg_idx++] = token;
            }
            args[arg_idx] = NULL;

            execvp(args[0], args);
            perror("execvp failed");
            exit(EXIT_FAILURE);
        }
    }

    // Parent process: close all pipes and wait for children
    for (int i = 0; i < num_commands - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (int i = 0; i < num_commands; i++) {
        waitpid(pids[i], NULL, 0);
    }
}

// Handle input/output redirection
// Modifies the command line to remove redirection operators
// Returns: input_fd, output_fd
void extract_redirection(char **args, int *redir_in, int *redir_out, int *arg_count) {
    *redir_in = -1;
    *redir_out = -1;

    for (int i = 0; i < *arg_count; i++) {
        if (strcmp(args[i], ">") == 0) {
            if (i + 1 < *arg_count) {
                *redir_out = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (*redir_out < 0) {
                    perror("open failed");
                    return;
                }
                // Remove redirection from args
                int j = i;
                while (j + 2 < *arg_count) {
                    args[j] = args[j + 2];
                    j++;
                }
                *arg_count = i;
            }
        } else if (strcmp(args[i], ">>") == 0) {
            if (i + 1 < *arg_count) {
                *redir_out = open(args[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (*redir_out < 0) {
                    perror("open failed");
                    return;
                }
                int j = i;
                while (j + 2 < *arg_count) {
                    args[j] = args[j + 2];
                    j++;
                }
                *arg_count = i;
            }
        }
    }
}

int main(void) {
    signal(SIGINT, signal_handler);

    char line[MAX_CMD_LEN];
    char *args[MAX_ARGS];
    int background = 0;
    int arg_count = 0;

    printf("=== Mini Shell ===\n");
    printf("Type 'exit' to quit, 'help' for commands\n\n");

    while (1) {
        printf("> ");
        fflush(stdout);

        // Read command line
        if (fgets(line, MAX_CMD_LEN, stdin) == NULL) {
            break;
        }

        // Remove newline
        line[strcspn(line, "\n")] = 0;

        // Skip empty lines
        if (strlen(line) == 0) continue;

        // Make a copy for parsing (strtok modifies original)
        char line_copy[MAX_CMD_LEN];
        strcpy(line_copy, line);

        // Parse command
        arg_count = parse_command(line_copy, args, &background);

        if (arg_count == 0) continue;

        // Handle built-in commands
        if (strcmp(args[0], "exit") == 0) {
            break;
        }

        if (strcmp(args[0], "cd") == 0) {
            if (arg_count > 1) {
                if (chdir(args[1]) < 0) {
                    perror("cd failed");
                }
            } else {
                chdir(getenv("HOME"));
            }
            continue;
        }

        if (strcmp(args[0], "help") == 0) {
            printf("Mini Shell - Basic Commands:\n");
            printf("  exit           - Exit shell\n");
            printf("  cd <dir>       - Change directory\n");
            printf("  help           - Show this help\n");
            printf("\nSupported features:\n");
            printf("  - Pipes: ls | grep .c\n");
            printf("  - Redirection: ls > file.txt\n");
            printf("  - Background jobs: sleep 10 &\n");
            continue;
        }

        // Check for pipes
        int pipe_count = count_pipes(line);
        if (pipe_count > 0) {
            // Split by pipes
            char commands[MAX_PIPES + 1][MAX_CMD_LEN];
            int cmd_idx = 0;
            char *pipe_token = strtok(line, "|");

            while (pipe_token != NULL && cmd_idx < MAX_PIPES + 1) {
                // Trim whitespace
                int start = 0;
                while (pipe_token[start] == ' ') start++;
                strcpy(commands[cmd_idx++], pipe_token + start);
                pipe_token = strtok(NULL, "|");
            }

            execute_piped_commands((char **)commands, cmd_idx);
        } else {
            // Single command - check for redirection
            extract_redirection(args, &args[-1], &args[-2], &arg_count);

            int redir_in = -1;
            int redir_out = -1;
            extract_redirection(args, &redir_in, &redir_out, &arg_count);

            execute_single_command(args, redir_in, redir_out);
        }
    }

    printf("Goodbye!\n");
    return 0;
}
