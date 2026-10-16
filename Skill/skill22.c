
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define OUTPUT_SIZE 16384

typedef struct {
    const char *name;
    const char *input;
    const char *expected;
} TestCase;

static int run_test(const TestCase *test, FILE *report)
{
    char command[256];
    char output[OUTPUT_SIZE];
    size_t used = 0;

    snprintf(command, sizeof(command),
             "printf '%%s' '%s' | ./shellforge 2>&1",
             test->input);

    FILE *pipe = popen(command, "r");

    if (pipe == NULL) {
        fprintf(report, "FAIL: %s - could not start ShellForge\n",
                test->name);
        printf("FAIL: %s - could not start ShellForge\n",
               test->name);
        return 0;
    }

    while (used < sizeof(output) - 1) {
        size_t n = fread(output + used, 1,
                         sizeof(output) - 1 - used, pipe);

        used += n;

        if (n == 0)
            break;
    }

    output[used] = '\0';

    int status = pclose(pipe);

    int passed = status != -1 &&
                 WIFEXITED(status) &&
                 WEXITSTATUS(status) == 0 &&
                 strstr(output, test->expected) != NULL;

    if (passed) {
        printf("PASS: %s\n", test->name);
        fprintf(report, "PASS: %s\n", test->name);
    } else {
        printf("FAIL: %s\n", test->name);
        fprintf(report, "FAIL: %s\n", test->name);
        fprintf(report, "  Expected output to contain: %s\n",
                test->expected);
    }

    return passed;
}

int main(void)
{
    const TestCase tests[] = {
        {
            "Welcome banner",
            "exit\n",
            "WELCOME TO SHELLFORGE"
        },
        {
            "Working directory command",
            "pwd\nexit\n",
            "/home/arnav/OS"
        },
        {
            "Echo command",
            "echo hello\nexit\n",
            "hello"
        },
        {
            "Clean exit message",
            "exit\n",
            "Shell terminated successfully."
        }
    };

    const size_t count = sizeof(tests) / sizeof(tests[0]);

    FILE *report = fopen("skill22_report.txt", "w");

    if (report == NULL) {
        perror("skill22_report.txt");
        return EXIT_FAILURE;
    }

    int passed = 0;

    printf("Skill 22: Automated Testing and Output Verification\n\n");

    for (size_t i = 0; i < count; i++) {
        if (run_test(&tests[i], report))
            passed++;
    }

    fprintf(report, "\nResults: %d/%zu tests passed\n",
            passed, count);

    printf("\nResults: %d/%zu tests passed\n", passed, count);
    printf("Report saved to skill22_report.txt\n");

    fclose(report);

    return passed == (int)count ? EXIT_SUCCESS : EXIT_FAILURE;
}
