#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_COMMANDS 10
#define MAX_ARGS 32
#define MAX_INPUT 512

typedef struct {
    char *args[MAX_ARGS];
    int argc;
} Command;

void freeCommands(Command commands[], int count)
{
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < commands[i].argc; j++)
            free(commands[i].args[j]);
    }
}

int parsePipeline(char *input, Command commands[])
{
    int count = 0;
    char *save = NULL;
    char *part = strtok_r(input, "|", &save);

    while (part != NULL && count < MAX_COMMANDS) {
        while (*part == ' ' || *part == '\t')
            part++;

        size_t len = strlen(part);

        while (len > 0 &&
               (part[len - 1] == ' ' || part[len - 1] == '\t'))
            part[--len] = '\0';

        if (len == 0)
            return -1;

        commands[count].argc = 0;

        char *arg_save = NULL;
        char *arg = strtok_r(part, " \t", &arg_save);

        while (arg != NULL && commands[count].argc < MAX_ARGS - 1) {
            commands[count].args[commands[count].argc] =
                strdup(arg);

            if (!commands[count].args[commands[count].argc])
                return -1;

            commands[count].argc++;
            arg = strtok_r(NULL, " \t", &arg_save);
        }

        commands[count].args[commands[count].argc] = NULL;

        if (commands[count].argc == 0)
            return -1;

        count++;
        part = strtok_r(NULL, "|", &save);
    }

    return part != NULL ? -1 : count;
}

int executePipeline(Command commands[], int count)
{
    int previous_read = -1;
    pid_t pids[MAX_COMMANDS];
    int started = 0;

    for (int i = 0; i < count; i++) {
        int pipefd[2] = {-1, -1};

        if (i < count - 1 && pipe(pipefd) < 0) {
            perror("pipe");
            if (previous_read != -1)
                close(previous_read);
            break;
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");

            if (pipefd[0] != -1)
                close(pipefd[0]);
            if (pipefd[1] != -1)
                close(pipefd[1]);
            if (previous_read != -1)
                close(previous_read);

            break;
        }

        if (pid == 0) {
            if (previous_read != -1) {
                if (dup2(previous_read, STDIN_FILENO) < 0)
                    _exit(126);
            }

            if (i < count - 1) {
                if (dup2(pipefd[1], STDOUT_FILENO) < 0)
                    _exit(126);
            }

            if (previous_read != -1)
                close(previous_read);

            if (pipefd[0] != -1)
                close(pipefd[0]);
            if (pipefd[1] != -1)
                close(pipefd[1]);

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

    int result = 0;

    for (int i = 0; i < started; i++) {
        int status;
        pid_t waited;

        do {
            waited = waitpid(pids[i], &status, 0);
        } while (waited < 0 && errno == EINTR);

        if (waited < 0) {
            perror("waitpid");
            result = -1;
        } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            result = -1;
        }
    }

    return result;
}

int main(void)
{
    char input[MAX_INPUT];

    printf("Skill 12: Pipes and Multiple Pipelines\n");
    printf("Enter a pipeline using | between commands.\n");
    printf("Example: ls | wc -l\n");
    printf("Type exit to quit.\n");

    while (1) {
        printf("skill12> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin))
            break;

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "exit") == 0)
            break;

        if (input[0] == '\0')
            continue;

        Command commands[MAX_COMMANDS] = {0};
        int count = parsePipeline(input, commands);

        if (count <= 0) {
            printf("Invalid pipeline syntax or too many commands.\n");
            freeCommands(commands, MAX_COMMANDS);
            continue;
        }

        executePipeline(commands, count);
        freeCommands(commands, count);
    }

    return 0;
}
