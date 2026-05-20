#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <inttypes.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <poll.h>
#include <signal.h>
#include <arpa/inet.h>

/* ──────────────────────────────────────────────────────────────
 * Types shared across sections
 * ──────────────────────────────────────────────────────────── */

#define _sd_printf_(a, b) __attribute__((format(printf, a, b)))
#define _sd_sentinel_     __attribute__((sentinel))
#define _SD_BEGIN_DECLARATIONS
#define _SD_END_DECLARATIONS

typedef uint64_t usec_t;

/* sd_id128_t */
typedef union {
    uint8_t  bytes[16];
    uint64_t qwords[2];
} sd_id128_t;

/* sd_journal (opaque) */
typedef struct sd_journal sd_journal;
typedef struct sd_login_monitor sd_login_monitor;

/* sd_event */
typedef struct sd_event sd_event;
typedef struct sd_event_source sd_event_source;

enum {
    SD_EVENT_OFF = 0,
    SD_EVENT_ON  = 1,
};

enum {
    SD_EVENT_PRIORITY_IMPORTANT = -100,
    SD_EVENT_PRIORITY_NORMAL   = 0,
    SD_EVENT_PRIORITY_IDLE     = 100,
};

/* ──────────────────────────────────────────────────────────────
 * sd_notify – notify service manager about state changes
 * ──────────────────────────────────────────────────────────── */

static int notify_send(const char *socket_path, const char *state)
{
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    size_t path_len = strlen(socket_path);
    if (path_len >= sizeof(addr.sun_path)) return -E2BIG;
    memcpy(addr.sun_path, socket_path, path_len + 1);

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -errno;

    if (addr.sun_path[0] == '@')
        addr.sun_path[0] = '\0';

    ssize_t r = sendto(fd, state, strlen(state), 0,
                       (struct sockaddr *)&addr,
                       offsetof(struct sockaddr_un, sun_path) + path_len);
    int saved_errno = errno;
    close(fd);

    if (r < 0) return -saved_errno;
    return 1;
}

int sd_notify(int unset_environment, const char *state)
{
    const char *socket_path = getenv("NOTIFY_SOCKET");
    if (!socket_path || !*socket_path) return 0;
    if (!state) return -EINVAL;

    int r = notify_send(socket_path, state);
    if (r > 0 && unset_environment)
        unsetenv("NOTIFY_SOCKET");
    return r;
}

int sd_notifyf(int unset_environment, const char *format, ...)
{
    if (!format) return -EINVAL;

    char buf[4096];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    if (n < 0 || (size_t)n >= sizeof(buf)) return -ENOMEM;
    return sd_notify(unset_environment, buf);
}

int sd_pid_notify(pid_t pid, int unset_environment, const char *state)
{
    if (pid == 0) pid = getpid();
    if (pid != getpid())
        return -ENOSYS; /* not implemented for remote processes */
    return sd_notify(unset_environment, state);
}

int sd_pid_notifyf(pid_t pid, int unset_environment, const char *format, ...)
{
    if (!format) return -EINVAL;

    char buf[4096];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    if (n < 0 || (size_t)n >= sizeof(buf)) return -ENOMEM;
    return sd_pid_notify(pid, unset_environment, buf);
}

int sd_pid_notify_with_fds(pid_t pid, int unset_environment,
                           const char *state, const int *fds,
                           unsigned n_fds)
{
    return -ENOSYS;
}

/* ──────────────────────────────────────────────────────────────
 * sd_booted – detect whether systemd is the init
 * ──────────────────────────────────────────────────────────── */

int sd_booted(void)
{
    struct stat st;
    if (stat("/run/systemd/system/", &st) == 0 && S_ISDIR(st.st_mode))
        return 1;
    return 0;
}

/* ──────────────────────────────────────────────────────────────
 * sd_journal – logging (forward to syslog)
 * ──────────────────────────────────────────────────────────── */

#include <syslog.h>

