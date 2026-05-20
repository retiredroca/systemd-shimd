#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>

typedef enum {
    INIT_UNKNOWN = 0,
    INIT_OPENRC,
    INIT_RUNIT,
    INIT_S6,
    INIT_SYSV,
} InitType;

#define MASK_DIR "/etc/systemd-shimd/masked"

static inline const char *init_type_name(InitType t)
{
    switch (t) {
    case INIT_OPENRC: return "openrc";
    case INIT_RUNIT:  return "runit";
    case INIT_S6:     return "s6";
    case INIT_SYSV:   return "sysvinit";
    default:          return "unknown";
    }
}

static inline InitType detect_init_system(void)
{
    static InitType cached = INIT_UNKNOWN;
    if (cached != INIT_UNKNOWN) return cached;

    FILE *f = fopen("/proc/1/comm", "r");
    char comm[64] = {0};
    if (f) {
        if (fgets(comm, sizeof(comm), f)) {
            size_t len = strlen(comm);
            if (len > 0 && comm[len - 1] == '\n')
                comm[len - 1] = '\0';
        }
        fclose(f);
    }

    if (strcmp(comm, "openrc-init") == 0) return cached = INIT_OPENRC;
    if (strcmp(comm, "runit") == 0)       return cached = INIT_RUNIT;
    if (strcmp(comm, "s6-svscan") == 0)   return cached = INIT_S6;

    if (access("/sbin/rc-service", X_OK) == 0 ||
        access("/usr/sbin/rc-service", X_OK) == 0 ||
        access("/usr/bin/rc-service", X_OK) == 0)
        return cached = INIT_OPENRC;

    if (access("/usr/bin/sv", X_OK) == 0 ||
        access("/bin/sv", X_OK) == 0 ||
        access("/sbin/sv", X_OK) == 0)
        return cached = INIT_RUNIT;

    if (access("/usr/bin/s6-svc", X_OK) == 0 ||
        access("/bin/s6-svc", X_OK) == 0)
        return cached = INIT_S6;

    return cached = INIT_SYSV;
}

