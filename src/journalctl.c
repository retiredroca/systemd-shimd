#include "systemd-shimd.h"
#include <sys/inotify.h>
#include <limits.h>
#include <errno.h>

#define LOG_PATHS_COUNT 6
static const char *log_paths[LOG_PATHS_COUNT] = {
    "/var/log/messages/current",
    "/var/log/messages",
    "/var/log/syslog/current",
    "/var/log/syslog",
    "/var/log/everything/current",
    "/var/log/rc.log",
};

typedef struct {
    char   *path;
    FILE   *fp;
    int     inotify_fd;
    int     watch_fd;
    char   *unit_filter;
    char   *since;
    char   *until;
    bool    eof;
} LogReader;

static LogReader *log_reader_open(void)
{
    LogReader *r = calloc(1, sizeof(LogReader));
    if (!r) return NULL;
    r->inotify_fd = -1;
    r->watch_fd   = -1;
    for (int i = 0; i < LOG_PATHS_COUNT; i++) {
        FILE *fp = fopen(log_paths[i], "r");
        if (fp) { r->path = strdup(log_paths[i]); r->fp = fp; return r; }
    }
    fprintf(stderr, "systemd-shimd: no log file found\n");
    free(r);
    return NULL;
}

static void log_reader_close(LogReader *r)
{
    if (!r) return;
    if (r->fp) fclose(r->fp);
    if (r->watch_fd >= 0 && r->inotify_fd >= 0)
        inotify_rm_watch(r->inotify_fd, r->watch_fd);
    if (r->inotify_fd >= 0) close(r->inotify_fd);
    free(r->path); free(r->unit_filter); free(r->since); free(r->until);
    free(r);
}

static int log_reader_seek_tail(LogReader *r, int nlines)
{
    if (!r || !r->fp) return -1;
    if (nlines <= 0) { rewind(r->fp); return 0; }
    fseek(r->fp, 0, SEEK_END);
    long end = ftell(r->fp);
    if (end <= 0) return 0;
    long pos = end;
    int count = 0;
    char buf[4096];
    while (pos > 0 && count <= nlines) {
        long chunk = pos > (long)sizeof(buf) ? (long)sizeof(buf) : pos;
        pos -= chunk;
        fseek(r->fp, pos, SEEK_SET);
        size_t rs = fread(buf, 1, (size_t)chunk, r->fp);
        for (size_t i = rs; i > 0; i--)
            if (buf[i - 1] == '\n') {
                count++;
                if (count > nlines) { pos += (long)(i); fseek(r->fp, pos, SEEK_SET); return 0; }
            }
    }
    rewind(r->fp);
    return 0;
}

static int log_reader_next_line(LogReader *r, char **line)
{
    if (!r || !r->fp || r->eof) return -1;
    char buf[8192];
    while (1) {
        if (!fgets(buf, sizeof(buf), r->fp)) {
            if (feof(r->fp)) r->eof = true;
            return -1;
        }
        chomp(buf);
        if (r->unit_filter && !strstr(buf, r->unit_filter)) continue;
        *line = strdup(buf);
        return 0;
    }
}

static int log_reader_wait(LogReader *r, int timeout_ms)
{
    if (!r || !r->path) return -1;
    if (r->inotify_fd < 0) {
        r->inotify_fd = inotify_init1(IN_NONBLOCK);
        if (r->inotify_fd < 0) return -1;
        r->watch_fd = inotify_add_watch(r->inotify_fd, r->path, IN_MODIFY);
        if (r->watch_fd < 0) { close(r->inotify_fd); r->inotify_fd = -1; return -1; }
    }
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(r->inotify_fd, &fds);
    struct timeval tv = { .tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000 };
    int ret = select(r->inotify_fd + 1, &fds, NULL, NULL, timeout_ms < 0 ? NULL : &tv);
    if (ret <= 0) return ret;
    char event_buf[sizeof(struct inotify_event) + NAME_MAX + 1];
    if (read(r->inotify_fd, event_buf, sizeof(event_buf)) < 0) return -1;
    clearerr(r->fp);
    r->eof = false;
    return 1;
}

static void print_usage(void)
{
    fprintf(stderr, "Usage: journalctl [OPTIONS...]\n\nOptions:\n");
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
            if (i + 1 < argc) { lines = atoi(argv[++i]); if (lines < 0) lines = 0; }
        } else if (strncmp(argv[i], "-n", 2) == 0) {
            lines = atoi(argv[i] + 2); if (lines < 0) lines = 0;
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
            print_usage(); return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("systemd-shimd 0.1.0\n"); return 0;
        } else {
            fprintf(stderr, "journalctl: unknown option '%s'\n", argv[i]); return 1;
        }
    }

    LogReader *reader = log_reader_open();
    if (!reader) return 1;

    if (unit) { free(reader->unit_filter); reader->unit_filter = strdup(unit); }
    if (since) { free(reader->since); reader->since = strdup(since); }
    if (until) { free(reader->until); reader->until = strdup(until); }

    log_reader_seek_tail(reader, lines);

    char *line = NULL;
    while (1) {
        while (log_reader_next_line(reader, &line) == 0) {
            printf("%s\n", line);
            free(line); line = NULL;
        }
        if (!follow) break;
        log_reader_wait(reader, -1);
    }

    log_reader_close(reader);
    return 0;
}