int sd_journal_print(int priority, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vsyslog(priority, format, ap);
    va_end(ap);
    return 0;
}

int sd_journal_printv(int priority, const char *format, va_list ap)
{
    vsyslog(priority, format, ap);
    return 0;
}

int sd_journal_perror(const char *message)
{
    syslog(LOG_ERR, "%s: %m", message ? message : "error");
    return 0;
}

int sd_journal_sendv(const struct iovec *iov, int n)
{
    int priority = LOG_INFO;
    for (int i = 0; i < n; i++) {
        if (iov[i].iov_base &&
            strncmp(iov[i].iov_base, "PRIORITY=", 9) == 0) {
            priority = atoi((const char *)iov[i].iov_base + 9);
        }
    }
    for (int i = 0; i < n; i++) {
        if (iov[i].iov_base && iov[i].iov_len > 0) {
            syslog(priority, "%.*s", (int)iov[i].iov_len,
                   (const char *)iov[i].iov_base);
        }
    }
    return 0;
}

int sd_journal_send(const char *format, ...)
{
    if (!format) return -EINVAL;

    char buf[4096];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    if (n < 0 || (size_t)n >= sizeof(buf)) return -ENOMEM;

    struct iovec iov = { .iov_base = buf, .iov_len = (size_t)n };
    return sd_journal_sendv(&iov, 1);
}

/* sd_journal_open etc. – stubs since there's no journald */

int sd_journal_open(sd_journal **ret, int flags)
{
    return -ENOENT;
}

int sd_journal_open_directory(sd_journal **ret, const char *path, int flags)
{
    return -ENOENT;
}

int sd_journal_open_files(sd_journal **ret, const char **paths, int flags)
{
    return -ENOENT;
}

int sd_journal_open_container(sd_journal **ret, const char *machine, int flags)
{
    return -ENOENT;
}

int sd_journal_close(sd_journal *j) { return 0; }

int sd_journal_next(sd_journal *j) { return 0; }

int sd_journal_previous(sd_journal *j) { return 0; }

int sd_journal_next_skip(sd_journal *j, uint64_t skip) { return 0; }

int sd_journal_previous_skip(sd_journal *j, uint64_t skip) { return 0; }

int sd_journal_get_data(sd_journal *j, const char *field,
                        const void **data, size_t *l)
{
    return -ENOENT;
}

int sd_journal_enumerate_data(sd_journal *j, const void **data, size_t *l)
{
    return 0;
}

int sd_journal_set_data_threshold(sd_journal *j, size_t sz) { return 0; }

int sd_journal_get_realtime_usec(sd_journal *j, uint64_t *ret)
{
    return -ENOENT;
}

int sd_journal_get_monotonic_usec(sd_journal *j, uint64_t *ret,
                                  sd_id128_t *ret_boot_id)
{
    return -ENOENT;
}

int sd_journal_get_cursor(sd_journal *j, char **ret_cursor)
{
    return -ENOENT;
}

int sd_journal_seek_head(sd_journal *j) { return 0; }

int sd_journal_seek_tail(sd_journal *j) { return 0; }

int sd_journal_seek_cursor(sd_journal *j, const char *cursor) { return 0; }

int sd_journal_add_match(sd_journal *j, const void *data, size_t sz)
{
    return 0;
}

int sd_journal_add_disjunction(sd_journal *j) { return 0; }

int sd_journal_add_conjunction(sd_journal *j) { return 0; }

int sd_journal_flush_matches(sd_journal *j) { return 0; }

int sd_journal_get_usage(sd_journal *j, uint64_t *bytes) { return -ENOENT; }

int sd_journal_get_cutoff_realtime_usec(sd_journal *j, uint64_t *from,
                                         uint64_t *to)
{
    return -ENOENT;
}

int sd_journal_get_cutoff_monotonic_usec(sd_journal *j, uint64_t *from,
                                          uint64_t *to, sd_id128_t *boot_id)
{
    return -ENOENT;
}

