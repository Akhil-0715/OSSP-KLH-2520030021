#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_INPUT 512
#define MAX_ARGS 64
#define MAX_TOKENS 128

typedef struct {
    char *args[MAX_ARGS];
    int argc;
    char *append_file;
    char *error_file;
} Command;

/* Free dynamically allocated tokens. */
static void freeTokens(char *tokens[], int count)
{
    for (int i = 0; i < count; i++)
        free(tokens[i]);
}

/* Split input while recognizing >> and 2>, including attached operators. */
static int tokenizeInput(const char *input, char *tokens[])
{
    int count = 0;
    int i = 0;

    while (input[i] != '\0') {
        while (input[i] == ' ' || input[i] == '\t')
            i++;

        if (input[i] == '\0')
            break;

        char token[MAX_INPUT];
        int length = 0;

        if (input[i] == '>' && input[i + 1] == '>') {
            token[length++] = '>';
            token[length++] = '>';
            i += 2;
        } else if (input[i] == '2' && input[i + 1] == '>') {
            token[length++] = '2';
            token[length++] = '>';
            i += 2;
        } else {
            while (input[i] != '\0' &&
                   input[i] != ' ' &&
                   input[i] != '\t' &&
                   !(input[i] == '>' && input[i + 1] == '>') &&
                   !(input[i] == '2' && input[i + 1] == '>')) {
                if (length >= MAX_INPUT - 1) {
                    fprintf(stderr, "Error: token too long.\n");
                    freeTokens(tokens, count);
                    return -1;
                }

                token[length++] = input[i++];
            }
        }

        token[length] = '\0';

        if (count >= MAX_TOKENS) {
            fprintf(stderr, "Error: too many tokens.\n");
            freeTokens(tokens, count);
            return -1;
        }

        tokens[count] = strdup(token);

        if (tokens[count] == NULL) {
            perror("strdup");
            freeTokens(tokens, count);
            return -1;
        }

        count++;
    }

    return count;
}

/* Parse command arguments and redirection operators. */
static int parseCommand(char *tokens[], int count, Command *cmd)
{
    memset(cmd, 0, sizeof(*cmd));

    for (int i = 0; i < count; i++) {
        if (strcmp(tokens[i], ">>") == 0 ||
            strcmp(tokens[i], "2>") == 0) {

            int is_append = strcmp(tokens[i], ">>") == 0;

            if (i + 1 >= count ||
                strcmp(tokens[i + 1], ">>") == 0 ||
                strcmp(tokens[i + 1], "2>") == 0) {
                fprintf(stderr, "Syntax error: missing filename.\n");
                return -1;
            }

            if (is_append) {
                if (cmd->append_file != NULL) {
                    fprintf(stderr, "Error: duplicate >> redirection.\n");
                    return -1;
                }
                cmd->append_file = tokens[++i];
            } else {
                if (cmd->error_file != NULL) {
                    fprintf(stderr, "Error: duplicate 2> redirection.\n");
                    return -1;
                }
                cmd->error_file = tokens[++i];
            }
        } else {
            if (cmd->argc >= MAX_ARGS - 1) {
                fprintf(stderr, "Error: too many arguments.\n");
                return -1;
            }

            cmd->args[cmd->argc++] = tokens[i];
        }
    }

    cmd->args[cmd->argc] = NULL;

    if (cmd->argc == 0) {
        fprintf(stderr, "Error: no command specified.\n");
        return -1;
    }

    return 0;
}

/* Execute the command with the requested redirections. */
static void executeCommand(Command *cmd)
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        /* Skill 14: append stdout without deleting existing contents. */
        if (cmd->append_file != NULL) {
            int fd = open(cmd->append_file,
                          O_WRONLY | O_CREAT | O_APPEND,
                          0644);

            if (fd < 0) {
                perror(cmd->append_file);
                _exit(1);
            }

            if (dup2(fd, STDOUT_FILENO) < 0) {
                perror("dup2 stdout");
                close(fd);
                _exit(1);
            }

            close(fd);
        }

        /* Skill 14: send stderr to a separate file. */
        if (cmd->error_file != NULL) {
            int fd = open(cmd->error_file,
                          O_WRONLY | O_CREAT | O_TRUNC,
                          0644);

            if (fd < 0) {
                perror(cmd->error_file);
                _exit(1);
            }

            if (dup2(fd, STDERR_FILENO) < 0) {
                perror("dup2 stderr");
                close(fd);
                _exit(1);
            }

            close(fd);
        }

        execvp(cmd->args[0], cmd->args);

        /* This error is redirected if 2> was supplied. */
        perror(cmd->args[0]);
        _exit(127);
    }

    int status;
    pid_t result;

    do {
        result = waitpid(pid, &status, 0);
    } while (result < 0 && errno == EINTR);

    if (result < 0)
        perror("waitpid");
}

int main(void)
{
    char input[MAX_INPUT];

    printf("Skill 14: Append and stderr Redirection\n");
    printf("Append example: echo hello >> output.txt\n");
    printf("Error example: ls missing_file 2> errors.txt\n");
    printf("Type exit to quit.\n");

    while (1) {
        printf("skill14> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "exit") == 0)
            break;

        if (input[0] == '\0')
            continue;

        char *tokens[MAX_TOKENS] = {0};
        int token_count = tokenizeInput(input, tokens);

        if (token_count < 0)
            continue;

        if (token_count == 0) {
            freeTokens(tokens, token_count);
            continue;
        }

        Command cmd;

        if (parseCommand(tokens, token_count, &cmd) == 0)
            executeCommand(&cmd);

        freeTokens(tokens, token_count);
    }

    printf("Exiting Skill 14.\n");
    return 0;
}
