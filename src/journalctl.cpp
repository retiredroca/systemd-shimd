#include "systemd-shimd.hpp"

using namespace shimd;

static const char *log_paths[] = {
    "/var/log/messages",
    "/var/log/syslog",
    "/var/log/rc.log",
    "/var/log/system.log",
    "/var/log/everything/current",
    "/var/log/all.log",
    nullptr
};

static std::string find_log()
{
    for (auto p = log_paths; *p; p++) {
        if (access(*p, R_OK) == 0) return *p;
    }
    if (auto out = run_cmd_capture({"find", "/var/log", "-name", "*.log",
                                     "-type", "f", "-readable",  nullptr})) {
        std::istringstream ss(*out);
        std::string line;
        if (std::getline(ss, line)) return line;
    }
    return {};
}

static int follow(const std::string &path)
{
    FILE *fp = fopen(path.c_str(), "r");
    if (!fp) return 1;

    /* seek to end */
    fseek(fp, 0, SEEK_END);

    /* poll for new content */
    char buf[4096];
    while (true) {
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
            fwrite(buf, 1, n, stdout);
        fflush(stdout);
        struct timespec ts = {0, 100000000};  /* 100ms */
        nanosleep(&ts, nullptr);
    }

    fclose(fp);
    return 0;
}

static int show_lines(const std::string &path, int n)
{
    FILE *fp = fopen(path.c_str(), "r");
    if (!fp) return 1;

    /* count lines */
    int lines = 0;
    int c;
    while ((c = fgetc(fp)) != EOF)
        if (c == '\n') lines++;
    rewind(fp);

    int skip = (lines > n) ? lines - n : 0;
    char buf[4096];
    while (skip > 0 && fgets(buf, sizeof(buf), fp))
        if (buf[strlen(buf) - 1] == '\n') skip--;

    while (fgets(buf, sizeof(buf), fp))
        fputs(buf, stdout);

    fclose(fp);
    return 0;
}

int main(int argc, char *argv[])
{
    bool follow_mode = false;
    int lines = 0;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-f") == 0 ||
            std::strcmp(argv[i], "--follow") == 0)
            follow_mode = true;
        else if (std::strcmp(argv[i], "-n") == 0 ||
                 std::strcmp(argv[i], "--lines") == 0) {
            if (i + 1 < argc) lines = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-u") == 0) {
            /* consume the next arg */
            if (i + 1 < argc) i++;
        } else if (std::strcmp(argv[i], "--since") == 0) {
            if (i + 1 < argc) i++;
        } else if (std::strcmp(argv[i], "--until") == 0) {
            if (i + 1 < argc) i++;
        } else if (std::strcmp(argv[i], "--help") == 0 ||
                   std::strcmp(argv[i], "-h") == 0) {
            std::printf(
                "Usage: journalctl [OPTIONS...]\n"
                "\nOptions:\n"
                "  -f, --follow           Follow the journal\n"
                "  -n, --lines=N          Number of journal entries to show\n"
                "  -u, --unit=UNIT        Show only specified unit\n"
                "  --since=DATE           Show entries since date\n"
                "  --until=DATE           Show entries until date\n"
            );
            return 0;
        }
    }

    auto path = find_log();
    if (path.empty()) {
        std::fprintf(stderr, "journalctl: no readable log file found\n");
        return 1;
    }

    if (follow_mode) return follow(path);

    if (lines > 0) return show_lines(path, lines);

    /* default: show last 50 lines */
    return show_lines(path, 50);
}
