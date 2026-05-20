#define _GNU_SOURCE
#include "backend.h"
#include "backend_helpers.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>

#define SYSV_INIT_DIR "/etc/init.d"

static int sysv_start(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t len = strlen(SYSV_INIT_DIR) + 1 + strlen(s) + 1;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s", SYSV_INIT_DIR, s);
    const char *argv[] = {path, "start", NULL};
    int rc = run_cmd(argv);
    free(s); free(path);
    return rc;
}

static int sysv_stop(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t len = strlen(SYSV_INIT_DIR) + 1 + strlen(s) + 1;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s", SYSV_INIT_DIR, s);
    const char *argv[] = {path, "stop", NULL};
    int rc = run_cmd(argv);
    free(s); free(path);
    return rc;
}

static int sysv_restart(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t len = strlen(SYSV_INIT_DIR) + 1 + strlen(s) + 1;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s", SYSV_INIT_DIR, s);
    const char *argv[] = {path, "restart", NULL};
    int rc = run_cmd(argv);
    free(s); free(path);
    return rc;
}

static int sysv_status(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t len = strlen(SYSV_INIT_DIR) + 1 + strlen(s) + 1;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s", SYSV_INIT_DIR, s);
    const char *argv[] = {path, "status", NULL};
    int rc = run_cmd(argv);
    free(s); free(path);
    return rc;
}

static int sysv_enable(const char *name)
{
    char *s = strip_unit_suffix(name);
    const char *argv[] = {"update-rc.d", s, "defaults", NULL};
    int rc = run_cmd(argv);
    free(s);
    return rc;
}

static int sysv_disable(const char *name)
{
    char *s = strip_unit_suffix(name);
    const char *argv[] = {"update-rc.d", "-f", s, "remove", NULL};
    int rc = run_cmd(argv);
    free(s);
    return rc;
}

static int sysv_is_active(const char *name)
{
    return sysv_status(name) == 0 ? 0 : 1;
}

static int sysv_is_enabled(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t len = strlen(SYSV_INIT_DIR) + 1 + strlen(s) + 1;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s", SYSV_INIT_DIR, s);
    int rc = access(path, X_OK) == 0 ? 0 : 1;
    free(s); free(path);
    return rc;
}

static int sysv_daemon_reload(void)
{
    return 0;
}

static char **sysv_list_units(void)
{
    DIR *d = opendir(SYSV_INIT_DIR);
    if (!d) return NULL;

    struct dirent *e;
    size_t cap = 64, n = 0;
    char **list = calloc(cap, sizeof(char *));

    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;

        size_t len = strlen(SYSV_INIT_DIR) + 1 + strlen(e->d_name) + 1;
        char *path = malloc(len);
        snprintf(path, len, "%s/%s", SYSV_INIT_DIR, e->d_name);

        if (access(path, X_OK) == 0) {
            if (n + 1 >= cap) {
                cap *= 2;
                list = realloc(list, cap * sizeof(char *));
            }
            list[n] = strdup(e->d_name);
            n++;
        }
        free(path);
    }
    list[n] = NULL;
    closedir(d);
    return list;
}

static int sysv_poweroff(void)
{
    const char *argv[] = {"shutdown", "-h", "now", NULL};
    return run_cmd(argv);
}

static int sysv_reboot(void)
{
    const char *argv[] = {"shutdown", "-r", "now", NULL};
    return run_cmd(argv);
}

static int sysv_halt(void)
{
    const char *argv[] = {"halt", NULL};
    return run_cmd(argv);
}

static const InitBackend sysvinit_backend = {
    .name          = "sysvinit",
    .start         = sysv_start,
    .stop          = sysv_stop,
    .restart       = sysv_restart,
    .status        = sysv_status,
    .enable        = sysv_enable,
    .disable       = sysv_disable,
    .is_active     = sysv_is_active,
    .is_enabled    = sysv_is_enabled,
    .list_units    = sysv_list_units,
    .daemon_reload = sysv_daemon_reload,
    .poweroff      = sysv_poweroff,
    .reboot        = sysv_reboot,
    .halt          = sysv_halt,
};

const InitBackend *get_sysvinit_backend(void)
{
    return &sysvinit_backend;
}
