#include "systemd-shimd.h"

static void print_usage(void)
{
    fprintf(stderr, "Usage: systemctl [OPTIONS...] COMMAND [UNIT...]\n");
    fprintf(stderr, "\nCommands:\n");
    fprintf(stderr, "  start NAME              Start a unit\n");
    fprintf(stderr, "  stop NAME               Stop a unit\n");
    fprintf(stderr, "  restart NAME            Restart a unit\n");
    fprintf(stderr, "  try-restart NAME        Restart a unit if active\n");
    fprintf(stderr, "  reload NAME             Reload a unit\n");
    fprintf(stderr, "  reload-or-restart NAME  Reload or restart a unit\n");
    fprintf(stderr, "  status NAME             Show status of a unit\n");
    fprintf(stderr, "  enable NAME             Enable a unit\n");
    fprintf(stderr, "  disable NAME            Disable a unit\n");
    fprintf(stderr, "  reenable NAME           Re-enable a unit\n");
    fprintf(stderr, "  mask NAME               Mask a unit\n");
    fprintf(stderr, "  unmask NAME             Unmask a unit\n");
    fprintf(stderr, "  is-active NAME          Check if a unit is active\n");
    fprintf(stderr, "  is-enabled NAME         Check if a unit is enabled\n");
    fprintf(stderr, "  is-failed NAME          Check if a unit has failed\n");
    fprintf(stderr, "  is-system-running       Check if system is running\n");
    fprintf(stderr, "  list-units              List available units\n");
    fprintf(stderr, "  list-unit-files         List installed unit files\n");
    fprintf(stderr, "  list-dependencies NAME  List unit dependencies\n");
    fprintf(stderr, "  show [NAME]             Show unit or manager properties\n");
    fprintf(stderr, "  cat NAME                Show unit file contents\n");
    fprintf(stderr, "  kill NAME [SIGNAL]      Send signal to a unit\n");
    fprintf(stderr, "  daemon-reload           Reload manager configuration\n");
    fprintf(stderr, "  daemon-reexec           Reexecute the manager\n");
    fprintf(stderr, "  reset-failed [NAME]     Reset failed state\n");
    fprintf(stderr, "  preset NAME             Enable/disable per preset policy\n");
    fprintf(stderr, "  get-default             Show default target\n");
    fprintf(stderr, "  set-default TARGET      Set default target\n");
    fprintf(stderr, "  reboot                  Reboot the system\n");
    fprintf(stderr, "  poweroff                Power off the system\n");
    fprintf(stderr, "  halt                    Halt the system\n");
}

static int skip_options(int argc, char *argv[], int *quiet)
{
    int i = 1;
    *quiet = 0;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0)
            *quiet = 1;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
            return -1;
        else if (strcmp(argv[i], "--version") == 0)
            return -2;
        i++;
    }
    return i;
}

