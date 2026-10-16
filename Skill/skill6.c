#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_INPUT 256
#define MAX_ARGS 50

void parseCommand(char *input, char *args[]) {
    int i = 0;
    char *token;

    token = strtok(input, " ");

    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }

    args[i] = NULL;
}

void processEscapes(char *input) {
    int i, j = 0;

    for (i = 0; input[i] != '\0'; i++) {

        if (input[i] == '\\' && input[i + 1] != '\0') {
            i++;
            input[j++] = input[i];
        } else {
            input[j++] = input[i];
        }
    }

    input[j] = '\0';
}

int main() {

    char input[MAX_INPUT];
    char *args[MAX_ARGS];

    printf("=====================================\n");
    printf("       SHELL COMMAND EXECUTOR\n");
    printf("=====================================\n");

    printf("\nEnter command: ");

    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = '\0';

    if (strlen(input) == 0) {
        printf("Empty command\n");
        return 0;
    }

    /* Process escape sequences */
    processEscapes(input);

    /* Parse command and arguments */
    parseCommand(input, args);

    if (args[0] == NULL) {
        printf("Invalid command\n");
        return 0;
    }

    printf("\nCommand: %s\n", args[0]);

    printf("Arguments:\n");

    for (int i = 0; args[i] != NULL; i++) {
        printf("Argument %d: %s\n", i + 1, args[i]);
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        return 1;
    }

    if (pid == 0) {

        printf("\nChild process created.\n");
        printf("Child PID: %d\n", getpid());

        execvp(args[0], args);

        perror("Execution failed");
        exit(EXIT_FAILURE);

    } else {

        printf("\nParent process.\n");
        printf("Parent PID: %d\n", getpid());
        printf("Waiting for child process...\n");

        wait(NULL);

        printf("Child process completed.\n");
    }

    return 0;
}
