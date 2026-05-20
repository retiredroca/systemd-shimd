#define _GNU_SOURCE
#include "backend.h"
#include "backend_helpers.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef RUNIT_SV_DIR
#define RUNIT_SV_DIR "/etc/sv"
#endif

static char *runit_svc_path(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t len = strlen(RUNIT_SV_DIR) + 1 + strlen(s) + 1;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s", RUNIT_SV_DIR, s);
    free(s);
    return path;
}

static int runit_start(const char *name)
{
    char *path = runit_svc_path(name);
    const char *argv[] = {"sv", "start", path, NULL};
    int rc = run_cmd(argv);
    free(path);
    return rc;
}

static int runit_stop(const char *name)
{
    char *path = runit_svc_path(name);
    const char *argv[] = {"sv", "stop", path, NULL};
    int rc = run_cmd(argv);
    free(path);
    return rc;
}

static int runit_restart(const char *name)
{
    char *path = runit_svc_path(name);
    const char *argv[] = {"sv", "restart", path, NULL};
    int rc = run_cmd(argv);
    free(path);
    return rc;
}

static int runit_status(const char *name)
{
    char *path = runit_svc_path(name);
    const char *argv[] = {"sv", "status", path, NULL};
    int rc = run_cmd(argv);
    free(path);
    return rc;
}

static int runit_enable(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t len = strlen(RUNIT_SV_DIR) + 1 + strlen(s) + 1;
    char *src = malloc(len);
    snprintf(src, len, "%s/%s", RUNIT_SV_DIR, s);

    size_t dst_len = strlen("/etc/runit/runsvdir/default") + 1 + strlen(s) + 1;
    char *dst = malloc(dst_len);
    snprintf(dst, dst_len, "/etc/runit/runsvdir/default/%s", s);

    const char *argv[] = {"ln", "-sf", src, dst, NULL};
    int rc = run_cmd(argv);

    free(s); free(src); free(dst);
    return rc;
}

static int runit_disable(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t len = strlen("/etc/runit/runsvdir/default") + 1 + strlen(s) + 1;
    char *path = malloc(len);
    snprintf(path, len, "/etc/runit/runsvdir/default/%s", s);

    const char *argv[] = {"rm", "-f", path, NULL};
    int rc = run_cmd(argv);

    free(s); free(path);
    return rc;
}

static int runit_is_active(const char *name)
{
    return runit_status(name) == 0 ? 0 : 1;
}

static int runit_is_enabled(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t len = strlen("/etc/runit/runsvdir/default") + 1 + strlen(s) + 1;
    char *path = malloc(len);
    snprintf(path, len, "/etc/runit/runsvdir/default/%s", s);

    int rc = access(path, F_OK) == 0 ? 0 : 1;
    free(s); free(path);
    return rc;
}

static int runit_daemon_reload(void)
{
    return 0;
}

static char **runit_list_units(void)
{
    return NULL;
}

static int runit_poweroff(void)
{
    const char *argv[] = {"shutdown", "-h", "now", NULL};
    return run_cmd(argv);
}

static int runit_reboot(void)
{
    const char *argv[] = {"shutdown", "-r", "now", NULL};
    return run_cmd(argv);
}

static int runit_halt(void)
{
    const char *argv[] = {"halt", NULL};
    return run_cmd(argv);
}

static const InitBackend runit_backend = {
    .name          = "runit",
    .start         = runit_start,
    .stop          = runit_stop,
    .restart       = runit_restart,
    .status        = runit_status,
    .enable        = runit_enable,
    .disable       = runit_disable,
    .is_active     = runit_is_active,
    .is_enabled    = runit_is_enabled,
    .list_units    = runit_list_units,
    .daemon_reload = runit_daemon_reload,
    .poweroff      = runit_poweroff,
    .reboot        = runit_reboot,
    .halt          = runit_halt,
};

const InitBackend *get_runit_backend(void)
{
    return &runit_backend;
}