int sd_journal_get_events(sd_journal *j) { return POLLIN; }

int sd_journal_get_fd(sd_journal *j) { return -ENOENT; }

int sd_journal_reliable_fd(sd_journal *j) { return 0; }

int sd_journal_process(sd_journal *j) { return 0; }

int sd_journal_wait(sd_journal *j, uint64_t timeout_usec) { return 0; }

int sd_journal_enumerate_unique(sd_journal *j, const void **data, size_t *l)
{
    return 0;
}

int sd_journal_enumerate_fields(sd_journal *j, const char **field,
                                 size_t *l)
{
    return 0;
}

int sd_journal_has_runtime_files(sd_journal *j) { return 0; }

int sd_journal_has_persistent_files(sd_journal *j) { return 0; }

int sd_journal_get_seqnum(sd_journal *j, uint64_t *seqnum)
{
    return -ENOENT;
}

/* ──────────────────────────────────────────────────────────────
 * sd_daemon – socket/fd identification (fully portable)
 * ──────────────────────────────────────────────────────────── */

int sd_is_fifo(int fd, const char *path)
{
    struct stat st;
    if (fstat(fd, &st) < 0) return -errno;
    if (!S_ISFIFO(st.st_mode)) return 0;
    if (path) {
        struct stat st2;
        if (stat(path, &st2) < 0) return -errno;
        return st.st_dev == st2.st_dev && st.st_ino == st2.st_ino;
    }
    return 1;
}

int sd_is_special(int fd, const char *path)
{
    struct stat st;
    if (fstat(fd, &st) < 0) return -errno;
    if (!S_ISREG(st.st_mode) && !S_ISCHR(st.st_mode) &&
        !S_ISBLK(st.st_mode) && !S_ISFIFO(st.st_mode) &&
        !S_ISSOCK(st.st_mode))
        return 0;
    if (path) {
        struct stat st2;
        if (stat(path, &st2) < 0) return -errno;
        return st.st_dev == st2.st_dev && st.st_ino == st2.st_ino;
    }
    return 1;
}

int sd_is_socket(int fd, int family, int type, int listening)
{
    struct stat st;
    if (fstat(fd, &st) < 0) return -errno;
    if (!S_ISSOCK(st.st_mode)) return 0;

    int so_type = 0;
    socklen_t l = sizeof(so_type);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &so_type, &l) < 0)
        return -errno;
    if (type != 0 && so_type != type) return 0;

    struct sockaddr_storage sa;
    l = sizeof(sa);
    if (getsockname(fd, (struct sockaddr *)&sa, &l) < 0)
        return -errno;

    if (family != 0 && sa.ss_family != family) return 0;

    if (listening >= 0) {
        int acc = 0;
        l = sizeof(acc);
        if (getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &acc, &l) < 0)
            return -errno;
        if ((listening > 0) != (acc > 0)) return 0;
    }

    return 1;
}

int sd_is_socket_inet(int fd, int family, int type, int listening,
                       uint16_t port)
{
    int r = sd_is_socket(fd, family, type, listening);
    if (r <= 0) return r;

    struct sockaddr_storage sa;
    socklen_t l = sizeof(sa);
    if (getsockname(fd, (struct sockaddr *)&sa, &l) < 0)
        return -errno;

    if (sa.ss_family == AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)&sa;
        if (port != 0 && ntohs(in->sin_port) != port) return 0;
    } else if (sa.ss_family == AF_INET6) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&sa;
        if (port != 0 && ntohs(in6->sin6_port) != port) return 0;
    } else {
        return 0;
    }

    return 1;
}

int sd_is_socket_unix(int fd, int family, int type, int listening,
                       const char *path, size_t length)
{
    int r = sd_is_socket(fd, AF_UNIX, type, listening);
    if (r <= 0) return r;

    if (path) {
        struct sockaddr_un un;
        socklen_t l = sizeof(un);
        if (getsockname(fd, (struct sockaddr *)&un, &l) < 0)
            return -errno;

        if (l < (socklen_t)offsetof(struct sockaddr_un, sun_path))
            return 0;

        size_t path_len = strlen(path);
        if (length > 0)
            path_len = length;

        if (path_len >= sizeof(un.sun_path))
            return 0;

        if (memcmp(un.sun_path, path, path_len) != 0) return 0;
    }

    (void)family;
    return 1;
}

