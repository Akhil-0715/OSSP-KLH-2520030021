
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64

static char *trim(char *text)
{
    while (isspace((unsigned char)*text))
        text++;

    size_t len = strlen(text);

    while (len > 0 && isspace((unsigned char)text[len - 1]))
        text[--len] = '\0';

    return text;
}

static int tokenize(char *input, char *args[])
{
    int argc = 0;
    char *saveptr = NULL;
    char *token = strtok_r(input, " \t", &saveptr);

    while (token != NULL && argc < MAX_ARGS - 1) {
        args[argc++] = token;
        token = strtok_r(NULL, " \t", &saveptr);
    }

    args[argc] = NULL;
    return argc;
}

static void run_command(char *args[])
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        execvp(args[0], args);
        perror(args[0]);
        _exit(errno == ENOENT ? 127 : 126);
    }

    int status;
    pid_t result;

    do {
        result = waitpid(pid, &status, 0);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
        perror("waitpid");
        return;
    }

    if (WIFEXITED(status)) {
        printf("[Skill 21] Command exited with status %d.\n",
               WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("[Skill 21] Command terminated by signal %d.\n",
               WTERMSIG(status));
    }
}

int main(void)
{
    char input[MAX_INPUT];

    printf("Skill 21: Module Integration and Error Handling\n");
    printf("Commands: run <command>, help, exit\n");

    for (;;) {
        printf("skill21> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (feof(stdin))
                break;

            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }

            perror("fgets");
            clearerr(stdin);
            continue;
        }

        if (strchr(input, '\n') == NULL && !feof(stdin)) {
            int ch;

            while ((ch = getchar()) != '\n' && ch != EOF) {
            }

            fprintf(stderr, "Input too long. Try again.\n");
            continue;
        }

        char *line = trim(input);

        if (*line == '\0')
            continue;

        if (strcmp(line, "exit") == 0)
            break;

        if (strcmp(line, "help") == 0) {
            printf("Available commands:\n");
            printf("  run <command>  Execute an external command\n");
            printf("  help           Display this help message\n");
            printf("  exit           Exit the program\n");
            continue;
        }

        if (strncmp(line, "run", 3) != 0 ||
            (line[3] != '\0' &&
             !isspace((unsigned char)line[3]))) {
            fprintf(stderr,
                    "Syntax error. Use: run <command>, help, or exit\n");
            continue;
        }

        char *command = trim(line + 3);

        if (*command == '\0') {
            fprintf(stderr, "Missing command after 'run'.\n");
            continue;
        }

        char *args[MAX_ARGS];
        int argc = tokenize(command, args);

        if (argc == 0) {
            fprintf(stderr, "No command to execute.\n");
            continue;
        }

        printf("[Skill 21] Executing: %s\n", args[0]);
        fflush(stdout);
        run_command(args);
    }

    printf("Skill 21: Exiting cleanly.\n");
    return EXIT_SUCCESS;
}
