#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>

#define INPUT_SIZE 256
#define MAX_ARGS 20

void showCurrentDirectory() {
    char currentDirectory[PATH_MAX];

    if (getcwd(currentDirectory, sizeof(currentDirectory)) != NULL)
        printf("Current directory: %s\n", currentDirectory);
    else
        perror("getcwd");
}

int validVariableName(const char *name) {
    if (name == NULL || name[0] == '\0')
        return 0;

    if (!isalpha((unsigned char)name[0]) && name[0] != '_')
        return 0;

    for (int i = 1; name[i] != '\0'; i++) {
        if (!isalnum((unsigned char)name[i]) && name[i] != '_')
            return 0;
    }

    return 1;
}

void handleExport(char *argument) {

    if (argument == NULL || strlen(argument) == 0) {
        printf("export: missing variable\n");
        return;
    }

    char *equals = strchr(argument, '=');

    if (equals == NULL) {
        printf("export: invalid syntax\n");
        printf("Use: export NAME=value\n");
        return;
    }

    int nameLength = equals - argument;

    if (nameLength <= 0 || nameLength >= 100) {
        printf("export: invalid variable name\n");
        return;
    }

    char name[100];

    strncpy(name, argument, nameLength);
    name[nameLength] = '\0';

    char *value = equals + 1;

    if (!validVariableName(name)) {
        printf("export: invalid variable name: %s\n", name);
        return;
    }

    if (setenv(name, value, 1) != 0) {
        perror("export");
        return;
    }

    printf("Environment variable exported successfully.\n");
    printf("%s=%s\n", name, getenv(name));
}

void parseCommand(char *input, char *args[]) {

    int i = 0;

    char *token = strtok(input, " ");

    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }

    args[i] = NULL;
}

int main() {

    char input[INPUT_SIZE];
    char *args[MAX_ARGS];

    printf("=====================================\n");
    printf("      SHELL ENVIRONMENT MANAGER\n");
    printf("=====================================\n");

    showCurrentDirectory();

    printf("\nEnter command: ");

    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("Input error.\n");
        return 1;
    }

    input[strcspn(input, "\n")] = '\0';

    if (strlen(input) == 0) {
        printf("Empty command.\n");
        return 0;
    }

    parseCommand(input, args);

    if (args[0] == NULL)
        return 0;

    if (strcmp(args[0], "pwd") == 0) {

        showCurrentDirectory();

    } else if (strcmp(args[0], "export") == 0) {

        handleExport(args[1]);

    } else if (strcmp(args[0], "exit") == 0) {

        printf("Cleaning up resources...\n");
        printf("Shell terminated.\n");

        return 0;

    } else {

        printf("Unknown command: %s\n", args[0]);
    }

    return 0;
}
