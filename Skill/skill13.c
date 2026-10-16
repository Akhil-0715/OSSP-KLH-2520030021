#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_INPUT 512
#define MAX_ARGS 64

int main(void)
{
    char input[MAX_INPUT];

    printf("Skill 13: Input and Output Redirection\n");
    printf("Examples: ls > files.txt\n");
    printf("          cat < files.txt\n");
    printf("Type exit to quit.\n");

    while (1) {
        printf("skill13> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "exit") == 0)
            break;

        if (input[0] == '\0')
            continue;

        char *args[MAX_ARGS];
        int argc = 0;
        char *input_file = NULL;
        char *output_file = NULL;
        int invalid = 0;

        char *saveptr = NULL;
        char *token = strtok_r(input, " \t", &saveptr);

        while (token != NULL) {
            if (strcmp(token, "<") == 0 ||
                strcmp(token, ">") == 0) {

                int is_input = (strcmp(token, "<") == 0);
                char *filename = strtok_r(NULL, " \t", &saveptr);

                if (filename == NULL ||
                    strcmp(filename, "<") == 0 ||
                    strcmp(filename, ">") == 0) {
                    fprintf(stderr, "Error: missing filename.\n");
                    invalid = 1;
                    break;
                }

                if (is_input) {
                    if (input_file != NULL) {
                        fprintf(stderr, "Error: duplicate input redirection.\n");
                        invalid = 1;
                        break;
                    }
                    input_file = filename;
                } else {
                    if (output_file != NULL) {
                        fprintf(stderr, "Error: duplicate output redirection.\n");
                        invalid = 1;
                        break;
                    }
                    output_file = filename;
                }
            } else {
                if (argc >= MAX_ARGS - 1) {
                    fprintf(stderr, "Error: too many arguments.\n");
                    invalid = 1;
                    break;
                }
                args[argc++] = token;
            }

            token = strtok_r(NULL, " \t", &saveptr);
        }

        args[argc] = NULL;

        if (invalid)
            continue;

        if (argc == 0) {
            fprintf(stderr, "Error: no command specified.\n");
            continue;
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            continue;
        }

        if (pid == 0) {
            if (input_file != NULL) {
                int fd = open(input_file, O_RDONLY);

                if (fd < 0) {
                    perror(input_file);
                    _exit(1);
                }

                if (dup2(fd, STDIN_FILENO) < 0) {
                    perror("dup2 input");
                    close(fd);
                    _exit(1);
                }

                close(fd);
            }

            if (output_file != NULL) {
                int fd = open(output_file,
                              O_WRONLY | O_CREAT | O_TRUNC,
                              0644);

                if (fd < 0) {
                    perror(output_file);
                    _exit(1);
                }

                if (dup2(fd, STDOUT_FILENO) < 0) {
                    perror("dup2 output");
                    close(fd);
                    _exit(1);
                }

                close(fd);
            }

            execvp(args[0], args);
            perror(args[0]);
            _exit(127);
        }

        int status;
        while (waitpid(pid, &status, 0) < 0) {
            perror("waitpid");
            break;
        }
    }

    printf("Exiting Skill 13.\n");
    return 0;
}
