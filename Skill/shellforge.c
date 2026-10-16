#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define INPUT_SIZE 2048
#define MAX_TOKENS 256
#define MAX_COMMANDS 16
#define MAX_ARGS 96
#define MAX_REDIRS 16
#define MAX_HISTORY 100
#define MAX_JOBS 64

typedef struct {
    char *op;
    char *file;
} Redirection;

typedef struct {
    char *argv[MAX_ARGS];
    int argc;
    Redirection redirs[MAX_REDIRS];
    int nredirs;
} Command;

typedef struct {
    char *line;
} HistoryEntry;

typedef struct {
    int id;
    pid_t pids[MAX_COMMANDS];
    int count;
    int remaining;
    int active;
    char command[INPUT_SIZE];
} Job;

static HistoryEntry history[MAX_HISTORY];
static int history_count = 0;
static int history_next = 0;
static Job jobs[MAX_JOBS];
static int next_job_id = 1;
static int shell_interactive = 0;

static void on_sigint(int sig)
{
    (void)sig;
    write(STDOUT_FILENO, "\n", 1);
}

static void add_history(const char *line)
{
    if (!line || !*line)
        return;

    free(history[history_next].line);
    history[history_next].line = strdup(line);

    if (!history[history_next].line)
        return;

    history_next = (history_next + 1) % MAX_HISTORY;

    if (history_count < MAX_HISTORY)
        history_count++;
}

static void show_history(void)
{
    int start = (history_next - history_count + MAX_HISTORY)
                % MAX_HISTORY;

    for (int i = 0; i < history_count; i++) {
        int slot = (start + i) % MAX_HISTORY;

        printf("%4d  %s\n",
               i + 1,
               history[slot].line ? history[slot].line : "");
    }

    if (!history_count)
        puts("No commands in history.");
}

static void free_history(void)
{
    for (int i = 0; i < MAX_HISTORY; i++)
        free(history[i].line);
}

static int is_operator_start(const char *s)
{
    return s[0] == '|' ||
           s[0] == '<' ||
           s[0] == '>' ||
           s[0] == '&' ||
           (s[0] == '2' && s[1] == '>');
}

static int add_char(char *buf, size_t *len, char c)
{
    if (*len >= INPUT_SIZE - 1)
        return -1;

    buf[(*len)++] = c;
    buf[*len] = '\0';

    return 0;
}

static int append_env(char *buf, size_t *len,
                      const char *input, size_t *i)
{
    char name[256];
    size_t n = 0;

    (*i)++;

    if (input[*i] == '{') {
        (*i)++;

        while (input[*i] &&
               input[*i] != '}' &&
               n < sizeof(name) - 1) {
            name[n++] = input[(*i)++];
        }

        if (input[*i] != '}') {
            fprintf(stderr,
                    "ShellForge: missing } in variable expansion\n");
            return -1;
        }

        (*i)++;
    } else {
        while ((isalnum((unsigned char)input[*i]) ||
                input[*i] == '_') &&
               n < sizeof(name) - 1) {
            name[n++] = input[(*i)++];
        }
    }

    if (n == 0)
        return add_char(buf, len, '$');

    name[n] = '\0';

    const char *value = getenv(name);

    if (!value)
        return 0;

    while (*value) {
        if (add_char(buf, len, *value++) < 0) {
            fprintf(stderr,
                    "ShellForge: expanded token is too long\n");
            return -1;
        }
    }

    return 0;
}

