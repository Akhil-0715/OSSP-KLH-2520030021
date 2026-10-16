
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
static volatile sig_atomic_t sigtstp_received = 0;

static void handle_sigtstp(int signo)
{
    (void)signo;
    sigtstp_received = 1;
}

static void install_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handle_sigtstp;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGTSTP, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

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
    if (worker_pid <= 0 ||
        job_state == JOB_DONE ||
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
            printf("\nWorker stopped by signal %d.\n",
                   WSTOPSIG(status));
        } else if (WIFCONTINUED(status)) {
            job_state = JOB_RUNNING;
            printf("\nWorker continued.\n");
        } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
            job_state = JOB_DONE;
            printf("\nWorker completed.\n");
            worker_pid = -1;
            worker_pgid = -1;
        }
    } else if (result < 0 && errno == ECHILD) {
        job_state = JOB_DONE;
        worker_pid = -1;
        worker_pgid = -1;
    } else if (result < 0) {
        perror("waitpid");
    }
}

static void start_worker(void)
{
    update_status();

    if (job_state == JOB_RUNNING || job_state == JOB_STOPPED) {
        printf("Worker already exists. State: %s\n",
               state_name(job_state));
        return;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        if (setpgid(0, 0) < 0)
            _exit(EXIT_FAILURE);

        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);

        sigaction(SIGTSTP, &sa, NULL);
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);

        printf("[Worker %ld] Started.\n", (long)getpid());
        fflush(stdout);

        for (;;) {
            printf("[Worker %ld] Working...\n", (long)getpid());
            fflush(stdout);
            sleep(2);
        }
    }

    if (setpgid(pid, pid) < 0 &&
        errno != EACCES && errno != ESRCH)
        perror("setpgid");

    worker_pid = pid;
    worker_pgid = pid;
    job_state = JOB_RUNNING;

    printf("Worker started: PID=%ld PGID=%ld\n",
           (long)worker_pid, (long)worker_pgid);
}

static void stop_worker(void)
{
    update_status();

    if (job_state != JOB_RUNNING) {
        printf("Cannot stop worker. State: %s\n",
               state_name(job_state));
        return;
    }

    if (kill(-worker_pgid, SIGTSTP) < 0) {
        perror("kill SIGTSTP");
        return;
    }

    int status;
    pid_t result;

    do {
        result = waitpid(worker_pid, &status, WUNTRACED);
    } while (result < 0 && errno == EINTR);

    if (result == worker_pid && WIFSTOPPED(status)) {
        job_state = JOB_STOPPED;
        printf("Worker suspended by SIGTSTP.\n");
    } else if (result == worker_pid &&
               (WIFEXITED(status) || WIFSIGNALED(status))) {
        job_state = JOB_DONE;
        worker_pid = -1;
        worker_pgid = -1;
        printf("Worker exited.\n");
    } else if (result < 0) {
        perror("waitpid");
    }
}

static void resume_worker(void)
{
    update_status();

    if (job_state != JOB_STOPPED) {
        printf("No stopped worker. State: %s\n",
               state_name(job_state));
        return;
    }

    if (kill(-worker_pgid, SIGCONT) < 0) {
        perror("kill SIGCONT");
        return;
    }

    job_state = JOB_RUNNING;
    printf("SIGCONT sent. Worker resumed.\n");
}

static void show_status(void)
{
    update_status();

    printf("\n--- Job Table ---\n");
    printf("PID:   %ld\n", (long)worker_pid);
    printf("PGID:  %ld\n", (long)worker_pgid);
    printf("State: %s\n", state_name(job_state));
    printf("-----------------\n");
}

static void cleanup(void)
{
    if (worker_pid <= 0)
        return;

    pid_t pid = worker_pid;
    pid_t pgid = worker_pgid;

    if (job_state == JOB_STOPPED)
        kill(-pgid, SIGCONT);

    if (kill(-pgid, SIGTERM) < 0 && errno != ESRCH)
        perror("kill SIGTERM");

    while (waitpid(pid, NULL, 0) < 0) {
        if (errno == EINTR)
            continue;
        if (errno != ECHILD)
            perror("waitpid");
        break;
    }

    worker_pid = -1;
    worker_pgid = -1;
    job_state = JOB_DONE;
    printf("Worker cleaned up.\n");
}

int main(void)
{
    char input[100];

    install_handler();

    printf("Skill 20: SIGTSTP and Process Groups\n");
    printf("Commands: start, stop, resume, status, quit\n");

    for (;;) {
        update_status();

        if (sigtstp_received) {
            sigtstp_received = 0;
            printf("\nSIGTSTP received by controller; controller remains active.\n");
        }

        printf("skill20> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (errno == EINTR || sigtstp_received) {
                clearerr(stdin);
                continue;
            }
            break;
        }

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "start") == 0)
            start_worker();
        else if (strcmp(input, "stop") == 0)
            stop_worker();
        else if (strcmp(input, "resume") == 0)
            resume_worker();
        else if (strcmp(input, "status") == 0)
            show_status();
        else if (strcmp(input, "quit") == 0)
            break;
        else if (input[0] != '\0')
            printf("Unknown command.\n");
    }

    cleanup();
    printf("Exiting Skill 20.\n");
    return EXIT_SUCCESS;
}
