#define _GNU_SOURCE
#include "backend.h"
#include "backend_helpers.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int openrc_start(const char *name)
{
    char *s = strip_unit_suffix(name);
    const char *argv[] = {"rc-service", s, "start", NULL};
    int rc = run_cmd(argv);
    free(s);
    return rc;
}

static int openrc_stop(const char *name)
{
    char *s = strip_unit_suffix(name);
    const char *argv[] = {"rc-service", s, "stop", NULL};
    int rc = run_cmd(argv);
    free(s);
    return rc;
}

static int openrc_restart(const char *name)
{
    char *s = strip_unit_suffix(name);
    const char *argv[] = {"rc-service", s, "restart", NULL};
    int rc = run_cmd(argv);
    free(s);
    return rc;
}

static int openrc_status(const char *name)
{
    char *s = strip_unit_suffix(name);
    const char *argv[] = {"rc-service", s, "status", NULL};
    int rc = run_cmd(argv);
    free(s);
    return rc;
}

static int openrc_enable(const char *name)
{
    char *s = strip_unit_suffix(name);
    const char *argv[] = {"rc-update", "add", s, NULL};
    int rc = run_cmd(argv);
    free(s);
    return rc;
}

static int openrc_disable(const char *name)
{
    char *s = strip_unit_suffix(name);
    const char *argv[] = {"rc-update", "delete", s, NULL};
    int rc = run_cmd(argv);
    free(s);
    return rc;
}

static int openrc_is_active(const char *name)
{
    char *s = strip_unit_suffix(name);
    const char *argv[] = {"rc-service", s, "status", NULL};
    int rc = run_cmd(argv);
    free(s);
    return rc == 0 ? 0 : 1;
}

static int openrc_is_enabled(const char *name)
{
    char *s = strip_unit_suffix(name);
    const char *argv[] = {"rc-update", "show", NULL};
    char *output = NULL;
    int rc = run_cmd_capture(argv, &output, NULL);
    if (rc != 0 || !output) {
        free(s);
        free(output);
        return 1;
    }

    int found = 0;
    char *line = output;
    while (line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (strstr(line, s)) {
            found = 1;
            break;
        }

        if (nl) {
            *nl = '\n';
            line = nl + 1;
        } else {
            break;
        }
    }

    free(s);
    free(output);
    return found ? 0 : 1;
}

static int openrc_daemon_reload(void)
{
    return 0;
}

static char **openrc_list_units(void)
{
    const char *argv[] = {"rc-service", "-l", NULL};
    char *output = NULL;
    int rc = run_cmd_capture(argv, &output, NULL);
    if (rc != 0 || !output) return NULL;

    size_t count = 0;
    for (char *p = output; *p; p++)
        if (*p == '\n') count++;

    if (count == 0 && output[0] == '\0') { free(output); return NULL; }

    char **list = calloc(count + 2, sizeof(char *));
    size_t idx = 0;
    char *line = output;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        chomp(line);
        if (line[0]) {
            list[idx] = strdup(line);
            idx++;
        }
        if (nl) {
            *nl = '\n';
            line = nl + 1;
        } else {
            break;
        }
    }
    list[idx] = NULL;
    free(output);
    return list;
}

static int openrc_poweroff(void)
{
    const char *argv[] = {"openrc-shutdown", "-p", "now", NULL};
    return run_cmd(argv);
}

static int openrc_reboot(void)
{
    const char *argv[] = {"openrc-shutdown", "-r", "now", NULL};
    return run_cmd(argv);
}

static int openrc_halt(void)
{
    const char *argv[] = {"openrc-shutdown", "-H", "now", NULL};
    return run_cmd(argv);
}

static const InitBackend openrc_backend = {
    .name          = "openrc",
    .start         = openrc_start,
    .stop          = openrc_stop,
    .restart       = openrc_restart,
    .status        = openrc_status,
    .enable        = openrc_enable,
    .disable       = openrc_disable,
    .is_active     = openrc_is_active,
    .is_enabled    = openrc_is_enabled,
    .list_units    = openrc_list_units,
    .daemon_reload = openrc_daemon_reload,
    .poweroff      = openrc_poweroff,
    .reboot        = openrc_reboot,
    .halt          = openrc_halt,
};

const InitBackend *get_openrc_backend(void)
{
    return &openrc_backend;
}
