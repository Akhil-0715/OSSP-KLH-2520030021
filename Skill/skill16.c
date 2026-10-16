
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_INPUT 1024
#define MAX_TOKENS 128
#define MAX_COMMANDS 16
#define MAX_ARGS 64
#define MAX_REDIRS 16

typedef struct {
    char *operator;
    char *filename;
} Redirection;

typedef struct {
    char *args[MAX_ARGS];
    int argc;
    Redirection redirs[MAX_REDIRS];
    int redir_count;
} Command;

static char *duplicate(const char *s)
{
    char *p = strdup(s);
    if (p == NULL)
        perror("strdup");
    return p;
}

/* Recognize operators even when attached to words. */
static int tokenize(const char *input, char *tokens[])
{
    int count = 0;
    int i = 0;

    while (input[i] != '\0') {
        if (input[i] == ' ' || input[i] == '\t') {
            i++;
            continue;
        }

        char token[ MAX_INPUT ];
        int len = 0;

        if (strncmp(input + i, "2>&1", 4) == 0) {
            strcpy(token, "2>&1");
            i += 4;
        } else if (strncmp(input + i, "2>", 2) == 0) {
            strcpy(token, "2>");
            i += 2;
        } else if (strncmp(input + i, ">>", 2) == 0) {
            strcpy(token, ">>");
            i += 2;
        } else if (input[i] == '|' || input[i] == '<' ||
                   input[i] == '>') {
            token[len++] = input[i++];
            token[len] = '\0';
        } else {
            while (input[i] != '\0' &&
                   input[i] != ' ' && input[i] != '\t' &&
                   input[i] != '|' && input[i] != '<' &&
                   input[i] != '>' &&
                   !(input[i] == '2' &&
                     (input[i + 1] == '>' ||
                      strncmp(input + i, "2>&1", 4) == 0))) {
                if (len >= MAX_INPUT - 1) {
                    fprintf(stderr, "Token too long.\n");
                    return -1;
                }
                token[len++] = input[i++];
            }
            token[len] = '\0';
        }

        if (count >= MAX_TOKENS) {
            fprintf(stderr, "Too many tokens.\n");
            return -1;
        }

        tokens[count] = duplicate(token);
        if (tokens[count] == NULL)
            return -1;
        count++;
    }

    return count;
}

static void freeTokens(char *tokens[], int count)
{
    for (int i = 0; i < count; i++)
        free(tokens[i]);
}

/* Parse commands separated by pipes and store redirections in order. */
static int parsePipeline(char *tokens[], int count,
                         Command commands[], int *command_count)
{
    int n = 0;
    memset(commands, 0, sizeof(Command) * MAX_COMMANDS);

    if (count == 0)
        return -1;

    n = 1;

    for (int i = 0; i < count; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            if (commands[n - 1].argc == 0 || n >= MAX_COMMANDS) {
                fprintf(stderr, "Invalid pipeline syntax.\n");
                return -1;
            }
            n++;
            continue;
        }

        Command *cmd = &commands[n - 1];

        if (strcmp(tokens[i], "<") == 0 ||
            strcmp(tokens[i], ">") == 0 ||
            strcmp(tokens[i], ">>") == 0 ||
            strcmp(tokens[i], "2>") == 0 ||
            strcmp(tokens[i], "2>&1") == 0) {

            const char *op = tokens[i];
            char *filename = NULL;

            if (strcmp(op, "2>&1") != 0) {
                if (i + 1 >= count ||
                    strcmp(tokens[i + 1], "|") == 0 ||
                    strcmp(tokens[i + 1], "<") == 0 ||
                    strcmp(tokens[i + 1], ">") == 0 ||
                    strcmp(tokens[i + 1], ">>") == 0 ||
                    strcmp(tokens[i + 1], "2>") == 0 ||
                    strcmp(tokens[i + 1], "2>&1") == 0) {
                    fprintf(stderr, "Redirection requires a filename.\n");
                    return -1;
                }
                filename = tokens[++i];
            }

            if (cmd->redir_count >= MAX_REDIRS) {
                fprintf(stderr, "Too many redirections.\n");
                return -1;
            }

            cmd->redirs[cmd->redir_count].operator = (char *)op;
            cmd->redirs[cmd->redir_count].filename = filename;
            cmd->redir_count++;
        } else {
            if (cmd->argc >= MAX_ARGS - 1) {
                fprintf(stderr, "Too many arguments.\n");
                return -1;
            }
            cmd->args[cmd->argc++] = tokens[i];
        }
    }

    if (commands[n - 1].argc == 0) {
        fprintf(stderr, "Pipeline cannot end with '|'.\n");
        return -1;
    }

    *command_count = n;
    return 0;
}

