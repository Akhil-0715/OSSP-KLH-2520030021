#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

void expandVariables(char *input) {
    char result[MAX_INPUT];
    int i = 0, j = 0;

    while (input[i] != '\0' && j < MAX_INPUT - 1) {

        if (input[i] == '$') {
            char variable[100];
            int k = 0;

            i++;

            while ((input[i] >= 'A' && input[i] <= 'Z') ||
                   (input[i] >= 'a' && input[i] <= 'z') ||
                   (input[i] >= '0' && input[i] <= '9') ||
                   input[i] == '_') {

                if (k < 99)
                    variable[k++] = input[i];

                i++;
            }

            variable[k] = '\0';

            char *value = getenv(variable);

            if (value != NULL) {
                for (int x = 0; value[x] != '\0' && j < MAX_INPUT - 1; x++)
                    result[j++] = value[x];
            } else {
                printf("Undefined variable: $%s\n", variable);
            }

        } else {
            result[j++] = input[i++];
        }
    }

    result[j] = '\0';
    strcpy(input, result);
}

void builtin_cd(char *path) {
    if (path == NULL) {
        printf("cd: missing directory\n");
        return;
    }

    if (chdir(path) == 0)
        printf("Directory changed successfully.\n");
    else
        perror("cd");
}

void builtin_pwd() {
    char cwd[MAX_INPUT];

    if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("Current directory: %s\n", cwd);
    else
        perror("pwd");
}

void builtin_echo(char *args[]) {
    for (int i = 1; args[i] != NULL; i++) {
        printf("%s", args[i]);

        if (args[i + 1] != NULL)
            printf(" ");
    }

    printf("\n");
}

int executeBuiltin(char *args[]) {

    if (args[0] == NULL)
        return 1;

    if (strcmp(args[0], "cd") == 0) {
        builtin_cd(args[1]);
        return 1;
    }

    if (strcmp(args[0], "pwd") == 0) {
        builtin_pwd();
        return 1;
    }

    if (strcmp(args[0], "echo") == 0) {
        builtin_echo(args);
        return 1;
    }

    if (strcmp(args[0], "exit") == 0) {
        printf("Shell terminated.\n");
        exit(0);
    }

    return 0;
}

int main() {

    char input[MAX_INPUT];
    char *args[MAX_ARGS];

    printf("=====================================\n");
    printf("       SHELL VARIABLE & BUILTIN\n");
    printf("=====================================\n");

    printf("\nEnter command: ");

    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = '\0';

    if (strlen(input) == 0) {
        printf("Empty command\n");
        return 0;
    }

    expandVariables(input);

    printf("\nExpanded command: %s\n", input);

    parseCommand(input, args);

    if (args[0] == NULL) {
        printf("Invalid command\n");
        return 0;
    }

    if (executeBuiltin(args)) {
        printf("Built-in command executed successfully.\n");
    } else {
        printf("Not a built-in command: %s\n", args[0]);
        printf("Built-in dispatch completed.\n");
    }

    return 0;
}