/* Tokenizer supporting quotes, variables and shell operators. */
static int tokenize(const char *input, char *tokens[])
{
    int count = 0;
    size_t i = 0;

    while (input[i]) {
        while (input[i] == ' ' || input[i] == '\t')
            i++;

        if (!input[i])
            break;

        if (count >= MAX_TOKENS - 1) {
            fprintf(stderr, "ShellForge: too many tokens\n");
            return -1;
        }

        if (strncmp(input + i, "2>&1", 4) == 0) {
            tokens[count++] = strdup("2>&1");
            i += 4;
            continue;
        }

        if (strncmp(input + i, "2>", 2) == 0) {
            tokens[count++] = strdup("2>");
            i += 2;
            continue;
        }

        if (strncmp(input + i, ">>", 2) == 0) {
            tokens[count++] = strdup(">>");
            i += 2;
            continue;
        }

        if (input[i] == '|' ||
            input[i] == '<' ||
            input[i] == '>' ||
            input[i] == '&') {
            char op[2] = { input[i++], '\0' };
            tokens[count++] = strdup(op);
            continue;
        }

        char word[INPUT_SIZE] = "";
        size_t len = 0;
        int started = 0;
        char quote = '\0';

        while (input[i]) {
            char c = input[i];

            if (!quote &&
                (c == ' ' || c == '\t' ||
                 is_operator_start(input + i))) {
                break;
            }

            if (quote == '\'') {
                if (c == '\'') {
                    quote = '\0';
                    i++;
                } else {
                    if (add_char(word, &len, c) < 0)
                        goto too_long;
                    i++;
                }

                started = 1;
                continue;
            }

            if (quote == '"') {
                if (c == '"') {
                    quote = '\0';
                    i++;
                    started = 1;
                    continue;
                }

                if (c == '\\' && input[i + 1]) {
                    char next = input[i + 1];

                    if (next == '$' || next == '`' ||
                        next == '"' || next == '\\') {
                        i++;
                        if (add_char(word, &len, input[i++]) < 0)
                            goto too_long;
                    } else {
                        if (add_char(word, &len, input[i++]) < 0)
                            goto too_long;
                    }

                    started = 1;
                    continue;
                }

                if (c == '$') {
                    if (append_env(word, &len, input, &i) < 0)
                        return -1;

                    started = 1;
                    continue;
                }

                if (add_char(word, &len, c) < 0)
                    goto too_long;

                i++;
                started = 1;
                continue;
            }

            if (c == '\'' || c == '"') {
                quote = c;
                i++;
                started = 1;
                continue;
            }

            if (c == '\\' && input[i + 1]) {
                i++;

                if (add_char(word, &len, input[i++]) < 0)
                    goto too_long;

                started = 1;
                continue;
            }

            if (c == '$') {
                if (append_env(word, &len, input, &i) < 0)
                    return -1;

                started = 1;
                continue;
            }

            if (add_char(word, &len, c) < 0)
                goto too_long;

            i++;
            started = 1;
        }

        if (quote) {
            fprintf(stderr, "ShellForge: unmatched quote\n");
            return -1;
        }

        if (started) {
            tokens[count] = strdup(word);

            if (!tokens[count]) {
                perror("strdup");
                return -1;
            }

            count++;
        }
    }

    tokens[count] = NULL;
    return count;

too_long:
    fprintf(stderr, "ShellForge: token too long\n");
    return -1;
}

static void free_tokens(char *tokens[], int count)
{
    for (int i = 0; i < count; i++)
        free(tokens[i]);
}

static int is_redir(const char *s)
{
    return !strcmp(s, "<") ||
           !strcmp(s, ">") ||
           !strcmp(s, ">>") ||
           !strcmp(s, "2>") ||
           !strcmp(s, "2>&1");
}

