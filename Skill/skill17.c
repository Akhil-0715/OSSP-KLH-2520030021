
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <errno.h>

#define MAX_INPUT 1024
#define MAX_JOBS 64
#define MAX_ARGS 64

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} JobState;

typedef struct {
    int id;
    pid_t pid;
    pid_t pgid;
    char command[MAX_INPUT];
    JobState state;
} Job;

static Job jobs[MAX_JOBS];
static int job_count = 0;
static int next_job_id = 1;
static pid_t shell_pgid;
static int shell_terminal;
static struct termios shell_modes;

static void initShell(void)
{
    shell_terminal = STDIN_FILENO;

    /* Wait until this shell owns the terminal. */
    while (tcgetpgrp(shell_terminal) !=
           (shell_pgid = getpgrp())) {
        kill(-shell_pgid, SIGTTIN);
    }

    shell_pgid = getpid();

    if (setpgid(shell_pgid, shell_pgid) < 0 &&
        errno != EACCES && errno != EPERM) {
        perror("setpgid");
    }

    shell_pgid = getpgrp();

    if (tcsetpgrp(shell_terminal, shell_pgid) < 0)
        perror("tcsetpgrp");

    if (tcgetattr(shell_terminal, &shell_modes) < 0)
        perror("tcgetattr");

    /* The shell handles terminal signals separately from jobs. */
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
}

static const char *stateName(JobState state)
{
    switch (state) {
        case JOB_RUNNING: return "Running";
        case JOB_STOPPED: return "Stopped";
        case JOB_DONE:    return "Done";
    }

    return "Unknown";
}

static int findJobIndex(int id)
{
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].id == id && jobs[i].state != JOB_DONE)
            return i;
    }

    return -1;
}

static int addJob(pid_t pid, pid_t pgid,
                  const char *command, JobState state)
{
    if (job_count >= MAX_JOBS) {
        fprintf(stderr, "Job table is full.\n");
        return -1;
    }

    Job *job = &jobs[job_count];

    job->id = next_job_id++;
    job->pid = pid;
    job->pgid = pgid;
    job->state = state;

    snprintf(job->command, sizeof(job->command),
             "%s", command);

    job_count++;
    return job_count - 1;
}

static void removeDoneJobs(void)
{
    int write_index = 0;

    for (int i = 0; i < job_count; i++) {
        if (jobs[i].state != JOB_DONE) {
            if (write_index != i)
                jobs[write_index] = jobs[i];

            write_index++;
        }
    }

    job_count = write_index;
}

static void updateJobs(void)
{
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].state == JOB_DONE)
            continue;

        int status;
        pid_t result;

        do {
            result = waitpid(jobs[i].pid, &status,
                             WNOHANG | WUNTRACED | WCONTINUED);
        } while (result < 0 && errno == EINTR);

        if (result == jobs[i].pid) {
            if (WIFSTOPPED(status)) {
                jobs[i].state = JOB_STOPPED;
                printf("\n[%d] Stopped  %s\n",
                       jobs[i].id, jobs[i].command);
            } else if (WIFCONTINUED(status)) {
                jobs[i].state = JOB_RUNNING;
            } else if (WIFEXITED(status) ||
                       WIFSIGNALED(status)) {
                jobs[i].state = JOB_DONE;
                printf("\n[%d] Done  %s\n",
                       jobs[i].id, jobs[i].command);
            }
        } else if (result < 0 && errno == ECHILD) {
            jobs[i].state = JOB_DONE;
        } else if (result < 0) {
            perror("waitpid");
        }
    }
}

static void listJobs(void)
{
    updateJobs();

    int found = 0;

    for (int i = 0; i < job_count; i++) {
        if (jobs[i].state == JOB_DONE)
            continue;

        printf("[%d] %-7s PID=%ld PGID=%ld  %s\n",
               jobs[i].id,
               stateName(jobs[i].state),
               (long)jobs[i].pid,
               (long)jobs[i].pgid,
               jobs[i].command);

        found = 1;
    }

    if (!found)
        printf("No active jobs.\n");

    removeDoneJobs();
}

static void giveTerminalTo(pid_t pgid)
{
    if (tcsetpgrp(shell_terminal, pgid) < 0)
        perror("tcsetpgrp");
}

static void restoreShellTerminal(void)
{
    giveTerminalTo(shell_pgid);

    if (tcsetattr(shell_terminal, TCSADRAIN,
                  &shell_modes) < 0) {
        perror("tcsetattr");
    }
}

