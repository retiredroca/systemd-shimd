#include "systemd-shimd.hpp"

using namespace shimd;

static void print_usage()
{
    std::fprintf(stderr,
        "Usage: systemctl [OPTIONS...] COMMAND [UNIT...]\n"
        "\nCommands:\n"
        "  start NAME              Start a unit\n"
        "  stop NAME               Stop a unit\n"
        "  restart NAME            Restart a unit\n"
        "  try-restart NAME        Restart a unit if active\n"
        "  reload NAME             Reload a unit\n"
        "  reload-or-restart NAME  Reload or restart a unit\n"
        "  status NAME             Show status of a unit\n"
        "  enable NAME             Enable a unit\n"
        "  disable NAME            Disable a unit\n"
        "  reenable NAME           Re-enable a unit\n"
        "  mask NAME               Mask a unit\n"
        "  unmask NAME             Unmask a unit\n"
        "  is-active NAME          Check if a unit is active\n"
        "  is-enabled NAME         Check if a unit is enabled\n"
        "  is-failed NAME          Check if a unit has failed\n"
        "  is-system-running       Check if system is running\n"
        "  list-units              List available units\n"
        "  list-unit-files         List installed unit files\n"
        "  list-dependencies NAME  List unit dependencies\n"
        "  show [NAME]             Show unit or manager properties\n"
        "  cat NAME                Show unit file contents\n"
        "  edit NAME               Edit unit file\n"
        "  kill NAME [SIGNAL]      Send signal to a unit\n"
        "  add-wants TARGET UNIT   Add a wants dependency (advisory)\n"
        "  daemon-reload           Reload manager configuration\n"
        "  daemon-reexec           Reexecute the manager\n"
        "  reset-failed [NAME]     Reset failed state\n"
        "  preset NAME             Enable/disable per preset policy\n"
        "  get-default             Show default target\n"
        "  set-default TARGET      Set default target\n"
        "  reboot                  Reboot the system\n"
        "  poweroff                Power off the system\n"
        "  halt                    Halt the system\n"
    );
}

static int skip_options(int argc, char *argv[], bool &quiet)
{
    int i = 1;
    quiet = false;
    while (i < argc && argv[i][0] == '-') {
        auto a = argv[i];
        if (std::strcmp(a, "--quiet") == 0 || std::strcmp(a, "-q") == 0)
            quiet = true;
        else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0)
            return -1;
        else if (std::strcmp(a, "--version") == 0)
            return -2;
        i++;
    }
    return i;
}

