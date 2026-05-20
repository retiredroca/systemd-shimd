#include "../common/init_detect.h"
#include "../common/backend.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void print_usage(void)
{
    fprintf(stderr, "Usage: systemctl [OPTIONS...] COMMAND [UNIT...]\n");
    fprintf(stderr, "\nCommands:\n");
    fprintf(stderr, "  start NAME         Start a unit\n");
    fprintf(stderr, "  stop NAME          Stop a unit\n");
    fprintf(stderr, "  restart NAME       Restart a unit\n");
    fprintf(stderr, "  status NAME        Show status of a unit\n");
    fprintf(stderr, "  enable NAME        Enable a unit\n");
    fprintf(stderr, "  disable NAME       Disable a unit\n");
    fprintf(stderr, "  is-active NAME     Check if a unit is active\n");
    fprintf(stderr, "  is-enabled NAME    Check if a unit is enabled\n");
    fprintf(stderr, "  list-units         List available units\n");
    fprintf(stderr, "  daemon-reload      Reload systemd manager configuration\n");
    fprintf(stderr, "  reboot             Reboot the system\n");
    fprintf(stderr, "  poweroff           Power off the system\n");
    fprintf(stderr, "  halt               Halt the system\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage();
        return 1;
    }

    InitType init = detect_init_system();
    const InitBackend *backend = get_backend(init);

    if (!backend) {
        fprintf(stderr, "systemd-shimd: unsupported init system (%s)\n",
                init_type_name(init));
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage();
        return 0;
    }

    if (strcmp(cmd, "--version") == 0) {
        printf("systemd-shimd 0.1.0\n");
        return 0;
    }

    if (strcmp(cmd, "start") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: systemctl start NAME\n"); return 1; }
        return backend->start(argv[2]);
    }

    if (strcmp(cmd, "stop") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: systemctl stop NAME\n"); return 1; }
        return backend->stop(argv[2]);
    }

    if (strcmp(cmd, "restart") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: systemctl restart NAME\n"); return 1; }
        return backend->restart(argv[2]);
    }

    if (strcmp(cmd, "status") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: systemctl status NAME\n"); return 1; }
        return backend->status(argv[2]);
    }

    if (strcmp(cmd, "enable") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: systemctl enable NAME\n"); return 1; }
        return backend->enable(argv[2]);
    }

    if (strcmp(cmd, "disable") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: systemctl disable NAME\n"); return 1; }
        return backend->disable(argv[2]);
    }

    if (strcmp(cmd, "is-active") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: systemctl is-active NAME\n"); return 1; }
        int rc = backend->is_active(argv[2]);
        printf("%s\n", rc == 0 ? "active" : "inactive");
        return rc;
    }

    if (strcmp(cmd, "is-enabled") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: systemctl is-enabled NAME\n"); return 1; }
        int rc = backend->is_enabled(argv[2]);
        printf("%s\n", rc == 0 ? "enabled" : "disabled");
        return rc;
    }

    if (strcmp(cmd, "list-units") == 0) {
        char **units = backend->list_units();
        if (!units) {
            fprintf(stderr, "systemd-shimd: listing units not supported by %s backend\n",
                    backend->name);
            return 1;
        }
        printf("  UNIT                LOAD   ACTIVE     SUB\n");
        for (int i = 0; units[i]; i++) {
            printf("  %-20s loaded active     running\n", units[i]);
            free(units[i]);
        }
        free(units);
        return 0;
    }

    if (strcmp(cmd, "daemon-reload") == 0) {
        return backend->daemon_reload();
    }

    if (strcmp(cmd, "reboot") == 0) {
        return backend->reboot();
    }

    if (strcmp(cmd, "poweroff") == 0) {
        return backend->poweroff();
    }

    if (strcmp(cmd, "halt") == 0) {
        return backend->halt();
    }

    fprintf(stderr, "systemd-shimd: unknown command '%s'\n", cmd);
    print_usage();
    return 1;
}