static int parse_commands(char *tokens[], int ntokens,
                          Command cmds[], int *ncmds,
                          int *background)
{
    memset(cmds, 0, sizeof(Command) * MAX_COMMANDS);

    *ncmds = 1;
    *background = 0;

    if (!ntokens)
        return -1;

    if (!strcmp(tokens[ntokens - 1], "&")) {
        *background = 1;
        ntokens--;

        if (!ntokens)
            return -1;
    }

    for (int i = 0; i < ntokens; i++) {
        if (!strcmp(tokens[i], "&")) {
            fprintf(stderr,
                    "ShellForge: '&' is only supported at the end\n");
            return -1;
        }

        if (!strcmp(tokens[i], "|")) {
            Command *cur = &cmds[*ncmds - 1];

            if (!cur->argc || *ncmds >= MAX_COMMANDS) {
                fprintf(stderr,
                        "ShellForge: invalid or oversized pipeline\n");
                return -1;
            }

            (*ncmds)++;
            continue;
        }

        Command *cmd = &cmds[*ncmds - 1];

        if (is_redir(tokens[i])) {
            const char *op = tokens[i];
            char *file = NULL;

            if (strcmp(op, "2>&1")) {
                if (i + 1 >= ntokens ||
                    is_redir(tokens[i + 1]) ||
                    !strcmp(tokens[i + 1], "|") ||
                    !strcmp(tokens[i + 1], "&")) {
                    fprintf(stderr,
                            "ShellForge: redirection needs a filename\n");
                    return -1;
                }

                file = tokens[++i];
            }

            if (cmd->nredirs >= MAX_REDIRS) {
                fprintf(stderr,
                        "ShellForge: too many redirections\n");
                return -1;
            }

            cmd->redirs[cmd->nredirs++] =
                (Redirection){ (char *)op, file };
        } else {
            if (cmd->argc >= MAX_ARGS - 1) {
                fprintf(stderr,
                        "ShellForge: too many arguments\n");
                return -1;
            }

            cmd->argv[cmd->argc++] = tokens[i];
        }
    }

    if (!cmds[*ncmds - 1].argc) {
        fprintf(stderr,
                "ShellForge: pipeline cannot end with '|'\n");
        return -1;
    }

    for (int i = 0; i < *ncmds; i++)
        cmds[i].argv[cmds[i].argc] = NULL;

    return 0;
}

static int apply_redirections(Command *cmd)
{
    for (int i = 0; i < cmd->nredirs; i++) {
        char *op = cmd->redirs[i].op;
        char *file = cmd->redirs[i].file;

        if (!strcmp(op, "2>&1")) {
            if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
                perror("dup2");
                return -1;
            }

            continue;
        }

        int target, flags;

        if (!strcmp(op, "<")) {
            target = STDIN_FILENO;
            flags = O_RDONLY;
        } else if (!strcmp(op, ">")) {
            target = STDOUT_FILENO;
            flags = O_WRONLY | O_CREAT | O_TRUNC;
        } else if (!strcmp(op, ">>")) {
            target = STDOUT_FILENO;
            flags = O_WRONLY | O_CREAT | O_APPEND;
        } else {
            target = STDERR_FILENO;
            flags = O_WRONLY | O_CREAT | O_TRUNC;
        }

        int fd = open(file, flags, 0644);

        if (fd < 0) {
            perror(file);
            return -1;
        }

        if (dup2(fd, target) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }

        close(fd);
    }

    return 0;
}

static int builtin_index(const char *s)
{
    const char *names[] = {
        "cd", "pwd", "echo", "export", "unset", "env",
        "history", "help", "clear", "jobs", "wait", "exit",
        NULL
    };

    for (int i = 0; names[i]; i++) {
        if (!strcmp(s, names[i]))
            return i;
    }

    return -1;
}

