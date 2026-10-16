#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define MAX_INPUT 256
#define MAX_ARGS 50

void parseCommand(char *input, char *args[]) {
    int i = 0;
    char *token = strtok(input, " ");

    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }

    args[i] = NULL;
}

char *findExecutable(char *command) {
    char *path = getenv("PATH");

    if (path == NULL)
        return NULL;

    char *pathCopy = strdup(path);

    if (pathCopy == NULL)
        return NULL;

    char *directory = strtok(pathCopy, ":");

    while (directory != NULL) {
        static char fullPath[512];

        snprintf(fullPath, sizeof(fullPath),
                 "%s/%s", directory, command);

        if (access(fullPath, X_OK) == 0) {
            free(pathCopy);
            return fullPath;
        }

        directory = strtok(NULL, ":");
    }

    free(pathCopy);
    return NULL;
}

int main() {

    char input[MAX_INPUT];
    char *args[MAX_ARGS];

    printf("=====================================\n");
    printf("     SHELL PROCESS & PATH TEST\n");
    printf("=====================================\n");

    printf("\nEnter command: ");

    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = '\0';

    if (strlen(input) == 0) {
        printf("Empty command\n");
        return 0;
    }

    parseCommand(input, args);

    if (args[0] == NULL) {
        printf("Invalid command\n");
        return 0;
    }

    printf("\nCommand: %s\n", args[0]);

    char *executable = findExecutable(args[0]);

    if (executable == NULL) {
        printf("Command not found in PATH: %s\n", args[0]);
        return 0;
    }

    printf("Executable found: %s\n", executable);
    printf("Execute permission verified.\n");

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        return 1;
    }

    if (pid == 0) {

        printf("\nChild process created.\n");
        printf("Child PID: %d\n", getpid());

        execvp(args[0], args);

        perror("execvp failed");
        exit(EXIT_FAILURE);

    } else {

        int status;

        printf("\nParent process.\n");
        printf("Parent PID: %d\n", getpid());
        printf("Waiting using waitpid()...\n");

        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid failed");
            return 1;
        }

        if (WIFEXITED(status)) {
            printf("Child exited with status: %d\n",
                   WEXITSTATUS(status));
        } else {
            printf("Child did not exit normally.\n");
        }
    }

    printf("Parent process completed.\n");

    return 0;
}