static void waitForForeground(int index)
{
    Job *job = &jobs[index];
    int status;

    giveTerminalTo(job->pgid);

    if (job->state == JOB_STOPPED) {
        if (kill(-job->pgid, SIGCONT) < 0)
            perror("SIGCONT");
        else
            job->state = JOB_RUNNING;
    }

    while (job->state == JOB_RUNNING) {
        pid_t result = waitpid(job->pid, &status,
                               WUNTRACED);

        if (result < 0) {
            if (errno == EINTR)
                continue;

            if (errno == ECHILD) {
                job->state = JOB_DONE;
                break;
            }

            perror("waitpid");
            break;
        }

        if (WIFSTOPPED(status)) {
            job->state = JOB_STOPPED;
            printf("\n[%d] Stopped  %s\n",
                   job->id, job->command);
            break;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            job->state = JOB_DONE;
            break;
        }
    }

    restoreShellTerminal();

    if (job->state == JOB_DONE)
        printf("[%d] Completed  %s\n",
               job->id, job->command);
}

static void foregroundJob(int id)
{
    updateJobs();

    int index = findJobIndex(id);

    if (index < 0) {
        fprintf(stderr, "fg: no such active job: %d\n", id);
        removeDoneJobs();
        return;
    }

    printf("%s\n", jobs[index].command);
    waitForForeground(index);

    removeDoneJobs();
}

static int tokenize(char *input, char *args[])
{
    int argc = 0;
    char *saveptr = NULL;

    char *token = strtok_r(input, " \t", &saveptr);

    while (token != NULL && argc < MAX_ARGS - 1) {
        args[argc++] = token;
        token = strtok_r(NULL, " \t", &saveptr);
    }

    args[argc] = NULL;
    return argc;
}

static void launchCommand(char *args[],
                          const char *original,
                          int background)
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        /* Child gets its own process group. */
        if (setpgid(0, 0) < 0)
            perror("child setpgid");

        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);

        if (!background)
            tcsetpgrp(shell_terminal, getpid());

        execvp(args[0], args);
        perror(args[0]);
        _exit(127);
    }

    if (setpgid(pid, pid) < 0 &&
        errno != EACCES && errno != ESRCH) {
        perror("parent setpgid");
    }

    int index = addJob(pid, pid, original, JOB_RUNNING);

    if (index < 0) {
        kill(-pid, SIGTERM);
        waitpid(pid, NULL, 0);
        restoreShellTerminal();
        return;
    }

    if (background) {
        printf("[%d] %ld\n", jobs[index].id, (long)pid);
    } else {
        waitForForeground(index);
        removeDoneJobs();
    }
}

int main(void)
{
    char input[MAX_INPUT];

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr,
                "Run skill17 interactively in a terminal.\n");
        return EXIT_FAILURE;
    }

    initShell();

    printf("Skill 17: Job Listing and Foreground Switching\n");
    printf("Commands: sleep 20 &, jobs, fg 1, exit\n");

    while (1) {
        updateJobs();

        printf("skill17> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        input[strcspn(input, "\n")] = '\0';

        if (input[0] == '\0')
            continue;

        if (strcmp(input, "exit") == 0)
            break;

        if (strcmp(input, "jobs") == 0) {
            listJobs();
            continue;
        }

        if (strncmp(input, "fg", 2) == 0 &&
            (input[2] == '\0' || input[2] == ' ' ||
             input[2] == '\t')) {
            char *number = input + 2;

            while (*number == ' ' || *number == '\t')
                number++;

            if (*number == '\0') {
                fprintf(stderr, "Usage: fg <job-id>\n");
                continue;
            }

            char *end = NULL;
            errno = 0;
            long id = strtol(number, &end, 10);

            while (end != NULL &&
                   (*end == ' ' || *end == '\t'))
                end++;

            if (errno != 0 || end == number ||
                *end != '\0' || id <= 0 || id > 2147483647L) {
                fprintf(stderr, "Invalid job ID.\n");
                continue;
            }

            foregroundJob((int)id);
            continue;
        }

        char original[MAX_INPUT];
        snprintf(original, sizeof(original), "%s", input);

        size_t len = strlen(input);

        while (len > 0 &&
               (input[len - 1] == ' ' ||
                input[len - 1] == '\t')) {
            input[--len] = '\0';
        }

        int background = 0;

        if (len > 0 && input[len - 1] == '&') {
            background = 1;
            input[--len] = '\0';
        }

        char *args[MAX_ARGS];
        int argc = tokenize(input, args);

        if (argc == 0)
            continue;

        launchCommand(args, original, background);
    }

    /* Clean up remaining jobs. */
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].state != JOB_DONE) {
            kill(-jobs[i].pgid, SIGTERM);
        }
    }

    for (int i = 0; i < job_count; i++) {
        if (jobs[i].state != JOB_DONE) {
            while (waitpid(jobs[i].pid, NULL, 0) < 0 &&
                   errno == EINTR) {
            }
        }
    }

    restoreShellTerminal();
    printf("Exiting Skill 17.\n");
    return 0;
}
