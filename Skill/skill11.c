#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HISTORY 100
#define MAX_COMMANDS 20
#define MAX_LENGTH 256

typedef struct {
    char *commands[MAX_HISTORY];
    int count;
} CommandHistory;

typedef struct {
    char *commands[MAX_COMMANDS];
    int count;
} Pipeline;

void initHistory(CommandHistory *history) {
    history->count = 0;
}

void addHistory(CommandHistory *history, const char *command) {
    if (command == NULL || strlen(command) == 0)
        return;

    if (history->count == MAX_HISTORY) {
        free(history->commands[0]);
        for (int i = 1; i < MAX_HISTORY; i++)
            history->commands[i - 1] = history->commands[i];
        history->count--;
    }

    history->commands[history->count] = malloc(strlen(command) + 1);
    if (!history->commands[history->count]) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    strcpy(history->commands[history->count], command);
    history->count++;
}

void displayHistory(const CommandHistory *history) {
    for (int i = 0; i < history->count; i++)
        printf("%d  %s\n", i + 1, history->commands[i]);

    if (history->count == 0)
        printf("No commands in history.\n");
}

void freeHistory(CommandHistory *history) {
    for (int i = 0; i < history->count; i++)
        free(history->commands[i]);

    history->count = 0;
}

int addPipelineCommand(Pipeline *pipeline, const char *command) {
    if (!command || !*command || pipeline->count >= MAX_COMMANDS)
        return 0;

    pipeline->commands[pipeline->count] = malloc(strlen(command) + 1);
    if (!pipeline->commands[pipeline->count]) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    strcpy(pipeline->commands[pipeline->count], command);
    pipeline->count++;
    return 1;
}

int validatePipeline(const Pipeline *pipeline) {
    if (pipeline->count == 0)
        return 0;

    for (int i = 0; i < pipeline->count; i++) {
        if (!pipeline->commands[i] || !pipeline->commands[i][0])
            return 0;
    }

    return 1;
}

void displayPipeline(const Pipeline *pipeline) {
    if (!validatePipeline(pipeline)) {
        printf("Invalid pipeline.\n");
        return;
    }

    printf("Pipeline execution order:\n");

    for (int i = 0; i < pipeline->count; i++) {
        printf("%d. %s", i + 1, pipeline->commands[i]);

        if (i < pipeline->count - 1)
            printf(" |");

        printf("\n");
    }
}

void freePipeline(Pipeline *pipeline) {
    for (int i = 0; i < pipeline->count; i++)
        free(pipeline->commands[i]);

    pipeline->count = 0;
}

int main(void) {
    CommandHistory history;
    Pipeline pipeline = { .count = 0 };
    char input[MAX_LENGTH];

    initHistory(&history);

    printf("Skill 11: Command History and Pipeline Structures\n");
    printf("Enter commands for history (type 'done' to finish):\n");

    while (1) {
        printf("history> ");
        if (!fgets(input, sizeof(input), stdin))
            break;

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "done") == 0)
            break;

        addHistory(&history, input);
    }

    printf("\nCommand History:\n");
    displayHistory(&history);

    printf("\nEnter commands for pipeline (type 'done' to finish):\n");

    while (1) {
        printf("pipeline> ");
        if (!fgets(input, sizeof(input), stdin))
            break;

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "done") == 0)
            break;

        if (!addPipelineCommand(&pipeline, input))
            printf("Cannot add command. Pipeline is full or input is empty.\n");
    }

    printf("\n");
    displayPipeline(&pipeline);

    freeHistory(&history);
    freePipeline(&pipeline);

    return 0;
}
