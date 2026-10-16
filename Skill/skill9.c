#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#define INPUT_SIZE 256
#define MAX_ARGS 20

char previousDirectory[PATH_MAX] = "";

void parseCommand(char *input, char *args[]) {
    int i = 0;

    char *token = strtok(input, " ");

    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }

    args[i] = NULL;
}

void builtinPwd() {
    char currentDirectory[PATH_MAX];

    if (getcwd(currentDirectory, sizeof(currentDirectory)) != NULL) {
        printf("Current directory: %s\n", currentDirectory);
    } else {
        perror("pwd");
    }
}

void builtinCd(char *path) {
    char currentDirectory[PATH_MAX];

    if (getcwd(currentDirectory, sizeof(currentDirectory)) == NULL) {
        perror("getcwd");
        return;
    }

    /* cd without an argument */
    if (path == NULL || strcmp(path, "") == 0) {

        path = getenv("HOME");

        if (path == NULL) {
            printf("cd: HOME is not set\n");
            return;
        }
    }

    /* cd - : go to previous directory */
    if (strcmp(path, "-") == 0) {

        if (strlen(previousDirectory) == 0) {
            printf("cd: previous directory not available\n");
            return;
        }

        char temp[PATH_MAX];

        strcpy(temp, previousDirectory);

        if (chdir(temp) == -1) {
            perror("cd");
            return;
        }

        strcpy(previousDirectory, currentDirectory);

        printf("Changed to: %s\n", temp);
        return;
    }

    /* Change to requested directory */
    if (chdir(path) == -1) {

        printf("cd: cannot access '%s': %s\n",
               path, strerror(errno));

        return;
    }

    /* Store previous directory */
    strcpy(previousDirectory, currentDirectory);

    printf("Directory changed successfully.\n");
}

int executeBuiltin(char *args[]) {

    if (args[0] == NULL)
        return 1;

    /* cd */
    if (strcmp(args[0], "cd") == 0) {

        builtinCd(args[1]);

        return 1;
    }

    /* pwd */
    if (strcmp(args[0], "pwd") == 0) {

        builtinPwd();

        return 1;
    }

    /* exit */
    if (strcmp(args[0], "exit") == 0) {

        printf("Shell terminated.\n");

        exit(0);
    }

    return 0;
}

int main() {

    char input[INPUT_SIZE];
    char *args[MAX_ARGS];

    char currentDirectory[PATH_MAX];

    printf("=====================================\n");
    printf("       SHELL DIRECTORY MANAGEMENT\n");
    printf("=====================================\n");

    /* Display current directory */
    if (getcwd(currentDirectory, sizeof(currentDirectory)) != NULL) {
        printf("\nCurrent directory: %s\n",
               currentDirectory);
    }

    printf("\nEnter command: ");

    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("Input error.\n");
        return 1;
    }

    input[strcspn(input, "\n")] = '\0';

    if (strlen(input) == 0) {

        printf("Empty command\n");

        return 0;
    }

    /* Parse command */
    parseCommand(input, args);

    if (args[0] == NULL) {

        printf("Invalid command\n");

        return 0;
    }

    /* Execute built-in command */
    if (executeBuiltin(args)) {

        printf("Built-in command processed successfully.\n");

    } else {

        printf("Unknown command: %s\n",
               args[0]);
    }

    return 0;
}