int main(int argc, char *argv[])
{
    int quiet;
    int ci = skip_options(argc, argv, &quiet);
    if (ci == -1) { print_usage(); return 0; }
    if (ci == -2) { printf("systemd-shimd 0.1.0\n"); return 0; }

    if (ci >= argc) { print_usage(); return 1; }

    InitType init = detect_init_system();
    if (init == INIT_UNKNOWN) {
        fprintf(stderr, "systemd-shimd: unsupported init system\n");
        return 1;
    }

    const char *cmd = argv[ci];

    if (strcmp(cmd, "start") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl start NAME\n"); return 1; }
        return unit_start(argv[ci + 1]);
    }
    if (strcmp(cmd, "stop") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl stop NAME\n"); return 1; }
        return unit_stop(argv[ci + 1]);
    }
    if (strcmp(cmd, "restart") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl restart NAME\n"); return 1; }
        return unit_restart(argv[ci + 1]);
    }
    if (strcmp(cmd, "try-restart") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl try-restart NAME\n"); return 1; }
        return unit_try_restart(argv[ci + 1]);
    }
    if (strcmp(cmd, "reload") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl reload NAME\n"); return 1; }
        return unit_reload(argv[ci + 1]);
    }
    if (strcmp(cmd, "reload-or-restart") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl reload-or-restart NAME\n"); return 1; }
        return unit_reload_or_restart(argv[ci + 1]);
    }
    if (strcmp(cmd, "try-reload-or-restart") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl try-reload-or-restart NAME\n"); return 1; }
        return unit_try_reload_or_restart(argv[ci + 1]);
    }
    if (strcmp(cmd, "status") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl status NAME\n"); return 1; }
        return unit_status(argv[ci + 1]);
    }
    if (strcmp(cmd, "enable") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl enable NAME\n"); return 1; }
        return unit_enable(argv[ci + 1]);
    }
    if (strcmp(cmd, "disable") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl disable NAME\n"); return 1; }
        return unit_disable(argv[ci + 1]);
    }
    if (strcmp(cmd, "reenable") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl reenable NAME\n"); return 1; }
        return unit_reenable(argv[ci + 1]);
    }
    if (strcmp(cmd, "mask") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl mask NAME\n"); return 1; }
        return unit_mask(argv[ci + 1]);
    }
    if (strcmp(cmd, "unmask") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl unmask NAME\n"); return 1; }
        return unit_unmask(argv[ci + 1]);
    }
    if (strcmp(cmd, "preset") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl preset NAME\n"); return 1; }
        return unit_preset(argv[ci + 1]);
    }
    if (strcmp(cmd, "link") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl link PATH\n"); return 1; }
        return unit_link(argv[ci + 1]);
    }
    if (strcmp(cmd, "is-active") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl is-active NAME\n"); return 1; }
        int rc = unit_is_active(argv[ci + 1]);
        if (!quiet) printf("%s\n", rc == 0 ? "active" : "inactive");
        return rc;
    }
    if (strcmp(cmd, "is-enabled") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl is-enabled NAME\n"); return 1; }
        int rc = unit_is_enabled(argv[ci + 1]);
        if (!quiet) printf("%s\n", rc == 0 ? "enabled" : "disabled");
        return rc;
    }
    if (strcmp(cmd, "is-failed") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl is-failed NAME\n"); return 1; }
        int rc = unit_is_failed(argv[ci + 1]);
        if (!quiet) printf("%s\n", rc != 0 ? "failed" : "active");
        return rc;
    }
    if (strcmp(cmd, "is-system-running") == 0) {
        if (!quiet) printf("running\n");
        return 0;
    }
    if (strcmp(cmd, "kill") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl kill NAME [SIGNAL]\n"); return 1; }
        return unit_kill(argv[ci + 1], ci + 2 < argc ? argv[ci + 2] : "TERM");
    }
    if (strcmp(cmd, "show") == 0) {
        return unit_show(ci + 1 < argc ? argv[ci + 1] : NULL);
    }
    if (strcmp(cmd, "cat") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl cat NAME\n"); return 1; }
        return unit_cat(argv[ci + 1]);
    }
    if (strcmp(cmd, "list-units") == 0) {
        char **units = unit_list();
        if (!units) {
            fprintf(stderr, "systemd-shimd: listing units not supported\n");
            return 1;
        }
        if (!quiet) {
            printf("  UNIT                LOAD   ACTIVE     SUB\n");
            for (int i = 0; units[i]; i++) {
                printf("  %-20s loaded active     running\n", units[i]);
                free(units[i]);
            }
        }
        free(units);
        return 0;
    }
    if (strcmp(cmd, "list-unit-files") == 0) {
        if (!quiet) return unit_list_files();
        return unit_list_files();
    }
    if (strcmp(cmd, "list-dependencies") == 0) {
        /* stub: just print the unit name */
        if (!quiet && ci + 1 < argc) printf("%s\n", argv[ci + 1]);
        return 0;
    }
    if (strcmp(cmd, "get-default") == 0) {
        printf("multi-user.target\n");
        return 0;
    }
    if (strcmp(cmd, "set-default") == 0) {
        if (ci + 1 >= argc) { fprintf(stderr, "Usage: systemctl set-default TARGET\n"); return 1; }
        /* no-op stub */
        return 0;
    }
    if (strcmp(cmd, "daemon-reload") == 0) return daemon_reload();
    if (strcmp(cmd, "daemon-reexec") == 0) return unit_daemon_reexec();
    if (strcmp(cmd, "reset-failed") == 0) return unit_reset_failed(ci + 1 < argc ? argv[ci + 1] : NULL);
    if (strcmp(cmd, "help") == 0) { print_usage(); return 0; }
    if (strcmp(cmd, "reboot") == 0) return system_reboot();
    if (strcmp(cmd, "poweroff") == 0) return system_poweroff();
    if (strcmp(cmd, "halt") == 0) return system_halt();

    fprintf(stderr, "systemd-shimd: unknown command '%s'\n", cmd);
    print_usage();
    return 1;
}