int sd_is_mq(int fd, const char *path)
{
    return 0; /* not supported */
}

#define SD_LISTEN_FDS_START 3

int sd_listen_fds(int unset_environment)
{
    const char *e = getenv("LISTEN_PID");
    if (!e) return 0;
    pid_t pid = (pid_t)atoi(e);
    if (pid != getpid()) return 0;

    e = getenv("LISTEN_FDS");
    if (!e) return 0;
    int n = atoi(e);

    if (unset_environment) {
        unsetenv("LISTEN_PID");
        unsetenv("LISTEN_FDS");
        unsetenv("LISTEN_FDNAMES");
    }

    /* Validate that fds 3, 4, ..., 3+n-1 are open */
    int start = SD_LISTEN_FDS_START;
    for (int i = start; i < start + n; i++) {
        if (fcntl(i, F_GETFD) < 0) return -errno;
    }

    return n;
}

int sd_listen_fds_with_names(int unset_environment, char ***names)
{
    int n = sd_listen_fds(unset_environment);
    if (n <= 0) return n;

    const char *e = getenv("LISTEN_FDNAMES");
    if (names) {
        if (e && *e) {
            int count = 1;
            for (const char *p = e; *p; p++)
                if (*p == ':') count++;

            *names = calloc((size_t)(count + 1), sizeof(char *));

            char *dup = strdup(e);
            int idx = 0;
            char *save;
            char *tok = strtok_r(dup, ":", &save);
            while (tok && idx < count) {
                (*names)[idx++] = strdup(tok);
                tok = strtok_r(NULL, ":", &save);
            }
            (*names)[idx] = NULL;
            free(dup);
        } else {
            *names = calloc(2, sizeof(char *));
            char buf[16];
            for (int i = 0; i < n && i < 16; i++) {
                snprintf(buf, sizeof(buf), "unknown-%d", i);
                (*names)[i] = strdup(buf);
            }
            (*names)[n] = NULL;
        }
    }

    if (unset_environment) unsetenv("LISTEN_FDNAMES");
    return n;
}

int sd_notify_barrier(int unset_environment, uint64_t timeout_usec)
{
    return -ENOSYS;
}

/* ──────────────────────────────────────────────────────────────
 * sd_id128 – 128-bit unique identifiers
 * ──────────────────────────────────────────────────────────── */

int sd_id128_randomize(sd_id128_t *ret)
{
    if (!ret) return -EINVAL;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    ssize_t r = read(fd, ret->bytes, 16);
    int saved_errno = errno;
    close(fd);
    if (r < 0) return -saved_errno;
    if (r != 16) return -EIO;
    /* Turn into a valid v4 UUID */
    ret->bytes[6] = (ret->bytes[6] & 0x0F) | 0x40;
    ret->bytes[8] = (ret->bytes[8] & 0x3F) | 0x80;
    return 0;
}

int sd_id128_get_machine(sd_id128_t *ret)
{
    if (!ret) return -EINVAL;
    FILE *f = fopen("/etc/machine-id", "r");
    if (!f) return -ENOENT;

    char buf[33] = {0};
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return -EIO;
    }
    fclose(f);

    /* Parse 32 hex chars into 16 bytes */
    for (int i = 0; i < 16; i++) {
        unsigned int b;
        if (sscanf(buf + 2 * i, "%2x", &b) != 1)
            return -EINVAL;
        ret->bytes[i] = (uint8_t)b;
    }
    return 0;
}

int sd_id128_get_boot(sd_id128_t *ret)
{
    return sd_id128_get_machine(ret);
}