static int builtin_run(Command *cmd, int *should_exit)
{
    char **a = cmd->argv;
    int argc = cmd->argc;

    *should_exit = 0;

    if (!argc)
        return 0;

    if (!strcmp(a[0], "cd")) {
        const char *path = a[1] ? a[1] : getenv("HOME");

        if (!path) {
            fprintf(stderr, "cd: HOME is not set\n");
            return 1;
        }

        char old[4096];

        if (!getcwd(old, sizeof old))
            old[0] = '\0';

        if (!strcmp(path, "-")) {
            path = getenv("OLDPWD");

            if (!path) {
                fprintf(stderr, "cd: OLDPWD is not set\n");
                return 1;
            }
        }

        if (chdir(path) < 0) {
            perror("cd");
            return 1;
        }

        if (old[0])
            setenv("OLDPWD", old, 1);

        char now[4096];

        if (getcwd(now, sizeof now))
            setenv("PWD", now, 1);

        if (a[1] && !strcmp(a[1], "-"))
            printf("%s\n", getenv("PWD") ? getenv("PWD") : "");

    } else if (!strcmp(a[0], "pwd")) {
        char cwd[4096];

        if (getcwd(cwd, sizeof cwd))
            puts(cwd);
        else
            perror("pwd");

    } else if (!strcmp(a[0], "echo")) {
        for (int i = 1; i < argc; i++)
            printf("%s%s", a[i], i + 1 < argc ? " " : "");

        putchar('\n');

    } else if (!strcmp(a[0], "export")) {
        if (argc == 1) {
            puts("Usage: export NAME=value");
            return 1;
        }

        for (int i = 1; i < argc; i++) {
            char *eq = strchr(a[i], '=');

            if (!eq || eq == a[i]) {
                fprintf(stderr,
                        "export: expected NAME=value: %s\n", a[i]);
                continue;
            }

            size_t n = (size_t)(eq - a[i]);

            int valid =
                isalpha((unsigned char)a[i][0]) || a[i][0] == '_';

            for (size_t j = 1; j < n; j++) {
                if (!isalnum((unsigned char)a[i][j]) &&
                    a[i][j] != '_')
                    valid = 0;
            }

            if (!valid || n > 255) {
                fprintf(stderr, "export: invalid variable name\n");
                continue;
            }

            char name[256];

            memcpy(name, a[i], n);
            name[n] = '\0';

            if (setenv(name, eq + 1, 1) < 0)
                perror("export");
        }

    } else if (!strcmp(a[0], "unset")) {
        if (argc < 2) {
            fprintf(stderr, "unset: usage: unset NAME\n");
            return 1;
        }

        for (int i = 1; i < argc; i++) {
            if (unsetenv(a[i]) < 0)
                perror("unset");
        }

    } else if (!strcmp(a[0], "env")) {
        extern char **environ;

        for (char **p = environ; p && *p; p++)
            puts(*p);

    } else if (!strcmp(a[0], "history")) {
        show_history();

    } else if (!strcmp(a[0], "help")) {
        puts("\nAKHIL0715 SHELLFORGE - Built-in commands\n");
        puts("  cd [DIR]       Change directory");
        puts("  pwd            Display current directory");
        puts("  echo TEXT      Display text");
        puts("  export N=VALUE Set environment variable");
        puts("  unset NAME     Remove environment variable");
        puts("  env            Display environment variables");
        puts("  history        Display command history");
        puts("  jobs           List background jobs");
        puts("  wait           Wait for background jobs");
        puts("  clear          Clear terminal");
        puts("  help           Display this help");
        puts("  exit           Exit ShellForge\n");
        puts("External commands are supported through execvp().");
        puts("Pipes: |");
        puts("Redirection: <, >, >>, 2>, 2>&1");
        puts("Background command: command &");
        puts("Variables: $NAME or ${NAME}\n");

    } else if (!strcmp(a[0], "clear")) {
        printf("\033[2J\033[H");
        fflush(stdout);

    } else if (!strcmp(a[0], "jobs")) {
        int any = 0;

        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].active) {
                printf("[%d] Running  %s\n",
                       jobs[i].id, jobs[i].command);
                any = 1;
            }
        }

        if (!any)
            puts("No active background jobs.");

    } else if (!strcmp(a[0], "wait")) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].active) {
                for (int j = 0; j < jobs[i].count; j++) {
                    if (jobs[i].pids[j] <= 0)
                        continue;

                    int status;

                    while (waitpid(jobs[i].pids[j],
                                   &status, 0) < 0 &&
                           errno == EINTR) {
                    }

                    jobs[i].pids[j] = -1;
                }

                printf("[%d] Done  %s\n",
                       jobs[i].id, jobs[i].command);

                jobs[i].active = 0;
            }
        }

    } else if (!strcmp(a[0], "exit")) {
        *should_exit = 1;
    }

    return 1;
}

