#include "../common/log_reader.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static void print_usage(void)
{
    fprintf(stderr, "Usage: journalctl [OPTIONS...]\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -f, --follow           Follow the journal\n");
    fprintf(stderr, "  -n, --lines=N          Number of journal entries to show\n");
    fprintf(stderr, "  -u, --unit=UNIT        Show messages from specified unit\n");
    fprintf(stderr, "  --since=DATE           Show messages since specified date\n");
    fprintf(stderr, "  --until=DATE           Show messages until specified date\n");
    fprintf(stderr, "  -h, --help             Show this help\n");
    fprintf(stderr, "  --version              Show version\n");
}

int main(int argc, char *argv[])
{
    int   lines    = 10;
    bool  follow   = false;
    char *unit     = NULL;
    char *since    = NULL;
    char *until    = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--follow") == 0) {
            follow = true;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--lines") == 0) {
            if (i + 1 < argc) {
                lines = atoi(argv[++i]);
                if (lines < 0) lines = 0;
            }
        } else if (strncmp(argv[i], "-n", 2) == 0) {
            lines = atoi(argv[i] + 2);
            if (lines < 0) lines = 0;
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--unit") == 0) {
            if (i + 1 < argc) unit = argv[++i];
        } else if (strncmp(argv[i], "-u", 2) == 0) {
            unit = argv[i] + 2;
        } else if (strncmp(argv[i], "--unit=", 7) == 0) {
            unit = argv[i] + 7;
        } else if (strncmp(argv[i], "--since=", 8) == 0) {
            since = argv[i] + 8;
        } else if (strncmp(argv[i], "--until=", 8) == 0) {
            until = argv[i] + 8;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("systemd-shimd 0.1.0\n");
            return 0;
        } else {
            fprintf(stderr, "journalctl: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    LogReader *reader = log_reader_open();
    if (!reader) return 1;

    if (unit) log_reader_set_unit_filter(reader, unit);
    if (since) log_reader_set_since(reader, since);
    if (until) log_reader_set_until(reader, until);

    if (follow) {
        log_reader_seek_tail(reader, lines);
    } else {
        log_reader_seek_tail(reader, lines);
    }

    char *line = NULL;
    while (1) {
        while (log_reader_next_line(reader, &line) == 0) {
            printf("%s\n", line);
            free(line);
            line = NULL;
        }

        if (!follow) break;

        log_reader_wait(reader, -1);
    }

    log_reader_close(reader);
    return 0;
}