int sd_id128_get_invocation(sd_id128_t *ret)
{
    return sd_id128_randomize(ret);
}

char *sd_id128_to_string(sd_id128_t id, char s[static 33])
{
    for (int i = 0; i < 16; i++)
        sprintf(s + 2 * i, "%02" PRIx8, id.bytes[i]);
    s[32] = '\0';
    return s;
}

int sd_id128_from_string(const char *s, sd_id128_t *ret)
{
    if (!s || strlen(s) < 32) return -EINVAL;
    for (int i = 0; i < 16; i++) {
        unsigned int b;
        if (sscanf(s + 2 * i, "%2x", &b) != 1)
            return -EINVAL;
        ret->bytes[i] = (uint8_t)b;
    }
    return 0;
}

/* ──────────────────────────────────────────────────────────────
 * sd_event – event loop (basic epoll wrapper)
 * ──────────────────────────────────────────────────────────── */

typedef int (*sd_event_io_handler_t)(struct sd_event_source *s, int fd,
                                      uint32_t revents, void *userdata);
typedef int (*sd_event_time_handler_t)(struct sd_event_source *s, uint64_t usec,
                                        void *userdata);
typedef int (*sd_event_signal_handler_t)(struct sd_event_source *s,
                                         const struct signalfd_siginfo *si,
                                         void *userdata);
typedef int (*sd_event_child_handler_t)(struct sd_event_source *s,
                                         const siginfo_t *si,
                                         void *userdata);
typedef int (*sd_event_handler_t)(struct sd_event_source *s, void *userdata);
typedef void (*sd_event_destroy_t)(void *userdata);

struct sd_event;

struct sd_event_source {
    struct sd_event *event;
    int fd;
    int events;
    sd_event_io_handler_t io_callback;
    sd_event_time_handler_t time_callback;
    sd_event_signal_handler_t signal_callback;
    sd_event_child_handler_t child_callback;
    sd_event_handler_t handler;
    void *userdata;
    uint64_t next_time_usec;
    int64_t priority;
    int registered;
    struct signalfd_siginfo siginfo;
};

struct sd_event {
    int epoll_fd;
    int exit_code;
    int running;
    int n_sources;
    struct sd_event_source **sources;
};

int sd_event_new(sd_event **ret)
{
    if (!ret) return -EINVAL;
    sd_event *e = calloc(1, sizeof(sd_event));
    if (!e) return -ENOMEM;

    e->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (e->epoll_fd < 0) {
        int saved = errno;
        free(e);
        return -saved;
    }

    e->exit_code = 0;
    e->running = 0;
    e->n_sources = 0;
    e->sources = NULL;
    *ret = e;
    return 0;
}

sd_event *sd_event_ref(sd_event *e)
{
    return e; /* no refcounting in this simple impl */
}

sd_event *sd_event_unref(sd_event *e)
{
    if (!e) return NULL;
    if (e->epoll_fd >= 0) close(e->epoll_fd);
    for (int i = 0; i < e->n_sources; i++) {
        if (e->sources[i]) free(e->sources[i]);
    }
    free(e->sources);
    free(e);
    return NULL;
}