int main(int argc, char *argv[])
{
    bool quiet;
    int ci = skip_options(argc, argv, quiet);
    if (ci == -1) { print_usage(); return 0; }
    if (ci == -2) { std::printf("systemd-shimd 0.1.0\n"); return 0; }

    if (ci >= argc) { print_usage(); return 1; }

    auto init = detect();
    if (init == Init::Unknown) {
        std::fprintf(stderr, "systemd-shimd: unsupported init system\n");
        return 1;
    }

    std::string cmd = argv[ci];

    auto require_arg = [&](const char *usage) -> std::string {
        if (ci + 1 >= argc) {
            std::fprintf(stderr, "Usage: systemctl %s\n", usage);
            std::exit(1);
        }
        return argv[ci + 1];
    };

    if (cmd == "start") {
        auto n = require_arg("start NAME");
        return start(n);
    }
    if (cmd == "stop") {
        auto n = require_arg("stop NAME");
        return stop(n);
    }
    if (cmd == "restart") {
        auto n = require_arg("restart NAME");
        return restart(n);
    }
    if (cmd == "try-restart") {
        auto n = require_arg("try-restart NAME");
        return try_restart(n);
    }
    if (cmd == "reload") {
        auto n = require_arg("reload NAME");
        return reload(n);
    }
    if (cmd == "reload-or-restart") {
        auto n = require_arg("reload-or-restart NAME");
        return reload_or_restart(n);
    }
    if (cmd == "try-reload-or-restart") {
        auto n = require_arg("try-reload-or-restart NAME");
        return try_reload_or_restart(n);
    }
    if (cmd == "status") {
        auto n = require_arg("status NAME");
        return status(n);
    }
    if (cmd == "enable") {
        auto n = require_arg("enable NAME");
        return enable(n);
    }
    if (cmd == "disable") {
        auto n = require_arg("disable NAME");
        return disable(n);
    }
    if (cmd == "reenable") {
        auto n = require_arg("reenable NAME");
        return reenable(n);
    }
    if (cmd == "mask") {
        auto n = require_arg("mask NAME");
        return mask(n);
    }
    if (cmd == "unmask") {
        auto n = require_arg("unmask NAME");
        return unmask(n);
    }
    if (cmd == "preset") {
        auto n = require_arg("preset NAME");
        return preset(n);
    }
    if (cmd == "link") {
        auto n = require_arg("link PATH");
        return link(n);
    }
    if (cmd == "edit") {
        auto n = require_arg("edit NAME");
        return edit(n);
    }
    if (cmd == "add-wants") {
        if (ci + 2 >= argc) {
            std::fprintf(stderr, "Usage: systemctl add-wants TARGET UNIT\n");
            return 1;
        }
        return add_wants(argv[ci + 1], argv[ci + 2]);
    }
    if (cmd == "is-active") {
        auto n = require_arg("is-active NAME");
        auto rc = is_active(n);
        if (!quiet) std::printf("%s\n", rc ? "active" : "inactive");
        return rc ? 0 : 1;
    }
    if (cmd == "is-enabled") {
        auto n = require_arg("is-enabled NAME");
        auto rc = is_enabled(n);
        if (!quiet) std::printf("%s\n", rc ? "enabled" : "disabled");
        return rc ? 0 : 1;
    }
    if (cmd == "is-failed") {
        auto n = require_arg("is-failed NAME");
        auto rc = is_failed(n);
        if (!quiet) std::printf("%s\n", rc ? "failed" : "active");
        return rc;
    }
    if (cmd == "is-system-running") {
        if (!quiet) std::printf("running\n");
        return 0;
    }
    if (cmd == "kill") {
        auto n = require_arg("kill NAME [SIGNAL]");
        auto sig = (ci + 2 < argc) ? argv[ci + 2] : "TERM";
        return kill(n, sig);
    }
    if (cmd == "show") {
        auto n = (ci + 1 < argc) ? argv[ci + 1] : "";
        return show(n);
    }
    if (cmd == "cat") {
        auto n = require_arg("cat NAME");
        return cat(n);
    }
    if (cmd == "list-units") {
        auto units = list();
        if (units.empty()) {
            std::fprintf(stderr, "systemd-shimd: listing units not supported\n");
            return 1;
        }
        if (!quiet) {
            std::printf("  UNIT                LOAD   ACTIVE     SUB\n");
            for (auto &u : units)
                std::printf("  %-20s loaded active     running\n", u.c_str());
        }
        return 0;
    }
    if (cmd == "list-unit-files") {
        auto units = list_files();
        if (!quiet)
            for (auto &u : units) std::printf("%s\n", u.c_str());
        return 0;
    }
    if (cmd == "list-dependencies") {
        if (!quiet && ci + 1 < argc) std::printf("%s\n", argv[ci + 1]);
        return 0;
    }
    if (cmd == "get-default") {
        std::printf("multi-user.target\n");
        return 0;
    }
    if (cmd == "set-default") {
        /* stub */
        return 0;
    }
    if (cmd == "daemon-reload") return daemon_reload();
    if (cmd == "daemon-reexec") return daemon_reexec();
    if (cmd == "reset-failed") return reset_failed(ci + 1 < argc ? argv[ci + 1] : "");
    if (cmd == "help") { print_usage(); return 0; }
    if (cmd == "reboot") return reboot();
    if (cmd == "poweroff") return poweroff();
    if (cmd == "halt") return halt();

    std::fprintf(stderr, "systemd-shimd: unknown command '%s'\n", cmd.c_str());
    print_usage();
    return 1;
}
