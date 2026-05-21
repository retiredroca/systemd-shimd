#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

/* sd_id128_t type — defined manually to avoid header linkage conflicts */
typedef union sd_id128 {
    uint8_t bytes[16];
    uint64_t qwords[2];
} sd_id128_t;

/* ── sd_notify ──────────────────────────────────────────────── */

extern "C" int sd_notify(int unset_environment, const char *state)
{
    (void)unset_environment;
    if (!state) return 0;

    if (getenv("SYSTEMD_SHIMD_DEBUG"))
        fprintf(stderr, "libsystemd-shim: sd_notify(%s)\n", state);

    return 0;
}

extern "C" int sd_notifyf(int unset_environment, const char *format, ...)
{
    (void)unset_environment;
    if (!format) return 0;

    char buf[4096];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    return sd_notify(0, buf);
}

/* ── sd_booted ──────────────────────────────────────────────── */

extern "C" int sd_booted(void)
{
    if (access("/run/dbus/pid", F_OK) == 0) return 1;
    return 1;
}

/* ── sd_listen_fds / sd_is_socket ───────────────────────────── */

extern "C" int sd_listen_fds(int unset_environment)
{
    const char *env = getenv("LISTEN_PID");
    if (!env) return 0;

    pid_t listen_pid = (pid_t)atol(env);
    if (listen_pid != getpid()) return 0;

    const char *fds_env = getenv("LISTEN_FDS");
    if (!fds_env) return 0;

    int n = (int)atol(fds_env);
    if (n <= 0) return 0;

    if (unset_environment) {
        unsetenv("LISTEN_PID");
        unsetenv("LISTEN_FDS");
    }

    return n;
}

extern "C" int sd_is_socket(int fd, int family, int type, int listening)
{
    struct stat st;
    if (fstat(fd, &st) < 0) return 0;
    if (!S_ISSOCK(st.st_mode)) return 0;

    int so_type = 0;
    socklen_t l = sizeof(so_type);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &so_type, &l) < 0)
        return 0;
    if (type != 0 && so_type != type) return 0;

    if (family != AF_UNSPEC) {
        struct sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        if (getsockname(fd, (struct sockaddr*)&addr, &addrlen) < 0)
            return 0;
        if (addr.ss_family != (sa_family_t)family) return 0;
    }

    if (listening >= 0) {
        int is_listening = 0;
        l = sizeof(is_listening);
        if (getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &is_listening, &l) < 0)
            return 0;
        if ((bool)is_listening != (bool)listening) return 0;
    }

    return 1;
}

/* ── sd_journal_print ───────────────────────────────────────── */

extern "C" int sd_journal_print(int priority, const char *format, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    openlog("sd-journal", LOG_PID, LOG_DAEMON);
    syslog(priority, "%s", buf);
    closelog();

    return (int)strlen(buf);
}

extern "C" int sd_journal_sendv(const struct iovec *iov, int n)
{
    for (int i = 0; i < n; i++) {
        const char *data = (const char*)iov[i].iov_base;
        size_t len = iov[i].iov_len;
        if (len > 8 && strncmp(data, "MESSAGE=", 8) == 0) {
            char buf[4096];
            size_t clen = len - 8;
            if (clen > sizeof(buf) - 1) clen = sizeof(buf) - 1;
            memcpy(buf, data + 8, clen);
            buf[clen] = '\0';
            openlog("sd-journal", LOG_PID, LOG_DAEMON);
            syslog(LOG_INFO, "%s", buf);
            closelog();
            break;
        }
    }
    return 0;
}

/* ── sd_id128 ───────────────────────────────────────────────── */

extern "C" int sd_id128_randomize(sd_id128_t *ret)
{
    if (!ret) return -1;
    FILE *f = fopen("/proc/sys/kernel/random/uuid", "r");
    if (!f) return -1;

    char uuid[37];
    if (!fgets(uuid, sizeof(uuid), f)) { fclose(f); return -1; }
    fclose(f);

    unsigned a, b, c, d, e, f_, g, h;
    sscanf(uuid, "%08x-%04x-%04x-%04x-%04x%04x%04x",
           &a, &b, &c, &d, &e, &f_, &g, &h);
    ret->bytes[0]  = (uint8_t)(a >> 24);
    ret->bytes[1]  = (uint8_t)(a >> 16);
    ret->bytes[2]  = (uint8_t)(a >> 8);
    ret->bytes[3]  = (uint8_t)a;
    ret->bytes[4]  = (uint8_t)(b >> 8);
    ret->bytes[5]  = (uint8_t)b;
    ret->bytes[6]  = (uint8_t)(c >> 8);
    ret->bytes[7]  = (uint8_t)c;
    ret->bytes[8]  = (uint8_t)(d >> 8);
    ret->bytes[9]  = (uint8_t)d;
    ret->bytes[10] = (uint8_t)(e >> 8);
    ret->bytes[11] = (uint8_t)e;
    ret->bytes[12] = (uint8_t)(f_ >> 8);
    ret->bytes[13] = (uint8_t)f_;
    ret->bytes[14] = (uint8_t)(g >> 8);
    ret->bytes[15] = (uint8_t)g;

    return 0;
}

extern "C" int sd_id128_get_machine(sd_id128_t *ret)
{
    if (!ret) return -1;

    FILE *f = fopen("/etc/machine-id", "r");
    if (!f) {
        f = fopen("/var/lib/dbus/machine-id", "r");
        if (!f) return -1;
    }

    char id[33] = {0};
    if (!fgets(id, sizeof(id), f)) { fclose(f); return -1; }
    fclose(f);

    for (int i = 0; i < 16; i++) {
        unsigned byte;
        sscanf(id + i * 2, "%02x", &byte);
        ret->bytes[i] = (uint8_t)byte;
    }

    return 0;
}