int sd_event_add_io(sd_event *e, sd_event_source **ret,
                     int fd, uint32_t events,
                     sd_event_io_handler_t callback, void *userdata)
{
    if (!e || fd < 0 || !callback) return -EINVAL;

    sd_event_source *s = calloc(1, sizeof(sd_event_source));
    if (!s) return -ENOMEM;

    s->event = e;
    s->fd = fd;
    s->events = events;
    s->io_callback = callback;
    s->userdata = userdata;
    s->priority = 0;

    struct epoll_event ev = {
        .events = events,
        .data.ptr = s,
    };

    if (epoll_ctl(e->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        int saved = errno;
        free(s);
        return -saved;
    }

    /* Add to source list */
    e->sources = realloc(e->sources,
                         (size_t)(e->n_sources + 1) * sizeof(sd_event_source *));
    e->sources[e->n_sources++] = s;

    if (ret) *ret = s;
    return 0;
}

int sd_event_add_time(sd_event *e, sd_event_source **ret,
                       uint64_t usec, uint64_t accuracy,
                       sd_event_time_handler_t callback, void *userdata)
{
    if (!e || !callback) return -EINVAL;

    sd_event_source *s = calloc(1, sizeof(sd_event_source));
    if (!s) return -ENOMEM;

    s->event = e;
    s->time_callback = callback;
    s->userdata = userdata;
    s->next_time_usec = usec;
    s->fd = -1;

    e->sources = realloc(e->sources,
                         (size_t)(e->n_sources + 1) * sizeof(sd_event_source *));
    e->sources[e->n_sources++] = s;

    if (ret) *ret = s;
    return 0;
}

int sd_event_add_signal(sd_event *e, sd_event_source **ret,
                         int sig,
                         sd_event_signal_handler_t callback, void *userdata)
{
    return -ENOSYS;
}

int sd_event_add_child(sd_event *e, sd_event_source **ret,
                        pid_t pid, int options,
                        sd_event_child_handler_t callback, void *userdata)
{
    return -ENOSYS;
}

int sd_event_add_post(sd_event *e, sd_event_source **ret,
                       sd_event_handler_t callback, void *userdata)
{
    return -ENOSYS;
}

int sd_event_add_defer(sd_event *e, sd_event_source **ret,
                        sd_event_handler_t callback, void *userdata)
{
    return -ENOSYS;
}

int sd_event_add_exit(sd_event *e, sd_event_source **ret,
                       sd_event_handler_t callback, void *userdata)
{
    return -ENOSYS;
}

int sd_event_loop(sd_event *e)
{
    if (!e) return -EINVAL;

    e->running = 1;
    while (e->running) {
        struct epoll_event ev;
        int r = epoll_wait(e->epoll_fd, &ev, 1, -1);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }

        sd_event_source *s = ev.data.ptr;
        if (s && s->io_callback) {
            r = s->io_callback(s, s->fd, ev.events, s->userdata);
            if (r < 0) return r;
        }
    }
    return e->exit_code;
}

int sd_event_run(sd_event *e, uint64_t timeout_usec)
{
    if (!e) return -EINVAL;

    struct epoll_event ev;
    int r = epoll_wait(e->epoll_fd, &ev, 1,
                       timeout_usec == UINT64_MAX ? -1 :
                       (int)(timeout_usec / 1000));
    if (r < 0) {
        if (errno == EINTR) return 0;
        return -errno;
    }
    if (r == 0) return 0;

    sd_event_source *s = ev.data.ptr;
    if (s && s->io_callback) {
        r = s->io_callback(s, s->fd, ev.events, s->userdata);
        if (r < 0) return r;
    }
    return 1;
}

int sd_event_exit(sd_event *e, int code)
{
    if (!e) return -EINVAL;
    e->running = 0;
    e->exit_code = code;
    return 0;
}

int sd_event_get_state(sd_event *e)
{
    if (!e) return -EINVAL;
    return e->running ? SD_EVENT_ON : SD_EVENT_OFF;
}

int sd_event_get_fd(sd_event *e)
{
    if (!e) return -EINVAL;
    return e->epoll_fd;
}

int sd_event_get_tid(sd_event *e, pid_t *tid)
{
    return -ENOSYS;
}

int sd_event_get_exit_code(sd_event *e, int *code)
{
    if (!e || !code) return -EINVAL;
    *code = e->exit_code;
    return 0;
}

int sd_event_set_watchdog(sd_event *e, int b) { return 0; }

int sd_event_get_watchdog(sd_event *e) { return 0; }

int sd_event_now(sd_event *e, clockid_t clock, uint64_t *usec)
{
    (void)e;
    struct timespec ts;
    if (clock_gettime(clock, &ts) < 0) return -errno;
    *usec = (uint64_t)ts.tv_sec * UINT64_C(1000000) +
            (uint64_t)ts.tv_nsec / 1000;
    return 0;
}

