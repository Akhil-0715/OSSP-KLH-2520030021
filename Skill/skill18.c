
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

typedef enum {
    JOB_NONE,
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} JobState;

static pid_t worker_pid = -1;
static pid_t worker_pgid = -1;
static JobState job_state = JOB_NONE;

static const char *state_name(JobState state)
{
    switch (state) {
        case JOB_NONE:    return "Not started";
        case JOB_RUNNING: return "Running";
        case JOB_STOPPED: return "Stopped";
        case JOB_DONE:    return "Completed";
    }

    return "Unknown";
}

static void update_status(void)
{
    if (worker_pid <= 0 || job_state == JOB_DONE ||
        job_state == JOB_NONE)
        return;

    int status;
    pid_t result;

    do {
        result = waitpid(worker_pid, &status,
                         WNOHANG | WUNTRACED | WCONTINUED);
    } while (result < 0 && errno == EINTR);

    if (result == worker_pid) {
        if (WIFSTOPPED(status)) {
            job_state = JOB_STOPPED;
        } else if (WIFCONTINUED(status)) {
            job_state = JOB_RUNNING;
        } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
            job_state = JOB_DONE;
        }
    } else if (result < 0 && errno == ECHILD) {
        job_state = JOB_DONE;
    } else if (result < 0) {
        perror("waitpid");
    }
}

static void start_worker(void)
{
    if (job_state == JOB_RUNNING || job_state == JOB_STOPPED) {
        printf("A worker is already active.\n");
        return;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        if (setpgid(0, 0) < 0) {
            perror("child setpgid");
            _exit(EXIT_FAILURE);
        }

        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGCONT, SIG_DFL);

        for (;;) {
            printf("[Worker PID=%ld] Working...\n", (long)getpid());
            fflush(stdout);
            sleep(2);
        }
    }

    worker_pid = pid;
    worker_pgid = pid;

    if (setpgid(pid, pid) < 0 &&
        errno != EACCES && errno != ESRCH) {
        perror("parent setpgid");
    }

    job_state = JOB_RUNNING;

    printf("Worker started: PID=%ld, PGID=%ld\n",
           (long)worker_pid, (long)worker_pgid);
}

static void stop_worker(void)
{
    update_status();

    if (job_state != JOB_RUNNING) {
        printf("No running worker to stop. Current state: %s\n",
               state_name(job_state));
        return;
    }

    if (kill(-worker_pgid, SIGSTOP) < 0) {
        perror("SIGSTOP");
        return;
    }

    int status;
    pid_t result;

    do {
        result = waitpid(worker_pid, &status, WUNTRACED);
    } while (result < 0 && errno == EINTR);

    if (result == worker_pid && WIFSTOPPED(status)) {
        job_state = JOB_STOPPED;
        printf("Worker stopped using SIGSTOP.\n");
    } else if (result < 0) {
        perror("waitpid");
    }
}

static void resume_worker(void)
{
    update_status();

    if (job_state != JOB_STOPPED) {
        printf("No stopped worker to resume. Current state: %s\n",
               state_name(job_state));
        return;
    }

    if (kill(-worker_pgid, SIGCONT) < 0) {
        perror("SIGCONT");
        return;
    }

    job_state = JOB_RUNNING;
    printf("SIGCONT sent. Worker resumed.\n");
}

static void show_status(void)
{
    update_status();

    printf("\n--- Job Status ---\n");
    printf("PID:   %ld\n", (long)worker_pid);
    printf("PGID:  %ld\n", (long)worker_pgid);
    printf("State: %s\n", state_name(job_state));
    printf("------------------\n");
}

static void cleanup(void)
{
    if (worker_pid <= 0 || job_state == JOB_DONE ||
        job_state == JOB_NONE)
        return;

    if (kill(-worker_pgid, SIGTERM) < 0 && errno != ESRCH)
        perror("SIGTERM");

    while (waitpid(worker_pid, NULL, 0) < 0) {
        if (errno == EINTR)
            continue;

        if (errno != ECHILD)
            perror("waitpid");

        break;
    }

    job_state = JOB_DONE;
    printf("Worker cleaned up.\n");
}

int main(void)
{
    char input[100];

    printf("Skill 18: Signals and Stopped-Job Recovery\n");
    printf("Commands: start, stop, resume, status, quit\n");

    for (;;) {
        printf("skill18> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "start") == 0) {
            start_worker();
        } else if (strcmp(input, "stop") == 0) {
            stop_worker();
        } else if (strcmp(input, "resume") == 0) {
            resume_worker();
        } else if (strcmp(input, "status") == 0) {
            show_status();
        } else if (strcmp(input, "quit") == 0) {
            break;
        } else if (input[0] != '\0') {
            printf("Unknown command. Use start, stop, resume, status, or quit.\n");
        }
    }

    cleanup();
    printf("Exiting Skill 18.\n");
    return EXIT_SUCCESS;
}