static void reap_jobs(void)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active)
            continue;

        for (int j = 0; j < jobs[i].count; j++) {
            if (jobs[i].pids[j] <= 0)
                continue;

            int status;

            pid_t r = waitpid(jobs[i].pids[j],
                              &status, WNOHANG);

            if (r == jobs[i].pids[j]) {
                jobs[i].pids[j] = -1;
                jobs[i].remaining--;
            }
        }

        if (jobs[i].remaining <= 0) {
            printf("\n[%d] Done  %s\n",
                   jobs[i].id, jobs[i].command);
            jobs[i].active = 0;
        }
    }
}

static int add_job(pid_t pids[], int count, const char *line)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active) {
            jobs[i].id = next_job_id++;
            jobs[i].count = count;
            jobs[i].remaining = count;
            jobs[i].active = 1;

            for (int j = 0; j < count; j++)
                jobs[i].pids[j] = pids[j];

            snprintf(jobs[i].command,
                     sizeof jobs[i].command, "%s", line);

            printf("[%d] %d\n",
                   jobs[i].id, (int)pids[0]);

            return 0;
        }
    }

    fprintf(stderr,
            "ShellForge: job table full; waiting for child processes\n");

    for (int j = 0; j < count; j++) {
        int status;

        while (waitpid(pids[j], &status, 0) < 0 &&
               errno == EINTR) {
        }
    }

    return -1;
}

static int run_pipeline(Command cmds[], int ncmds,
                        int background, const char *line)
{
    int prev_read = -1;
    pid_t pids[MAX_COMMANDS];
    int started = 0;
    int failed = 0;

    for (int i = 0; i < ncmds; i++) {
        int fds[2] = {-1, -1};

        if (i < ncmds - 1 && pipe(fds) < 0) {
            perror("pipe");
            failed = 1;
            break;
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");

            if (fds[0] >= 0)
                close(fds[0]);

            if (fds[1] >= 0)
                close(fds[1]);

            failed = 1;
            break;
        }

        if (pid == 0) {
            signal(SIGINT, SIG_DFL);

            if (prev_read >= 0 &&
                dup2(prev_read, STDIN_FILENO) < 0)
                _exit(126);

            if (i < ncmds - 1 &&
                dup2(fds[1], STDOUT_FILENO) < 0)
                _exit(126);

            if (prev_read >= 0)
                close(prev_read);

            if (fds[0] >= 0)
                close(fds[0]);

            if (fds[1] >= 0)
                close(fds[1]);

            if (apply_redirections(&cmds[i]) < 0)
                _exit(126);

            if (builtin_index(cmds[i].argv[0]) >= 0) {
                int child_exit = 0;

                builtin_run(&cmds[i], &child_exit);
                fflush(NULL);
                _exit(0);
            }

            execvp(cmds[i].argv[0], cmds[i].argv);

            fprintf(stderr, "%s: %s\n",
                    cmds[i].argv[0], strerror(errno));

            _exit(errno == ENOENT ? 127 : 126);
        }

        pids[started++] = pid;

        if (prev_read >= 0)
            close(prev_read);

        if (fds[1] >= 0)
            close(fds[1]);

        prev_read = fds[0];
    }

    if (prev_read >= 0)
        close(prev_read);

    if (!background) {
        for (int i = 0; i < started; i++) {
            int status;
            pid_t r;

            do {
                r = waitpid(pids[i], &status, 0);
            } while (r < 0 && errno == EINTR);

            if (r < 0 ||
                !WIFEXITED(status) ||
                WEXITSTATUS(status) != 0)
                failed = 1;
        }

        return failed ? -1 : 0;
    }

    if (started)
        add_job(pids, started, line);

    return failed ? -1 : 0;
}

