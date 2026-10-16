
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#define STAGES 4
#define CONCURRENT_JOBS 3
#define STRESS_ROUNDS 10

typedef struct {
    const char *name;
    char *const *args;
} Command;

static FILE *report_file;

static double elapsed_seconds(struct timespec start,
                              struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

static void log_result(const char *name, int passed,
                       double seconds)
{
    printf("[%s] %s (%.4f seconds)\n",
           passed ? "PASS" : "FAIL", name, seconds);

    if (report_file != NULL) {
        fprintf(report_file, "[%s] %s (%.4f seconds)\n",
                passed ? "PASS" : "FAIL", name, seconds);
    }
}

/* Run a four-stage pipeline and capture its final output. */
static int run_pipeline(char *output, size_t output_size,
                        double *duration)
{
    char *printf_args[] = {
        "printf", "apple\nbanana\napple\n", NULL
    };
    char *grep_args[] = {"grep", "apple", NULL};
    char *tr_args[] = {"tr", "a-z", "A-Z", NULL};
    char *wc_args[] = {"wc", "-l", NULL};

    Command commands[STAGES] = {
        {"printf", printf_args},
        {"grep", grep_args},
        {"tr", tr_args},
        {"wc", wc_args}
    };

    int pipes[STAGES][2];
    pid_t pids[STAGES];
    int created = 0;
    int started = 0;
    int failed = 0;
    struct timespec start, end;

    memset(pids, 0, sizeof(pids));
    output[0] = '\0';

    for (int i = 0; i < STAGES; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            for (int j = 0; j < created; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return 0;
        }
        created++;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < STAGES; i++) {
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");
            failed = 1;
            break;
        }

        if (pid == 0) {
            if (i > 0 &&
                dup2(pipes[i - 1][0], STDIN_FILENO) == -1) {
                perror("dup2 stdin");
                _exit(126);
            }

            if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                perror("dup2 stdout");
                _exit(126);
            }

            for (int j = 0; j < STAGES; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execvp(commands[i].args[0], commands[i].args);
            perror(commands[i].name);
            _exit(127);
        }

        pids[i] = pid;
        started++;
    }

    /* Parent does not write to the pipeline. */
    for (int i = 0; i < STAGES; i++) {
        close(pipes[i][1]);
    }

    /* Only the final stage's output is needed. */
    for (int i = 0; i < STAGES - 1; i++) {
        close(pipes[i][0]);
    }

    if (!failed) {
        size_t used = 0;
        char buffer[256];
        ssize_t count;

        while ((count = read(pipes[STAGES - 1][0],
                             buffer, sizeof(buffer))) > 0) {
            size_t available = output_size - used - 1;
            size_t copy_count = (size_t)count;

            if (copy_count > available) {
                copy_count = available;
            }

            if (copy_count > 0) {
                memcpy(output + used, buffer, copy_count);
                used += copy_count;
                output[used] = '\0';
            }
        }

        if (count == -1) {
            perror("read");
            failed = 1;
        }
    } else {
        /* Drain the output pipe before waiting for children. */
        char discard[256];
        while (read(pipes[STAGES - 1][0],
                    discard, sizeof(discard)) > 0) {
        }
    }

    close(pipes[STAGES - 1][0]);

    for (int i = 0; i < started; i++) {
        int status;
        pid_t result;

        do {
            result = waitpid(pids[i], &status, 0);
        } while (result == -1 && errno == EINTR);

        if (result == -1 ||
            !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            failed = 1;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    *duration = elapsed_seconds(start, end);

    if (started != STAGES) {
        failed = 1;
    }

    return !failed;
}

/* Launch independent sleep jobs concurrently and wait for them. */
static int test_concurrent_jobs(double *duration)
{
    pid_t pids[CONCURRENT_JOBS];
    int started = 0;
    int failed = 0;
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < CONCURRENT_JOBS; i++) {
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork concurrent job");
            failed = 1;
            break;
        }

        if (pid == 0) {
            execlp("sleep", "sleep", "1", (char *)NULL);
            perror("sleep");
            _exit(127);
        }

        pids[started++] = pid;
    }

    for (int i = 0; i < started; i++) {
        int status;
        pid_t result;

        do {
            result = waitpid(pids[i], &status, 0);
        } while (result == -1 && errno == EINTR);

        if (result == -1 ||
            !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            failed = 1;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    *duration = elapsed_seconds(start, end);

    return !failed && started == CONCURRENT_JOBS;
}

static void write_architecture(void)
{
    FILE *file = fopen("skill23_architecture.txt", "w");

    if (file == NULL) {
        perror("skill23_architecture.txt");
        return;
    }

    fprintf(file,
        "SKILL 23 - SHELLFORGE PIPELINE AND RESOURCE TESTING\n\n"
        "ARCHITECTURE\n"
        "-------------\n"
        "Pipeline input -> printf -> grep -> tr -> wc -> Parent\n\n"
        "Execution flow:\n"
        "1. Create pipes using pipe().\n"
        "2. Create child processes using fork().\n"
        "3. Connect standard input/output using dup2().\n"
        "4. Replace child programs using execvp()/execlp().\n"
        "5. Close unused file descriptors using close().\n"
        "6. Collect output using read().\n"
        "7. Wait for child completion using waitpid().\n"
        "8. Measure elapsed time using clock_gettime().\n"
        "9. Measure child resource usage using getrusage().\n"
        "10. Record test results in skill23_report.txt.\n\n"
        "ERROR HANDLING\n"
        "--------------\n"
        "Pipe/fork failures are reported. Child exit statuses are checked.\n"
        "Interrupted waitpid() calls are retried.\n\n"
        "DESIGN DECISIONS\n"
        "----------------\n"
        "The pipeline uses four fixed commands for repeatable tests.\n"
        "Concurrent jobs are independent sleep processes.\n"
        "Ten pipeline rounds are used for a basic stability check.\n"
        "These tests are a baseline, not a complete shell benchmark.\n");

    fclose(file);
    printf("Architecture document: skill23_architecture.txt\n");
}

int main(void)
{
    char output[1024];
    double seconds = 0.0;
    int passed = 0;
    int total = 0;
    struct rusage usage;

    report_file = fopen("skill23_report.txt", "w");

    if (report_file == NULL) {
        perror("skill23_report.txt");
        return EXIT_FAILURE;
    }

    printf("\n========================================\n");
    printf(" SHELLFORGE - SKILL 23 TEST SUITE\n");
    printf("========================================\n\n");

    fprintf(report_file,
            "SHELLFORGE SKILL 23 TEST REPORT\n"
            "================================\n\n");

    /* Test 1: Four-stage pipeline and output validation. */
    total++;
    int ok = run_pipeline(output, sizeof(output), &seconds);
    char *endptr;
    long line_count = strtol(output, &endptr, 10);

    while (isspace((unsigned char)*endptr)) {
        endptr++;
    }

    ok = ok && endptr != output && *endptr == '\0'
         && line_count == 2;

    log_result("Four-stage pipeline and output validation",
               ok, seconds);
    passed += ok;

    if (!ok) {
        fprintf(report_file, "Pipeline output received: %s\n", output);
    }

    /* Test 2: Multiple jobs running concurrently. */
    total++;
    ok = test_concurrent_jobs(&seconds);
    log_result("Three concurrent child jobs", ok, seconds);
    passed += ok;

    /* Test 3: Repeated pipeline stability test. */
    total++;
    struct timespec stress_start, stress_end;
    int stress_ok = 1;

    clock_gettime(CLOCK_MONOTONIC, &stress_start);

    for (int i = 0; i < STRESS_ROUNDS; i++) {
        double round_seconds;

        if (!run_pipeline(output, sizeof(output), &round_seconds)) {
            stress_ok = 0;
            fprintf(report_file,
                    "Stress round %d failed.\n", i + 1);
        } else {
            char *p = output;
            while (isspace((unsigned char)*p)) {
                p++;
            }

            if (*p != '2') {
                stress_ok = 0;
                fprintf(report_file,
                        "Stress round %d had unexpected output: %s\n",
                        i + 1, output);
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &stress_end);
    seconds = elapsed_seconds(stress_start, stress_end);

    log_result("Ten repeated pipeline rounds",
               stress_ok, seconds);
    passed += stress_ok;

    /* Test 4: Collect process resource statistics. */
    total++;
    if (getrusage(RUSAGE_CHILDREN, &usage) == 0) {
        printf("[PASS] Child resource statistics collected\n");
        fprintf(report_file,
                "[PASS] Child resource statistics collected\n");
        printf("  User CPU time:   %ld.%06ld seconds\n",
               (long)usage.ru_utime.tv_sec,
               (long)usage.ru_utime.tv_usec);
        printf("  System CPU time: %ld.%06ld seconds\n",
               (long)usage.ru_stime.tv_sec,
               (long)usage.ru_stime.tv_usec);
        printf("  Max RSS:         %ld (platform-defined units)\n",
               usage.ru_maxrss);

        fprintf(report_file,
                "User CPU time: %ld.%06ld seconds\n"
                "System CPU time: %ld.%06ld seconds\n"
                "Max RSS: %ld (platform-defined units)\n",
                (long)usage.ru_utime.tv_sec,
                (long)usage.ru_utime.tv_usec,
                (long)usage.ru_stime.tv_sec,
                (long)usage.ru_stime.tv_usec,
                usage.ru_maxrss);
        passed++;
    } else {
        perror("getrusage");
        fprintf(report_file,
                "[FAIL] Could not collect child resource statistics\n");
        printf("[FAIL] Child resource statistics collection\n");
    }

    fprintf(report_file, "\nResults: %d/%d checks passed\n",
            passed, total);
    printf("\n========================================\n");
    printf(" RESULTS: %d/%d CHECKS PASSED\n", passed, total);
    printf("========================================\n");
    printf("Report: skill23_report.txt\n");

    write_architecture();
    fclose(report_file);

    return passed == total ? EXIT_SUCCESS : EXIT_FAILURE;
}