static inline int run_cmd(const char *argv[])
{
    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    if (pid < 0) return -1;
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

static inline int run_cmd_capture(const char *argv[], char **output, size_t *output_len)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }

    close(pipefd[1]);
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    ssize_t n;
    while ((n = read(pipefd[0], buf + len, cap - len - 1)) > 0) {
        len += (size_t)n;
        if (len >= cap - 1) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    buf[len] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    *output = buf;
    if (output_len) *output_len = len;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

static inline void chomp(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
        s[--len] = '\0';
}

static inline char *strip_unit_suffix(const char *name)
{
    size_t len = strlen(name);
    if (len > 8 && strcmp(name + len - 8, ".service") == 0) {
        char *s = strdup(name);
        s[len - 8] = '\0';
        return s;
    }
    return strdup(name);
}

/* forward declarations for systemd unit auto-generation helpers */
static inline int init_script_exists(const char *name);
static inline int ensure_init_script(const char *name);
static inline int is_auto_generated(const char *path);

static inline int unit_start(const char *name)
{
    InitType t = detect_init_system();
    char *s = strip_unit_suffix(name);
    size_t mpl = strlen(MASK_DIR) + 1 + strlen(s) + 1;
    char *mp = malloc(mpl); snprintf(mp, mpl, "%s/%s", MASK_DIR, s);
    int masked = access(mp, F_OK) == 0;
    free(mp);
    if (masked) {
        fprintf(stderr, "Unit %s is masked.\n", s);
        free(s);
        return 1;
    }
    int rc;
    switch (t) {
    case INIT_OPENRC: {
        ensure_init_script(s);
        const char *a[] = {"rc-service", s, "start", NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_RUNIT: {
        const char *a[] = {"sv", "start", s, NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_S6: {
        const char *a[] = {"s6-svc", "-u", s, NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_SYSV: {
        char *p; size_t pl = strlen("/etc/init.d/") + strlen(s) + 1;
        p = malloc(pl); snprintf(p, pl, "/etc/init.d/%s", s);
        const char *a[] = {p, "start", NULL};
        rc = run_cmd(a); free(p);
        break;
    }
    default: rc = -1;
    }
    free(s);
    return rc;
}

static inline int unit_stop(const char *name)
{
    InitType t = detect_init_system();
    char *s = strip_unit_suffix(name);
    int rc;
    switch (t) {
    case INIT_OPENRC: {
        ensure_init_script(s);
        const char *a[] = {"rc-service", s, "stop", NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_RUNIT: {
        const char *a[] = {"sv", "stop", s, NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_S6: {
        const char *a[] = {"s6-svc", "-d", s, NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_SYSV: {
        char *p; size_t pl = strlen("/etc/init.d/") + strlen(s) + 1;
        p = malloc(pl); snprintf(p, pl, "/etc/init.d/%s", s);
        const char *a[] = {p, "stop", NULL};
        rc = run_cmd(a); free(p);
        break;
    }
    default: rc = -1;
    }
    free(s);
    return rc;
}

static inline int unit_restart(const char *name)
{
    InitType t = detect_init_system();
    char *s = strip_unit_suffix(name);
    int rc;
    switch (t) {
    case INIT_OPENRC: {
        ensure_init_script(s);
        const char *a[] = {"rc-service", s, "restart", NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_RUNIT: {
        const char *a[] = {"sv", "restart", s, NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_S6: {
        const char *a[] = {"s6-svc", "-r", s, NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_SYSV: {
        char *p; size_t pl = strlen("/etc/init.d/") + strlen(s) + 1;
        p = malloc(pl); snprintf(p, pl, "/etc/init.d/%s", s);
        const char *a[] = {p, "restart", NULL};
        rc = run_cmd(a); free(p);
        break;
    }
    default: rc = -1;
    }
    free(s);
    return rc;
}

static inline int unit_status(const char *name)
{
    InitType t = detect_init_system();
    char *s = strip_unit_suffix(name);
    int rc;
    switch (t) {
    case INIT_OPENRC: {
        ensure_init_script(s);
        const char *a[] = {"rc-service", s, "status", NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_RUNIT: {
        const char *a[] = {"sv", "status", s, NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_S6: {
        const char *a[] = {"s6-svc", "-a", s, NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_SYSV: {
        char *p; size_t pl = strlen("/etc/init.d/") + strlen(s) + 1;
        p = malloc(pl); snprintf(p, pl, "/etc/init.d/%s", s);
        const char *a[] = {p, "status", NULL};
        rc = run_cmd(a); free(p);
        break;
    }
    default: rc = -1;
    }
    free(s);
    return rc;
}

static inline int unit_enable(const char *name)
{
    InitType t = detect_init_system();
    char *s = strip_unit_suffix(name);
    int rc;
    switch (t) {
    case INIT_OPENRC: {
        ensure_init_script(s);
        const char *a[] = {"rc-update", "add", s, NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_RUNIT: {
        size_t sl = strlen("/etc/sv/") + strlen(s) + 1;
        char *src = malloc(sl); snprintf(src, sl, "/etc/sv/%s", s);
        size_t dl = strlen("/etc/runit/runsvdir/default/") + strlen(s) + 1;
        char *dst = malloc(dl); snprintf(dst, dl, "/etc/runit/runsvdir/default/%s", s);
        const char *a[] = {"ln", "-sf", src, dst, NULL};
        rc = run_cmd(a); free(src); free(dst);
        break;
    }
    case INIT_S6: {
        const char *a[] = {"s6-rc", "-u", "change", s, NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_SYSV: {
        const char *a[] = {"update-rc.d", s, "defaults", NULL};
        rc = run_cmd(a);
        break;
    }
    default: rc = -1;
    }
    free(s);
    return rc;
}

static inline int unit_disable(const char *name)
{
    InitType t = detect_init_system();
    char *s = strip_unit_suffix(name);
    int rc;
    switch (t) {
    case INIT_OPENRC: {
        const char *a[] = {"rc-update", "delete", s, NULL};
        rc = run_cmd(a);
        if (rc == 0) {
            size_t pl = strlen("/etc/init.d/") + strlen(s) + 1;
            char *p = malloc(pl); snprintf(p, pl, "/etc/init.d/%s", s);
            if (is_auto_generated(p)) unlink(p);
            free(p);
        }
        break;
    }
    case INIT_RUNIT: {
        size_t dl = strlen("/etc/runit/runsvdir/default/") + strlen(s) + 1;
        char *dst = malloc(dl); snprintf(dst, dl, "/etc/runit/runsvdir/default/%s", s);
        const char *a[] = {"rm", "-f", dst, NULL};
        rc = run_cmd(a); free(dst);
        break;
    }
    case INIT_S6: {
        const char *a[] = {"s6-rc", "-d", "change", s, NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_SYSV: {
        const char *a[] = {"update-rc.d", "-f", s, "remove", NULL};
        rc = run_cmd(a);
        break;
    }
    default: rc = -1;
    }
    free(s);
    return rc;
}

static inline int unit_is_active(const char *name)
{
    return unit_status(name) == 0 ? 0 : 1;
}

static inline int unit_is_enabled(const char *name)
{
    InitType t = detect_init_system();
    char *s = strip_unit_suffix(name);
    int rc;
    switch (t) {
    case INIT_OPENRC: {
        const char *a[] = {"rc-update", "show", NULL};
        char *out = NULL;
        rc = run_cmd_capture(a, &out, NULL);
        if (rc != 0 || !out) { free(out); free(s); return 1; }
        int found = 0;
        char *line = out;
        while (line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (strstr(line, s)) { found = 1; break; }
            if (nl) { *nl = '\n'; line = nl + 1; } else break;
        }
        free(out);
        rc = found ? 0 : 1;
        break;
    }
    case INIT_RUNIT: {
        size_t dl = strlen("/etc/runit/runsvdir/default/") + strlen(s) + 1;
        char *p = malloc(dl); snprintf(p, dl, "/etc/runit/runsvdir/default/%s", s);
        rc = access(p, F_OK) == 0 ? 0 : 1;
        free(p);
        break;
    }
    case INIT_S6: {
        size_t dl = strlen("/run/s6-rc/compiled/") + strlen(s) + 1;
        char *p = malloc(dl); snprintf(p, dl, "/run/s6-rc/compiled/%s", s);
        rc = access(p, F_OK) == 0 ? 0 : 1;
        free(p);
        break;
    }
    case INIT_SYSV: {
        size_t dl = strlen("/etc/init.d/") + strlen(s) + 1;
        char *p = malloc(dl); snprintf(p, dl, "/etc/init.d/%s", s);
        rc = access(p, X_OK) == 0 ? 0 : 1;
        free(p);
        break;
    }
    default: rc = 1;
    }
    free(s);
    return rc;
}

static inline char **unit_list(void)
{
    InitType t = detect_init_system();
    switch (t) {
    case INIT_OPENRC: {
        const char *a[] = {"rc-service", "-l", NULL};
        char *out = NULL;
        if (run_cmd_capture(a, &out, NULL) != 0 || !out) return NULL;
        size_t count = 0;
        for (char *p = out; *p; p++) if (*p == '\n') count++;
        if (count == 0 && out[0] == '\0') { free(out); return NULL; }
        char **list = calloc(count + 2, sizeof(char *));
        size_t idx = 0;
        char *line = out;
        while (line && *line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            chomp(line);
            if (line[0]) list[idx++] = strdup(line);
            if (nl) { *nl = '\n'; line = nl + 1; } else break;
        }
        list[idx] = NULL;
        free(out);
        return list;
    }
    case INIT_SYSV: {
        DIR *d = opendir("/etc/init.d");
        if (!d) return NULL;
        struct dirent *e;
        size_t cap = 64, n = 0;
        char **list = calloc(cap, sizeof(char *));
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            size_t pl = strlen("/etc/init.d/") + strlen(e->d_name) + 1;
            char *p = malloc(pl); snprintf(p, pl, "/etc/init.d/%s", e->d_name);
            if (access(p, X_OK) == 0) {
                if (n + 1 >= cap) { cap *= 2; list = realloc(list, cap * sizeof(char *)); }
                list[n++] = strdup(e->d_name);
            }
            free(p);
        }
        list[n] = NULL;
        closedir(d);
        return list;
    }
    default:
        return NULL;
    }
}

static inline int daemon_reload(void)
{
    return 0;
}

static inline int system_poweroff(void)
{
    InitType t = detect_init_system();
    switch (t) {
    case INIT_OPENRC: {
        const char *a[] = {"openrc-shutdown", "-p", "now", NULL};
        return run_cmd(a);
    }
    case INIT_RUNIT: {
        const char *a[] = {"shutdown", "-h", "now", NULL};
        return run_cmd(a);
    }
    case INIT_S6: {
        const char *a[] = {"s6-svscanctl", "-p", "/run/s6-rc/servicedirs", NULL};
        return run_cmd(a);
    }
    case INIT_SYSV: {
        const char *a[] = {"shutdown", "-h", "now", NULL};
        return run_cmd(a);
    }
    default: return -1;
    }
}

static inline int system_reboot(void)
{
    InitType t = detect_init_system();
    switch (t) {
    case INIT_OPENRC: {
        const char *a[] = {"openrc-shutdown", "-r", "now", NULL};
        return run_cmd(a);
    }
    case INIT_RUNIT: {
        const char *a[] = {"shutdown", "-r", "now", NULL};
        return run_cmd(a);
    }
    case INIT_S6: {
        const char *a[] = {"s6-svscanctl", "-r", "/run/s6-rc/servicedirs", NULL};
        return run_cmd(a);
    }
    case INIT_SYSV: {
        const char *a[] = {"shutdown", "-r", "now", NULL};
        return run_cmd(a);
    }
    default: return -1;
    }
}

static inline int system_halt(void)
{
    InitType t = detect_init_system();
    switch (t) {
    case INIT_OPENRC: {
        const char *a[] = {"openrc-shutdown", "-H", "now", NULL};
        return run_cmd(a);
    }
    case INIT_RUNIT: {
        const char *a[] = {"halt", NULL};
        return run_cmd(a);
    }
    case INIT_S6: {
        const char *a[] = {"s6-svscanctl", "-h", "/run/s6-rc/servicedirs", NULL};
        return run_cmd(a);
    }
    case INIT_SYSV: {
        const char *a[] = {"halt", NULL};
        return run_cmd(a);
    }
    default: return -1;
    }
}

/* ── Systemd unit file helpers ─────────────────────────────── */

static inline char *find_unit_file(const char *name)
{
    static const char *dirs[] = {
        "/etc/systemd/system",
        "/run/systemd/system",
        "/usr/lib/systemd/system",
        NULL
    };
    char *s = strip_unit_suffix(name);
    for (int i = 0; dirs[i]; i++) {
        size_t pl = strlen(dirs[i]) + strlen(s) + 17;
        char *p = malloc(pl);
        snprintf(p, pl, "%s/%s.service", dirs[i], s);
        if (access(p, F_OK) == 0) { free(s); return p; }
        free(p);
    }
    free(s);
    return NULL;
}

static inline char *trim_ws(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
    *end = '\0';
    return s;
}

static inline char *parse_value(const char *line)
{
    const char *eq = strchr(line, '=');
    if (!eq) return NULL;
    char *v = strdup(eq + 1);
    v = trim_ws(v);
    char *c = v;
    int inq = 0;
    while (*c) {
        if (*c == '"') inq = !inq;
        if (!inq && (*c == '#' || *c == ';')) { *c = '\0'; break; }
        c++;
    }
    v = trim_ws(v);
    return v;
}

static inline int generate_init_script(const char *name, const char *unit_path)
{
    FILE *f = fopen(unit_path, "r");
    if (!f) return 1;

    char *desc = NULL, *exec_start = NULL, *exec_stop = NULL, *exec_reload = NULL;
    char *type = NULL, *pidfile = NULL, *user = NULL, *group = NULL, *wdir = NULL;
    char *restart = NULL, *env = NULL;
    int in_unit = 0, in_service = 0;
    char buf[4096], cont[4096] = {0};

    while (fgets(buf, sizeof(buf), f)) {
        chomp(buf);
        char *line = trim_ws(buf);
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;

        if (line[0] == '[') {
            in_unit = (strcmp(line, "[Unit]") == 0);
            in_service = (strcmp(line, "[Service]") == 0);
            continue;
        }

        size_t llen = strlen(line);
        if (llen > 0 && line[llen - 1] == '\\') {
            line[llen - 1] = '\0';
            strcat(cont, line);
            strcat(cont, "\n");
            continue;
        }
        if (cont[0]) {
            strcat(cont, line);
            line = cont;
        }

        if (in_unit) {
            if (strncmp(line, "Description=", 12) == 0) { free(desc); desc = parse_value(line); }
        } else if (in_service) {
            if (strncmp(line, "ExecStart=", 10) == 0) { free(exec_start); exec_start = parse_value(line); }
            else if (strncmp(line, "ExecStop=", 9) == 0) { free(exec_stop); exec_stop = parse_value(line); }
            else if (strncmp(line, "ExecReload=", 11) == 0) { free(exec_reload); exec_reload = parse_value(line); }
            else if (strncmp(line, "Type=", 5) == 0) { free(type); type = parse_value(line); }
            else if (strncmp(line, "PIDFile=", 8) == 0) { free(pidfile); pidfile = parse_value(line); }
            else if (strncmp(line, "User=", 5) == 0) { free(user); user = parse_value(line); }
            else if (strncmp(line, "Group=", 6) == 0) { free(group); group = parse_value(line); }
            else if (strncmp(line, "WorkingDirectory=", 17) == 0) { free(wdir); wdir = parse_value(line); }
            else if (strncmp(line, "Restart=", 8) == 0) { free(restart); restart = parse_value(line); }
            else if (strncmp(line, "Environment=", 12) == 0) { free(env); env = parse_value(line); }
        }
        memset(cont, 0, sizeof(cont));
    }
    fclose(f);

    if (!exec_start || !exec_start[0]) {
        free(desc); free(exec_start); free(exec_stop); free(exec_reload); free(type);
        free(pidfile); free(user); free(group); free(wdir); free(restart); free(env);
        return 1;
    }

    while (*exec_start == '@' || *exec_start == '!' || *exec_start == '-')
        memmove(exec_start, exec_start + 1, strlen(exec_start));

    char *cmd = exec_start;
    char *args = strchr(exec_start, ' ');
    if (args) { *args = '\0'; args = trim_ws(args + 1); }
    if (!type) type = strdup("simple");

    size_t pl = strlen("/etc/init.d/") + strlen(name) + 1;
    char *path = malloc(pl); snprintf(path, pl, "/etc/init.d/%s", name);

    int rc = 0;
    FILE *out = fopen(path, "w");
    if (!out) { free(path); rc = 1; goto cleanup; }

    fprintf(out, "#!/sbin/openrc-run\n");
    fprintf(out, "# Auto-generated by systemd-shimd from %s\n", unit_path);
    if (desc) fprintf(out, "description=\"%s\"\n", desc);

    if (strcmp(type, "oneshot") == 0) {
        fprintf(out, "command=\"%s\"\n", cmd);
        if (args) fprintf(out, "command_args=\"%s\"\n", args);
    } else if (strcmp(type, "forking") == 0) {
        fprintf(out, "command=\"%s\"\n", cmd);
        if (args) fprintf(out, "command_args=\"%s\"\n", args);
        fprintf(out, "command_background=true\n");
        fprintf(out, "pidfile=\"%s\"\n", pidfile ? pidfile : "/run/${RC_SVCNAME}.pid");
    } else {
        fprintf(out, "command=\"%s\"\n", cmd);
        if (args) fprintf(out, "command_args=\"%s\"\n", args);
        fprintf(out, "supervisor=\"supervise-daemon\"\n");
    }

    if (user && group)
        fprintf(out, "command_user=\"%s:%s\"\n", user, group);
    else if (user)
        fprintf(out, "command_user=\"%s\"\n", user);
    else if (group)
        fprintf(out, "command_user=\"root:%s\"\n", group);

    if (wdir) fprintf(out, "directory=\"%s\"\n", wdir);

    if (env) {
        fprintf(out, "\n");
        char *e = env;
        while (e && *e) {
            while (*e == ' ') e++;
            if (!*e) break;
            char *sp = strchr(e, ' ');
            if (sp) *sp = '\0';
            fprintf(out, "export %s\n", e);
            if (sp) { *sp = ' '; e = sp + 1; } else break;
        }
    }

    fprintf(out, "\ndepend() {\n    use logger\n}\n");

    if (exec_stop && strcmp(type, "oneshot") != 0) {
        fprintf(out, "\nstop() {\n    ebegin \"Stopping ${SVCNAME}\"\n");
        fprintf(out, "    start-stop-daemon --stop --exec \"%s\"", cmd);
        if (pidfile) fprintf(out, " --pidfile \"%s\"", pidfile);
        fprintf(out, "\n    eend $?\n}\n");
    }

    if (exec_reload) {
        char *rcmd = exec_reload;
        char *rargs = strchr(exec_reload, ' ');
        if (rargs) { *rargs = '\0'; rargs = trim_ws(rargs + 1); }
        fprintf(out, "\nreload() {\n    ebegin \"Reloading ${SVCNAME}\"\n");
        fprintf(out, "    start-stop-daemon --signal HUP --exec \"%s\"", rcmd);
        if (pidfile) fprintf(out, " --pidfile \"%s\"", pidfile);
        fprintf(out, "\n    eend $?\n}\n");
        if (rargs) *rargs = ' ';
    }

    fclose(out);
    chmod(path, 0755);

    free(path);
cleanup:
    free(desc); free(exec_start); free(exec_stop); free(exec_reload);
    free(type); free(pidfile); free(user); free(group); free(wdir); free(restart); free(env);
    return rc;
}

static inline int init_script_exists(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t pl = strlen("/etc/init.d/") + strlen(s) + 1;
    char *p = malloc(pl); snprintf(p, pl, "/etc/init.d/%s", s);
    int rc = access(p, F_OK) == 0 ? 0 : 1;
    free(p); free(s);
    return rc;
}

static inline int ensure_init_script(const char *name)
{
    if (init_script_exists(name) == 0) return 0;
    if (detect_init_system() != INIT_OPENRC) return 1;
    char *s = strip_unit_suffix(name);
    char *unit = find_unit_file(s);
    if (!unit) { free(s); return 1; }
    int rc = generate_init_script(s, unit);
    free(unit); free(s);
    return rc;
}

static inline int is_auto_generated(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char buf[128];
    int rc = 0;
    if (fgets(buf, sizeof(buf), f) && strstr(buf, "Auto-generated by systemd-shimd"))
        rc = 1;
    fclose(f);
    return rc;
}

/* ── Compatibility operations (package post-install) ─────────── */

static inline int unit_mask(const char *name)
{
    char *s = strip_unit_suffix(name);
    mkdir("/etc/systemd-shimd", 0755);
    mkdir(MASK_DIR, 0755);
    size_t pl = strlen(MASK_DIR) + 1 + strlen(s) + 1;
    char *p = malloc(pl); snprintf(p, pl, "%s/%s", MASK_DIR, s);
    FILE *f = fopen(p, "w");
    int rc = f ? 0 : 1;
    if (f) fclose(f);
    free(p); free(s);
    return rc;
}

static inline int unit_unmask(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t pl = strlen(MASK_DIR) + 1 + strlen(s) + 1;
    char *p = malloc(pl); snprintf(p, pl, "%s/%s", MASK_DIR, s);
    int rc = unlink(p) == 0 ? 0 : 1;
    free(p); free(s);
    return rc;
}

static inline int unit_try_restart(const char *name)
{
    if (unit_is_active(name) == 0) return unit_restart(name);
    return 0;
}

static inline int unit_reload(const char *name)
{
    InitType t = detect_init_system();
    char *s = strip_unit_suffix(name);
    int rc;
    switch (t) {
    case INIT_OPENRC: {
        ensure_init_script(s);
        const char *a[] = {"rc-service", s, "reload", NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_RUNIT: {
        const char *a[] = {"sv", "reload", s, NULL};
        rc = run_cmd(a);
        break;
    }
    case INIT_S6:
    case INIT_SYSV: {
        /* reload not available; fall through to restart */
        rc = -1;
        break;
    }
    default: rc = -1;
    }
    free(s);
    if (rc != 0) rc = unit_restart(name);
    return rc;
}

static inline int unit_reenable(const char *name)
{
    int rc = unit_disable(name);
    rc |= unit_enable(name);
    return rc;
}

static inline int unit_is_failed(const char *name)
{
    (void)name;
    return 1;
}

static inline int unit_kill(const char *name, const char *signal)
{
    char *s = strip_unit_suffix(name);
    char cmd[768];
    snprintf(cmd, sizeof(cmd),
        "p=$(cat /var/run/%s.pid 2>/dev/null; "
        "cat /run/%s.pid 2>/dev/null); "
        "[ -n \"$p\" ] && kill -s %s $p 2>/dev/null || true",
        s, s, signal);
    const char *a[] = {"sh", "-c", cmd, NULL};
    int rc = run_cmd(a);
    free(s);
    return rc;
}

static inline int unit_daemon_reexec(void)
{
    return 0;
}

static inline int unit_reset_failed(const char *name)
{
    (void)name;
    return 0;
}

static inline int unit_link(const char *path)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char *s = strip_unit_suffix(base);
    size_t pl = strlen("/etc/init.d/") + strlen(s) + 1;
    char *p = malloc(pl); snprintf(p, pl, "/etc/init.d/%s", s);
    int rc = 0;
    if (symlink(path, p) != 0) { perror("symlink"); rc = 1; }
    free(p); free(s);
    return rc;
}

static inline int unit_preset(const char *name)
{
    return unit_enable(name);
}

static inline int unit_reload_or_restart(const char *name)
{
    int rc = unit_reload(name);
    if (rc != 0) rc = unit_restart(name);
    return rc;
}

static inline int unit_try_reload_or_restart(const char *name)
{
    if (unit_is_active(name) != 0) return 0;
    return unit_reload_or_restart(name);
}

static inline int unit_show(const char *name)
{
    InitType t = detect_init_system();
    if (!name) {
        printf("Manager: systemd-shimd (pid %d)\n", getpid());
        printf("Init: %s\n", init_type_name(t));
        return 0;
    }
    char *s = strip_unit_suffix(name);
    printf("Unit: %s\n", s);
    printf("Init: %s\n", init_type_name(t));
    printf("Has init script: %s\n", init_script_exists(s) == 0 ? "yes" : "no");
    char *u = find_unit_file(s);
    printf("Has systemd unit: %s\n", u ? "yes" : "no");
    if (u) printf("Systemd unit path: %s\n", u);
    free(u);
    free(s);
    return 0;
}

static inline int unit_cat(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t pl = strlen("/etc/init.d/") + strlen(s) + 1;
    char *p = malloc(pl); snprintf(p, pl, "/etc/init.d/%s", s);
    FILE *f = fopen(p, "r");
    if (!f) {
        char *u = find_unit_file(s);
        if (u) {
            free(p);
            p = u;
            f = fopen(p, "r");
        }
        if (!f) { free(p); free(s); return 1; }
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        fwrite(buf, 1, n, stdout);
    fclose(f);
    free(p); free(s);
    return 0;
}

static inline int unit_list_files(void)
{
    char **units = unit_list();
    if (!units) return 1;
    for (int i = 0; units[i]; i++) {
        printf("%s\n", units[i]);
        free(units[i]);
    }
    free(units);
    return 0;
}

