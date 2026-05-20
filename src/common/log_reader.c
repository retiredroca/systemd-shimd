#define _GNU_SOURCE
#include "log_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/inotify.h>
#include <limits.h>

static void chomp(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
        s[--len] = '\0';
}

#define LOG_PATHS_COUNT 6
static const char *log_paths[LOG_PATHS_COUNT] = {
    "/var/log/messages/current",
    "/var/log/messages",
    "/var/log/syslog/current",
    "/var/log/syslog",
    "/var/log/everything/current",
    "/var/log/rc.log",
};

struct LogReader {
    char   *path;
    FILE   *fp;
    int     inotify_fd;
    int     watch_fd;
    char   *unit_filter;
    char   *since;
    char   *until;
    bool    eof;
};

LogReader *log_reader_open(void)
{
    LogReader *r = calloc(1, sizeof(LogReader));
    if (!r) return NULL;

    r->inotify_fd = -1;
    r->watch_fd   = -1;

    for (int i = 0; i < LOG_PATHS_COUNT; i++) {
        FILE *fp = fopen(log_paths[i], "r");
        if (fp) {
            r->path = strdup(log_paths[i]);
            r->fp   = fp;
            return r;
        }
    }

    fprintf(stderr, "systemd-shimd: no log file found (tried /var/log/messages, /var/log/syslog)\n");
    free(r);
    return NULL;
}

void log_reader_close(LogReader *r)
{
    if (!r) return;
    if (r->fp) fclose(r->fp);
    if (r->watch_fd >= 0 && r->inotify_fd >= 0)
        inotify_rm_watch(r->inotify_fd, r->watch_fd);
    if (r->inotify_fd >= 0) close(r->inotify_fd);
    free(r->path);
    free(r->unit_filter);
    free(r->since);
    free(r->until);
    free(r);
}

int log_reader_seek_tail(LogReader *r, int nlines)
{
    if (!r || !r->fp) return -1;
    if (nlines <= 0) {
        rewind(r->fp);
        return 0;
    }

    fseek(r->fp, 0, SEEK_END);
    long end = ftell(r->fp);
    if (end <= 0) return 0;

    long pos = end;
    int count = 0;
    char buf[4096];

    while (pos > 0 && count <= nlines) {
        long chunk_size = pos > (long)sizeof(buf) ? (long)sizeof(buf) : pos;
        pos -= chunk_size;
        fseek(r->fp, pos, SEEK_SET);
        size_t read_size = fread(buf, 1, (size_t)chunk_size, r->fp);

        for (size_t i = read_size; i > 0; i--) {
            if (buf[i - 1] == '\n') {
                count++;
                if (count > nlines) {
                    pos += (long)(i);
                    fseek(r->fp, pos, SEEK_SET);
                    return 0;
                }
            }
        }
    }

    rewind(r->fp);
    return 0;
}

int log_reader_next_line(LogReader *r, char **line)
{
    if (!r || !r->fp || r->eof) return -1;

    char buf[8192];
    while (1) {
        if (!fgets(buf, sizeof(buf), r->fp)) {
            if (feof(r->fp)) r->eof = true;
            return -1;
        }

        chomp(buf);

        if (r->unit_filter && !strstr(buf, r->unit_filter))
            continue;

        *line = strdup(buf);
        return 0;
    }
}

int log_reader_wait(LogReader *r, int timeout_ms)
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

    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };

    int ret = select(r->inotify_fd + 1, &fds, NULL, NULL,
                     timeout_ms < 0 ? NULL : &tv);
    if (ret <= 0) return ret;

    char event_buf[sizeof(struct inotify_event) + NAME_MAX + 1];
    if (read(r->inotify_fd, event_buf, sizeof(event_buf)) < 0)
        return -1;

    clearerr(r->fp);
    r->eof = false;
    return 1;
}

void log_reader_set_unit_filter(LogReader *r, const char *unit)
{
    if (!r) return;
    free(r->unit_filter);
    r->unit_filter = unit ? strdup(unit) : NULL;
}

void log_reader_set_since(LogReader *r, const char *since)
{
    if (!r) return;
    free(r->since);
    r->since = since ? strdup(since) : NULL;
}

void log_reader_set_until(LogReader *r, const char *until)
{
    if (!r) return;
    free(r->until);
    r->until = until ? strdup(until) : NULL;
}
