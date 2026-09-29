#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_INPUT 200
#define MAX_ARGS 50
#define MAX_HISTORY 20

char *history[MAX_HISTORY];
int history_count = 0;


/* ---------------- HISTORY ---------------- */

void add_history(char *command)
{
    if (strlen(command) == 0)
        return;

    if (history_count < MAX_HISTORY)
    {
        history[history_count] = malloc(strlen(command) + 1);
        strcpy(history[history_count], command);
        history_count++;
    }
}


void show_history()
{
    for (int i = 0; i < history_count; i++)
    {
        printf("%d: %s\n", i + 1, history[i]);
    }
}


/* ---------------- PARSER ---------------- */

int parse_command(char *input, char *args[])
{
    int count = 0;
    int i = 0;

    while (input[i] != '\0')
    {
        /* Skip spaces */
        while (input[i] == ' ')
            i++;

        if (input[i] == '\0')
            break;

        char *word = malloc(MAX_INPUT);
        int j = 0;

        /* Single quotes */
        if (input[i] == '\'')
        {
            i++;

            while (input[i] != '\'' && input[i] != '\0')
            {
                word[j++] = input[i++];
            }

            if (input[i] == '\'')
                i++;
        }

        /* Double quotes */
        else if (input[i] == '"')
        {
            i++;

            while (input[i] != '"' && input[i] != '\0')
            {
                if (input[i] == '\\' && input[i + 1] != '\0')
                {
                    i++;
                    word[j++] = input[i++];
                }
                else
                {
                    word[j++] = input[i++];
                }
            }

            if (input[i] == '"')
                i++;
        }

        /* Normal word */
        else
        {
            while (input[i] != ' ' && input[i] != '\0')
            {
                /* Escape character */
                if (input[i] == '\\' && input[i + 1] != '\0')
                {
                    i++;
                    word[j++] = input[i++];
                }
                else
                {
                    word[j++] = input[i++];
                }
            }
        }

        word[j] = '\0';

        args[count++] = word;

        if (count >= MAX_ARGS - 1)
            break;
    }

    args[count] = NULL;

    return count;
}


/* ---------------- EXECUTION ---------------- */

void execute_command(char *args[])
{
    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork");
        return;
    }

    /* CHILD PROCESS */
    if (pid == 0)
    {
        execvp(args[0], args);

        perror("Command not found");
        exit(1);
    }

    /* PARENT PROCESS */
    else
    {
        wait(NULL);
    }
}


/* ---------------- MAIN ---------------- */

int main()
{
    char input[MAX_INPUT];
    char *args[MAX_ARGS];

    while (1)
    {
        printf("akhil$> ");
        fflush(stdout);

        /* Read input */
        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        /* Remove newline */
        input[strcspn(input, "\n")] = '\0';

        /* Empty command */
        if (strlen(input) == 0)
            continue;


        /* EXIT */
        if (strcmp(input, "exit") == 0)
        {
            break;
        }


        /* HISTORY */
        if (strcmp(input, "history") == 0)
        {
            show_history();
            continue;
        }


        /* Add command to history */
        add_history(input);


        /* Parse command */
        int count = parse_command(input, args);

        if (count == 0)
            continue;


        /* ---------------- CD BUILT-IN ---------------- */

        if (strcmp(args[0], "cd") == 0)
        {
            if (args[1] == NULL)
            {
                printf("cd: missing directory\n");
            }
            else
            {
                if (chdir(args[1]) != 0)
                {
                    perror("cd");
                }
            }

            /* Free arguments */
            for (int i = 0; i < count; i++)
            {
                free(args[i]);
            }

            continue;
        }


        /* Execute normal command */
        execute_command(args);


        /* Free arguments */
        for (int i = 0; i < count; i++)
        {
            free(args[i]);
        }
    }


    /* Free history memory */
    for (int i = 0; i < history_count; i++)
    {
        free(history[i]);
    }

    return 0;
}
