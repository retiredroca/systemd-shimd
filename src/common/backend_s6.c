#define _GNU_SOURCE
#include "backend.h"
#include "backend_helpers.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef S6_SERVICE_DIR
#define S6_SERVICE_DIR "/run/s6-rc/servicedirs"
#endif

#ifndef S6_SCAN_DIR
#define S6_SCAN_DIR "/run/s6-rc/compiled"
#endif

static char *s6_svc_path(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t len = strlen(S6_SERVICE_DIR) + 1 + strlen(s) + 1;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s", S6_SERVICE_DIR, s);
    free(s);
    return path;
}

static int s6_start(const char *name)
{
    char *path = s6_svc_path(name);
    const char *argv[] = {"s6-svc", "-u", path, NULL};
    int rc = run_cmd(argv);
    free(path);
    return rc;
}

static int s6_stop(const char *name)
{
    char *path = s6_svc_path(name);
    const char *argv[] = {"s6-svc", "-d", path, NULL};
    int rc = run_cmd(argv);
    free(path);
    return rc;
}

static int s6_restart(const char *name)
{
    char *path = s6_svc_path(name);
    const char *argv[] = {"s6-svc", "-r", path, NULL};
    int rc = run_cmd(argv);
    free(path);
    return rc;
}

static int s6_status(const char *name)
{
    char *path = s6_svc_path(name);
    const char *argv[] = {"s6-svc", "-a", path, NULL};
    int rc = run_cmd(argv);
    free(path);
    return rc;
}

static int s6_enable(const char *name)
{
    char *s = strip_unit_suffix(name);
    const char *argv[] = {"s6-rc", "-u", "change", s, NULL};
    int rc = run_cmd(argv);
    free(s);
    return rc;
}

static int s6_disable(const char *name)
{
    char *s = strip_unit_suffix(name);
    const char *argv[] = {"s6-rc", "-d", "change", s, NULL};
    int rc = run_cmd(argv);
    free(s);
    return rc;
}

static int s6_is_active(const char *name)
{
    return s6_status(name) == 0 ? 0 : 1;
}

static int s6_is_enabled(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t len = strlen(S6_SCAN_DIR) + 1 + strlen(s) + 1;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s", S6_SCAN_DIR, s);

    int rc = access(path, F_OK) == 0 ? 0 : 1;
    free(s); free(path);
    return rc;
}

static int s6_daemon_reload(void)
{
    return 0;
}

static char **s6_list_units(void)
{
    return NULL;
}

static int s6_poweroff(void)
{
    const char *argv[] = {"s6-svscanctl", "-p", "/run/s6-rc/servicedirs", NULL};
    return run_cmd(argv);
}

static int s6_reboot(void)
{
    const char *argv[] = {"s6-svscanctl", "-r", "/run/s6-rc/servicedirs", NULL};
    return run_cmd(argv);
}

static int s6_halt(void)
{
    const char *argv[] = {"s6-svscanctl", "-h", "/run/s6-rc/servicedirs", NULL};
    return run_cmd(argv);
}

static const InitBackend s6_backend = {
    .name          = "s6",
    .start         = s6_start,
    .stop          = s6_stop,
    .restart       = s6_restart,
    .status        = s6_status,
    .enable        = s6_enable,
    .disable       = s6_disable,
    .is_active     = s6_is_active,
    .is_enabled    = s6_is_enabled,
    .list_units    = s6_list_units,
    .daemon_reload = s6_daemon_reload,
    .poweroff      = s6_poweroff,
    .reboot        = s6_reboot,
    .halt          = s6_halt,
};

const InitBackend *get_s6_backend(void)
{
    return &s6_backend;
}
