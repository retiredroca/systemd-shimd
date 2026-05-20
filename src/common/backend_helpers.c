#define _GNU_SOURCE
#include "backend_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int run_cmd(const char *argv[])
{
    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    if (pid < 0) return -1;
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

int run_cmd_capture(const char *argv[], char **output, size_t *output_len)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }

    close(pipefd[1]);
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    ssize_t n;
    while ((n = read(pipefd[0], buf + len, cap - len - 1)) > 0) {
        len += (size_t)n;
        if (len >= cap - 1) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    buf[len] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    *output = buf;
    if (output_len) *output_len = len;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

void chomp(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
        s[--len] = '\0';
}

char *strip_unit_suffix(const char *name)
{
    size_t len = strlen(name);
    if (len > 8 && strcmp(name + len - 8, ".service") == 0) {
        char *s = strdup(name);
        s[len - 8] = '\0';
        return s;
    }
    return strdup(name);
}
