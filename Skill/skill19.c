
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

static volatile sig_atomic_t interrupt_received = 0;
static pid_t worker_pid = -1;

static void handle_sigint(int signo)
{
    (void)signo;
    interrupt_received = 1;
}

static void install_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

static void check_worker(void)
{
    if (worker_pid <= 0)
        return;

    int status;
    pid_t result = waitpid(worker_pid, &status, WNOHANG);

    if (result == worker_pid) {
        if (WIFSIGNALED(status))
            printf("\nWorker terminated by signal %d.\n",
                   WTERMSIG(status));
        else if (WIFEXITED(status))
            printf("\nWorker exited with status %d.\n",
                   WEXITSTATUS(status));

        worker_pid = -1;
    } else if (result == -1 && errno == ECHILD) {
        worker_pid = -1;
    } else if (result == -1 && errno != EINTR) {
        perror("waitpid");
    }
}

static void start_worker(void)
{
    check_worker();

    if (worker_pid > 0) {
        printf("Worker is already running.\n");
        return;
    }

    int logfd = open("skill19_worker.log",
                     O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logfd == -1) {
        perror("open log");
        return;
    }

    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        close(logfd);
        return;
    }

    if (pid == 0) {
        signal(SIGINT, SIG_DFL);

        if (dup2(logfd, STDOUT_FILENO) == -1 ||
            dup2(logfd, STDERR_FILENO) == -1) {
            _exit(EXIT_FAILURE);
        }
        close(logfd);

        printf("Worker PID=%ld started.\n", (long)getpid());
        fflush(stdout);

        for (;;) {
            printf("[Worker PID=%ld] Working...\n",
                   (long)getpid());
            fflush(stdout);
            sleep(2);
        }
    }

    close(logfd);
    worker_pid = pid;
    interrupt_received = 0;

    printf("Worker started: PID=%ld\n", (long)worker_pid);
    printf("Worker output: skill19_worker.log\n");
}

static void show_status(void)
{
    check_worker();

    if (worker_pid > 0)
        printf("Worker PID=%ld State=Running\n", (long)worker_pid);
    else
        printf("State=Not running\n");
}

static void stop_worker(void)
{
    if (worker_pid <= 0) {
        printf("No active worker.\n");
        return;
    }

    pid_t pid = worker_pid;

    if (kill(pid, SIGINT) == -1 && errno != ESRCH) {
        perror("kill SIGINT");
        return;
    }

    while (waitpid(pid, NULL, 0) == -1) {
        if (errno == EINTR)
            continue;
        if (errno != ECHILD)
            perror("waitpid");
        break;
    }

    worker_pid = -1;
    printf("SIGINT sent. Worker stopped; controller remains active.\n");
}

static void cleanup(void)
{
    if (worker_pid <= 0)
        return;

    pid_t pid = worker_pid;

    if (kill(pid, SIGTERM) == -1 && errno != ESRCH)
        perror("kill SIGTERM");

    while (waitpid(pid, NULL, 0) == -1) {
        if (errno == EINTR)
            continue;
        if (errno != ECHILD)
            perror("waitpid");
        break;
    }

    worker_pid = -1;
    printf("Worker cleaned up.\n");
}

int main(void)
{
    char input[100];
    install_handler();

    printf("Skill 19: SIGINT Handling\n");
    printf("Commands: start, status, stop, log, quit\n");

    for (;;) {
        check_worker();

        printf("skill19> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (errno == EINTR || interrupt_received) {
                clearerr(stdin);
                interrupt_received = 0;
                printf("\nController remains active.\n");
                continue;
            }
            break;
        }

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "start") == 0) {
            start_worker();
        } else if (strcmp(input, "status") == 0) {
            show_status();
        } else if (strcmp(input, "stop") == 0) {
            stop_worker();
        } else if (strcmp(input, "log") == 0) {
            printf("--- Worker log ---\n");
            fflush(stdout);
            /* Show the log using an external command. */
            pid_t p = fork();
            if (p == 0) {
                execlp("tail", "tail", "-n", "10",
                       "skill19_worker.log", (char *)NULL);
                perror("tail");
                _exit(127);
            } else if (p > 0) {
                while (waitpid(p, NULL, 0) == -1 &&
                       errno == EINTR) {
                }
            } else {
                perror("fork");
            }
        } else if (strcmp(input, "quit") == 0) {
            break;
        } else if (input[0] != '\0') {
            printf("Unknown command. Use start, status, stop, log, or quit.\n");
        }

        if (interrupt_received) {
            interrupt_received = 0;
            printf("SIGINT received. Controller remains active.\n");
        }
    }

    cleanup();
    printf("Exiting Skill 19.\n");
    return EXIT_SUCCESS;
}