static int applyRedirections(Command *cmd)
{
    for (int i = 0; i < cmd->redir_count; i++) {
        char *op = cmd->redirs[i].operator;
        char *filename = cmd->redirs[i].filename;

        if (strcmp(op, "2>&1") == 0) {
            if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
                perror("dup2");
                return -1;
            }
            continue;
        }

        int target;
        int flags;
        mode_t mode = 0644;

        if (strcmp(op, "<") == 0) {
            target = STDIN_FILENO;
            flags = O_RDONLY;
        } else if (strcmp(op, ">") == 0) {
            target = STDOUT_FILENO;
            flags = O_WRONLY | O_CREAT | O_TRUNC;
        } else if (strcmp(op, ">>") == 0) {
            target = STDOUT_FILENO;
            flags = O_WRONLY | O_CREAT | O_APPEND;
        } else {
            target = STDERR_FILENO;
            flags = O_WRONLY | O_CREAT | O_TRUNC;
        }

        int fd = open(filename, flags, mode);
        if (fd < 0) {
            perror(filename);
            return -1;
        }

        if (dup2(fd, target) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }

        close(fd);
    }

    return 0;
}

static void executePipeline(Command commands[], int count)
{
    int previous_read = -1;
    pid_t pids[MAX_COMMANDS];
    int started = 0;
    int failed = 0;

    for (int i = 0; i < count; i++) {
        int pipefd[2] = {-1, -1};

        if (i < count - 1 && pipe(pipefd) < 0) {
            perror("pipe");
            failed = 1;
            break;
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            if (pipefd[0] != -1) close(pipefd[0]);
            if (pipefd[1] != -1) close(pipefd[1]);
            failed = 1;
            break;
        }

        if (pid == 0) {
            if (previous_read != -1 &&
                dup2(previous_read, STDIN_FILENO) < 0) {
                perror("dup2 stdin");
                _exit(1);
            }

            if (i < count - 1 &&
                dup2(pipefd[1], STDOUT_FILENO) < 0) {
                perror("dup2 stdout");
                _exit(1);
            }

            if (previous_read != -1)
                close(previous_read);
            if (pipefd[0] != -1)
                close(pipefd[0]);
            if (pipefd[1] != -1)
                close(pipefd[1]);

            /* Explicit redirections override pipeline defaults. */
            if (applyRedirections(&commands[i]) < 0)
                _exit(1);

            execvp(commands[i].args[0], commands[i].args);
            perror(commands[i].args[0]);
            _exit(127);
        }

        pids[started++] = pid;

        if (previous_read != -1)
            close(previous_read);
        if (pipefd[1] != -1)
            close(pipefd[1]);

        previous_read = pipefd[0];
    }

    if (previous_read != -1)
        close(previous_read);

    for (int i = 0; i < started; i++) {
        int status;
        pid_t result;

        do {
            result = waitpid(pids[i], &status, 0);
        } while (result < 0 && errno == EINTR);

        if (result < 0) {
            perror("waitpid");
            failed = 1;
        } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            failed = 1;
        }
    }

    if (failed)
        fprintf(stderr, "Pipeline completed with an error.\n");
}

int main(void)
{
    char input[MAX_INPUT];

    printf("Skill 15: Combined Redirection and Complex Commands\n");
    printf("Examples:\n");
    printf("  echo hello > output.txt 2> errors.txt\n");
    printf("  cat < input.txt | wc -l\n");
    printf("  ls missing 2>&1 > combined.txt\n");
    printf("Type exit to quit.\n");

    while (1) {
        printf("skill15> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "exit") == 0)
            break;
        if (input[0] == '\0')
            continue;

        char *tokens[MAX_TOKENS] = {0};
        int token_count = tokenize(input, tokens);

        if (token_count < 0) {
            continue;
        }

        Command commands[MAX_COMMANDS];
        int command_count = 0;

        if (parsePipeline(tokens, token_count, commands,
                          &command_count) == 0) {
            executePipeline(commands, command_count);
        }

        freeTokens(tokens, token_count);
    }

    printf("Exiting Skill 15.\n");
    return 0;
}