int sd_event_source_set_enabled(sd_event_source *s, int enabled)
{
    if (!s) return -EINVAL;
    if (enabled == SD_EVENT_ON) {
        if (s->fd >= 0 && !s->registered) {
            struct epoll_event ev = {
                .events = s->events,
                .data.ptr = s,
            };
            if (epoll_ctl(s->event->epoll_fd, EPOLL_CTL_ADD, s->fd, &ev) < 0)
                return -errno;
            s->registered = 1;
        }
    } else if (enabled == SD_EVENT_OFF) {
        if (s->fd >= 0) {
            epoll_ctl(s->event->epoll_fd, EPOLL_CTL_DEL, s->fd, NULL);
            s->registered = 0;
        }
    }
    return 0;
}

int sd_event_source_get_enabled(sd_event_source *s, int *ret)
{
    if (!s || !ret) return -EINVAL;
    *ret = s->registered ? SD_EVENT_ON : SD_EVENT_OFF;
    return 0;
}

int sd_event_source_get_priority(sd_event_source *s, int64_t *ret)
{
    if (!s || !ret) return -EINVAL;
    *ret = s->priority;
    return 0;
}

int sd_event_source_set_priority(sd_event_source *s, int64_t priority)
{
    if (!s) return -EINVAL;
    s->priority = priority;
    return 0;
}

int sd_event_source_set_userdata(sd_event_source *s, void *userdata)
{
    if (!s) return -EINVAL;
    s->userdata = userdata;
    return 0;
}

void *sd_event_source_get_userdata(sd_event_source *s)
{
    return s ? s->userdata : NULL;
}

int sd_event_source_get_io_fd(sd_event_source *s)
{
    if (!s) return -EINVAL;
    return s->fd;
}

int sd_event_source_set_io_fd(sd_event_source *s, int fd)
{
    if (!s) return -EINVAL;
    if (s->registered) {
        epoll_ctl(s->event->epoll_fd, EPOLL_CTL_DEL, s->fd, NULL);
    }
    s->fd = fd;
    if (s->registered) {
        struct epoll_event ev = { .events = s->events, .data.ptr = s };
        epoll_ctl(s->event->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    }
    return 0;
}

int sd_event_source_get_io_events(sd_event_source *s, uint32_t *ret)
{
    if (!s || !ret) return -EINVAL;
    *ret = s->events;
    return 0;
}

int sd_event_source_set_io_events(sd_event_source *s, uint32_t events)
{
    if (!s) return -EINVAL;
    s->events = events;
    if (s->registered && s->fd >= 0) {
        struct epoll_event ev = { .events = events, .data.ptr = s };
        epoll_ctl(s->event->epoll_fd, EPOLL_CTL_MOD, s->fd, &ev);
    }
    return 0;
}

int sd_event_source_get_io_revents(sd_event_source *s, uint32_t *ret)
{
    if (!s || !ret) return -EINVAL;
    *ret = 0;
    return 0;
}

int sd_event_source_get_time(sd_event_source *s, uint64_t *ret)
{
    if (!s || !ret) return -EINVAL;
    *ret = s->next_time_usec;
    return 0;
}

int sd_event_source_set_time(sd_event_source *s, uint64_t usec)
{
    if (!s) return -EINVAL;
    s->next_time_usec = usec;
    return 0;
}

int sd_event_source_get_signal(sd_event_source *s, int *ret)
{
    return -ENOSYS;
}

/* ──────────────────────────────────────────────────────────────
 * Constructor – print diagnostic
 * ──────────────────────────────────────────────────────────── */

__attribute__((constructor))
static void libsystemd_shim_init(void)
{
    const char *v = getenv("SYSTEMD_SHIMD_DEBUG");
    if (v && v[0] == '1')
        fprintf(stderr, "systemd-shimd: libsystemd.so.0 loaded (LD_PRELOAD)\n");
}