static int run_parent_builtin(Command *cmd, int *should_exit)
{
    int saved[3] = {-1, -1, -1};

    fflush(NULL);

    for (int i = 0; i < 3; i++) {
        saved[i] = dup(i);

        if (saved[i] < 0) {
            perror("dup");

            for (int j = 0; j < i; j++) {
                dup2(saved[j], j);
                close(saved[j]);
            }

            return -1;
        }
    }

    if (apply_redirections(cmd) < 0) {
        for (int i = 0; i < 3; i++) {
            dup2(saved[i], i);
            close(saved[i]);
        }

        return -1;
    }

    int handled = builtin_run(cmd, should_exit);

    fflush(NULL);

    for (int i = 0; i < 3; i++) {
        dup2(saved[i], i);
        close(saved[i]);
    }

    return handled;
}

static void print_banner(void)
{
    puts("\033[1;36m");
    puts("  ____  _          _ _ _____                      ");
    puts(" / ___|| |__   ___| | |Forge|__  ___              ");
    puts(" \\___ \\| '_ \\ / _ \\ | |  _/ _ \\/ __|             ");
    puts("  ___) | | | |  __/ | | ||  __/\\__ \\             ");
    puts(" |____/|_| |_|\\___|_|_|_| \\___||___/             ");
    puts("\033[0m");

    puts("              AKHIL0715 SHELLFORGE");
    puts("  Custom C Shell | Pipes | Redirection | Background Jobs");
    puts("  Type 'help' for commands and 'exit' to quit.\n");
}

int main(void)
{
    char input[INPUT_SIZE];

    shell_interactive = isatty(STDIN_FILENO);

    memset(history, 0, sizeof history);
    memset(jobs, 0, sizeof jobs);

    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT, &sa, NULL);

    if (shell_interactive)
        print_banner();
    else
        puts("AKHIL0715 SHELLFORGE - type 'help' for commands.");

    while (1) {
        reap_jobs();

        if (shell_interactive) {
            char cwd[1024];

            if (getcwd(cwd, sizeof cwd)) {
                printf("\033[1;32makhil0715\033[0m:"
                       "\033[1;34m%s\033[0m$ ", cwd);
            } else {
                printf("akhil0715$ ");
            }
        } else {
            printf("akhil0715$ ");
        }

        fflush(stdout);

        if (!fgets(input, sizeof input, stdin)) {
            if (shell_interactive) {
                clearerr(stdin);
                puts("\nShellForge: use 'exit' to quit (Ctrl+D is ignored).");
                continue;
            }

            putchar('\n');
            break;
        }

        input[strcspn(input, "\n")] = '\0';

        if (!input[0])
            continue;

        add_history(input);

        char *tokens[MAX_TOKENS] = {0};

        int ntokens = tokenize(input, tokens);

        if (ntokens < 0) {
            free_tokens(tokens, MAX_TOKENS);
            continue;
        }

        if (ntokens == 0) {
            free_tokens(tokens, ntokens);
            continue;
        }

        Command cmds[MAX_COMMANDS];
        int ncmds = 0;
        int background = 0;

        if (parse_commands(tokens, ntokens, cmds,
                           &ncmds, &background) < 0) {
            free_tokens(tokens, ntokens);
            continue;
        }

        int should_exit = 0;
        int handled = -1;

        if (ncmds == 1 &&
            !background &&
            builtin_index(cmds[0].argv[0]) >= 0) {
            handled = run_parent_builtin(
                &cmds[0], &should_exit);
        }

        if (should_exit) {
            free_tokens(tokens, ntokens);
            break;
        }

        if (handled < 0)
            run_pipeline(cmds, ncmds, background, input);

        free_tokens(tokens, ntokens);
    }

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active) {
            for (int j = 0; j < jobs[i].count; j++) {
                if (jobs[i].pids[j] > 0) {
                    kill(jobs[i].pids[j], SIGTERM);

                    int status;

                    while (waitpid(jobs[i].pids[j],
                                   &status, 0) < 0 &&
                           errno == EINTR) {
                    }
                }
            }
        }
    }

    free_history();

    puts("ShellForge closed cleanly. Goodbye, Akhil!");

    return 0;
}
