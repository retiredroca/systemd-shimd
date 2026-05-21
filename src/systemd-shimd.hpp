#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace shimd {

/* ── Constants ─────────────────────────────────────────────── */

constexpr auto MASK_DIR = "/etc/systemd-shimd/masked";

/* ── Init type detection ────────────────────────────────────── */

enum class Init { Unknown, OpenRC, Runit, S6, SysV };

inline Init detect() noexcept
{
    static Init cached = Init::Unknown;
    if (cached != Init::Unknown) return cached;

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

    if (strcmp(comm, "openrc-init") == 0) return cached = Init::OpenRC;
    if (strcmp(comm, "runit") == 0)       return cached = Init::Runit;
    if (strcmp(comm, "s6-svscan") == 0)   return cached = Init::S6;

    if (access("/sbin/rc-service", X_OK) == 0 ||
        access("/usr/sbin/rc-service", X_OK) == 0 ||
        access("/usr/bin/rc-service", X_OK) == 0)
        return cached = Init::OpenRC;

    if (access("/usr/bin/sv", X_OK) == 0 ||
        access("/bin/sv", X_OK) == 0 ||
        access("/sbin/sv", X_OK) == 0)
        return cached = Init::Runit;

    if (access("/usr/bin/s6-svc", X_OK) == 0 ||
        access("/bin/s6-svc", X_OK) == 0)
        return cached = Init::S6;

    return cached = Init::SysV;
}

inline const char *init_name(Init i) noexcept
{
    switch (i) {
    case Init::OpenRC: return "openrc";
    case Init::Runit:  return "runit";
    case Init::S6:     return "s6";
    case Init::SysV:   return "sysvinit";
    default:           return "unknown";
    }
}

/* ── Process helpers ───────────────────────────────────────── */

inline int run_cmd(std::initializer_list<const char*> args)
{
    std::vector<const char*> argv(args);
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    }
    if (pid < 0) return -1;

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

inline std::optional<std::string> run_cmd_capture(std::initializer_list<const char*> args)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) return std::nullopt;

    std::vector<const char*> argv(args);
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    }
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return std::nullopt; }

    close(pipefd[1]);
    std::string result;
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
        result.append(buf, n);
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return result;
    return std::nullopt;
}

/* ── String helpers ────────────────────────────────────────── */

inline std::string strip_unit_suffix(std::string_view name)
{
    static constexpr std::array<const char*, 10> suffixes{
        ".service", ".socket", ".timer", ".target", ".path",
        ".mount",   ".device", ".slice", ".scope",  ".swap"
    };

    std::string s(name);
    for (auto suf : suffixes) {
        auto sl = strlen(suf);
        if (s.size() > sl && s.substr(s.size() - sl) == suf) {
            s.resize(s.size() - sl);
            break;
        }
    }

    auto at = s.find('@');
    if (at != std::string::npos)
        s.resize(at);

    return s;
}

/* ── Systemd unit file helpers ──────────────────────────────── */

inline std::optional<std::string> find_unit_file(std::string_view name)
{
    static constexpr std::array<const char*, 3> dirs{
        "/etc/systemd/system",
        "/run/systemd/system",
        "/usr/lib/systemd/system"
    };

    auto s = strip_unit_suffix(name);
    for (auto d : dirs) {
        auto p = std::string(d) + "/" + s + ".service";
        if (access(p.c_str(), F_OK) == 0)
            return p;
    }
    return std::nullopt;
}

inline std::string init_script_path(const std::string& name, Init init)
{
    auto s = strip_unit_suffix(name);
    switch (init) {
    case Init::OpenRC:
    case Init::SysV:
        return "/etc/init.d/" + s;
    case Init::Runit:
        return "/etc/sv/" + s + "/run";
    case Init::S6:
        return "/etc/s6-rc/" + s + "/run";
    default:
        return {};
    }
}

inline bool init_script_exists(const std::string& name, Init init)
{
    auto p = init_script_path(name, init);
    return !p.empty() && access(p.c_str(), F_OK) == 0;
}

inline bool is_auto_generated(const std::string& path)
{
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    std::getline(f, line);
    return line.find("Auto-generated by systemd-shimd") != std::string::npos;
}

inline bool generate_init_script(const std::string& name,
                                  const std::string& unit_path, Init init)
{
    std::ifstream f(unit_path);
    if (!f) return false;

    std::string desc, exec_start, exec_stop, exec_reload, type, pidfile;
    std::string user, group, wdir, env;
    bool in_unit = false, in_service = false;
    std::string line, cont;

    while (std::getline(f, line)) {
        /* handle continuation */
        if (!cont.empty()) {
            cont += "\n" + line;
            if (!line.empty() && line.back() == '\\') {
                cont.pop_back();
                continue;
            }
            line = cont;
            cont.clear();
        }
        if (!line.empty() && line.back() == '\\') {
            cont = line.substr(0, line.size() - 1);
            continue;
        }

        /* trim */
        auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#' || line[start] == ';')
            continue;
        if (start > 0) line = line.substr(start);
        auto end = line.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) line = line.substr(0, end + 1);

        if (line[0] == '[') {
            in_unit   = (line == "[Unit]");
            in_service = (line == "[Service]");
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto key = line.substr(0, eq);
        auto val = line.substr(eq + 1);

        /* trim val */
        auto vs = val.find_first_not_of(" \t");
        auto ve = val.find_last_not_of(" \t\r\n");
        if (vs != std::string::npos) val = val.substr(vs, ve - vs + 1);
        else val.clear();

        /* strip inline comment */
        if (!val.empty()) {
            bool inq = false;
            for (size_t i = 0; i < val.size(); i++) {
                if (val[i] == '"') inq = !inq;
                if (!inq && (val[i] == '#' || val[i] == ';')) {
                    val = val.substr(0, i);
                    break;
                }
            }
            vs = val.find_first_not_of(" \t");
            if (vs != std::string::npos) val = val.substr(vs);
            else val.clear();
        }

        if (in_unit) {
            if (key == "Description") desc = val;
        } else if (in_service) {
            if (key == "ExecStart") exec_start = val;
            else if (key == "ExecStop") exec_stop = val;
            else if (key == "ExecReload") exec_reload = val;
            else if (key == "Type") type = val;
            else if (key == "PIDFile") pidfile = val;
            else if (key == "User") user = val;
            else if (key == "Group") group = val;
            else if (key == "WorkingDirectory") wdir = val;
            else if (key == "Environment") env = val;
        }
    }

    if (exec_start.empty()) return false;

    /* strip leading @, !, -, from ExecStart */
    while (!exec_start.empty() && (exec_start[0] == '@' || exec_start[0] == '!' || exec_start[0] == '-'))
        exec_start = exec_start.substr(1);

    if (type.empty()) type = "simple";

    /* split command and args */
    auto sp = exec_start.find(' ');
    std::string cmd = exec_start.substr(0, sp);
    std::string cmd_args = (sp != std::string::npos) ? exec_start.substr(sp + 1) : "";

    auto gen_path = init_script_path(name, init);
    auto gen_dir = std::filesystem::path(gen_path).parent_path();
    std::filesystem::create_directories(gen_dir);

    std::ofstream out(gen_path);
    if (!out) return false;

    switch (init) {
    case Init::OpenRC:
    case Init::SysV: {
        out << "#!/sbin/openrc-run\n";
        out << "# Auto-generated by systemd-shimd from " << unit_path << "\n";
        if (!desc.empty()) out << "description=\"" << desc << "\"\n";

        if (type == "oneshot") {
            out << "command=\"" << cmd << "\"\n";
            if (!cmd_args.empty()) out << "command_args=\"" << cmd_args << "\"\n";
        } else if (type == "forking") {
            out << "command=\"" << cmd << "\"\n";
            if (!cmd_args.empty()) out << "command_args=\"" << cmd_args << "\"\n";
            out << "command_background=true\n";
            out << "pidfile=\"" << (pidfile.empty() ? "/run/${RC_SVCNAME}.pid" : pidfile) << "\"\n";
        } else {
            out << "command=\"" << cmd << "\"\n";
            if (!cmd_args.empty()) out << "command_args=\"" << cmd_args << "\"\n";
            out << "supervisor=\"supervise-daemon\"\n";
        }

        if (!user.empty() && !group.empty())
            out << "command_user=\"" << user << ":" << group << "\"\n";
        else if (!user.empty())
            out << "command_user=\"" << user << "\"\n";
        else if (!group.empty())
            out << "command_user=\"root:" << group << "\"\n";

        if (!wdir.empty()) out << "directory=\"" << wdir << "\"\n";

        if (!env.empty()) {
            out << "\n";
            std::istringstream es(env);
            std::string ev;
            while (es >> ev)
                out << "export " << ev << "\n";
        }

        out << "\ndepend() {\n    use logger\n}\n";

        if (!exec_stop.empty() && type != "oneshot") {
            out << "\nstop() {\n    ebegin \"Stopping ${SVCNAME}\"\n";
            out << "    start-stop-daemon --stop --exec \"" << cmd << "\"";
            if (!pidfile.empty()) out << " --pidfile \"" << pidfile << "\"";
            out << "\n    eend $?\n}\n";
        }

        if (!exec_reload.empty()) {
            auto rp = exec_reload.find(' ');
            auto rcmd = exec_reload.substr(0, rp);
            out << "\nreload() {\n    ebegin \"Reloading ${SVCNAME}\"\n";
            out << "    start-stop-daemon --signal HUP --exec \"" << rcmd << "\"";
            if (!pidfile.empty()) out << " --pidfile \"" << pidfile << "\"";
            out << "\n    eend $?\n}\n";
        }
        break;
    }
    case Init::Runit: {
        out << "#!/bin/sh -e\n";
        out << "# Auto-generated by systemd-shimd from " << unit_path << "\n";
        if (!cmd_args.empty())
            out << "exec " << cmd << " " << cmd_args << " 2>&1\n";
        else
            out << "exec " << cmd << " 2>&1\n";
        break;
    }
    case Init::S6: {
        out << "#!/bin/sh -e\n";
        out << "# Auto-generated by systemd-shimd from " << unit_path << "\n";
        if (!cmd_args.empty())
            out << "exec " << cmd << " " << cmd_args << "\n";
        else
            out << "exec " << cmd << "\n";
        break;
    }
    default:
        return false;
    }

    out.close();
    chmod(gen_path.c_str(), 0755);

    /* for runit, also create a finish file */
    if (init == Init::Runit) {
        auto finish_path = std::filesystem::path(gen_path).parent_path() / "finish";
        std::ofstream finish(finish_path);
        finish << "#!/bin/sh\nexit 0\n";
        finish.close();
        chmod(finish_path.c_str(), 0755);
    }

    return true;
}

inline bool ensure_init_script(const std::string& name)
{
    auto init = detect();
    if (init_script_exists(name, init)) return true;
    auto unit = find_unit_file(name);
    if (!unit) return false;
    return generate_init_script(name, *unit, init);
}

/* ── Backend template — per-init dispatch ───────────────────── */

template<Init I>
struct Backend;

/* ── OpenRC ─────────────────────────────────────────────────── */

template<>
struct Backend<Init::OpenRC>
{
    static int start(const std::string& name) {
        ensure_init_script(name);
        return run_cmd({"rc-service", name.c_str(), "start"});
    }

    static int stop(const std::string& name) {
        ensure_init_script(name);
        return run_cmd({"rc-service", name.c_str(), "stop"});
    }

    static int restart(const std::string& name) {
        ensure_init_script(name);
        return run_cmd({"rc-service", name.c_str(), "restart"});
    }

    static int status(const std::string& name) {
        ensure_init_script(name);
        return run_cmd({"rc-service", name.c_str(), "status"});
    }

    static int enable(const std::string& name) {
        ensure_init_script(name);
        return run_cmd({"rc-update", "add", name.c_str()});
    }

    static int disable(const std::string& name) {
        int rc = run_cmd({"rc-update", "delete", name.c_str()});
        if (rc == 0) {
            auto p = "/etc/init.d/" + name;
            if (is_auto_generated(p)) unlink(p.c_str());
        }
        return rc;
    }

    static int reload(const std::string& name) {
        ensure_init_script(name);
        return run_cmd({"rc-service", name.c_str(), "reload"});
    }

    static bool is_enabled(const std::string& name) {
        auto out = run_cmd_capture({"rc-update", "show"});
        return out && out->find(name) != std::string::npos;
    }

    static std::vector<std::string> list() {
        auto out = run_cmd_capture({"rc-service", "-l"});
        if (!out) return {};
        std::vector<std::string> result;
        std::istringstream ss(*out);
        std::string line;
        while (std::getline(ss, line))
            if (!line.empty()) result.push_back(line);
        return result;
    }
};

/* ── Runit ──────────────────────────────────────────────────── */

template<>
struct Backend<Init::Runit>
{
    static int start(const std::string& name) {
        ensure_init_script(name);
        return run_cmd({"sv", "start", name.c_str()});
    }

    static int stop(const std::string& name) {
        return run_cmd({"sv", "stop", name.c_str()});
    }

    static int restart(const std::string& name) {
        return run_cmd({"sv", "restart", name.c_str()});
    }

    static int status(const std::string& name) {
        return run_cmd({"sv", "status", name.c_str()});
    }

    static int enable(const std::string& name) {
        ensure_init_script(name);
        auto src = "/etc/sv/" + name;
        auto dst = "/etc/runit/runsvdir/default/" + name;
        return run_cmd({"ln", "-sf", src.c_str(), dst.c_str()});
    }

    static int disable(const std::string& name) {
        auto dst = "/etc/runit/runsvdir/default/" + name;
        return run_cmd({"rm", "-f", dst.c_str()});
    }

    static int reload(const std::string& name) {
        return run_cmd({"sv", "reload", name.c_str()});
    }

    static bool is_enabled(const std::string& name) {
        auto p = "/etc/runit/runsvdir/default/" + name;
        return access(p.c_str(), F_OK) == 0;
    }

    static std::vector<std::string> list() {
        std::vector<std::string> result;
        DIR *d = opendir("/etc/sv");
        if (!d) return result;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            result.emplace_back(e->d_name);
        }
        closedir(d);
        return result;
    }
};

/* ── s6 ─────────────────────────────────────────────────────── */

template<>
struct Backend<Init::S6>
{
    static int start(const std::string& name) {
        ensure_init_script(name);
        return run_cmd({"s6-svc", "-u", name.c_str()});
    }

    static int stop(const std::string& name) {
        return run_cmd({"s6-svc", "-d", name.c_str()});
    }

    static int restart(const std::string& name) {
        return run_cmd({"s6-svc", "-r", name.c_str()});
    }

    static int status(const std::string& name) {
        return run_cmd({"s6-svc", "-a", name.c_str()});
    }

    static int enable(const std::string& name) {
        ensure_init_script(name);
        return run_cmd({"s6-rc", "-u", "change", name.c_str()});
    }

    static int disable(const std::string& name) {
        return run_cmd({"s6-rc", "-d", "change", name.c_str()});
    }

    static int reload(const std::string& name) {
        /* s6 has no reload; fall back to restart */
        return -1;
    }

    static bool is_enabled(const std::string& name) {
        auto p = "/run/s6-rc/compiled/" + name;
        return access(p.c_str(), F_OK) == 0;
    }

    static std::vector<std::string> list() {
        return {};  /* s6-rc compiled services not easily enumerated */
    }
};

/* ── SysVinit ───────────────────────────────────────────────── */

template<>
struct Backend<Init::SysV>
{
    static int start(const std::string& name) {
        ensure_init_script(name);
        auto p = "/etc/init.d/" + name;
        return run_cmd({p.c_str(), "start"});
    }

    static int stop(const std::string& name) {
        ensure_init_script(name);
        auto p = "/etc/init.d/" + name;
        return run_cmd({p.c_str(), "stop"});
    }

    static int restart(const std::string& name) {
        ensure_init_script(name);
        auto p = "/etc/init.d/" + name;
        return run_cmd({p.c_str(), "restart"});
    }

    static int status(const std::string& name) {
        ensure_init_script(name);
        auto p = "/etc/init.d/" + name;
        return run_cmd({p.c_str(), "status"});
    }

    static int enable(const std::string& name) {
        ensure_init_script(name);
        return run_cmd({"update-rc.d", name.c_str(), "defaults"});
    }

    static int disable(const std::string& name) {
        return run_cmd({"update-rc.d", "-f", name.c_str(), "remove"});
    }

    static int reload(const std::string& name) {
        return -1;  /* fallback to restart */
    }

    static bool is_enabled(const std::string& name) {
        auto p = "/etc/init.d/" + name;
        return access(p.c_str(), X_OK) == 0;
    }

    static std::vector<std::string> list() {
        std::vector<std::string> result;
        DIR *d = opendir("/etc/init.d");
        if (!d) return result;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            auto p = std::string("/etc/init.d/") + e->d_name;
            if (access(p.c_str(), X_OK) == 0)
                result.emplace_back(e->d_name);
        }
        closedir(d);
        return result;
    }
};

/* ── Runtime dispatchers ────────────────────────────────────── */

inline Init init_for_name(const std::string& name)
{
    (void)name;
    return detect();
}

inline int start(const std::string& name)
{
    auto s = strip_unit_suffix(name);
    auto init = detect();
    if (init_script_exists(s, init) || find_unit_file(s)) {
        /* check mask */
        auto mp = std::string(MASK_DIR) + "/" + s;
        if (access(mp.c_str(), F_OK) == 0) {
            fprintf(stderr, "Unit %s is masked.\n", s.c_str());
            return 1;
        }
    }
    switch (init) {
    case Init::OpenRC: return Backend<Init::OpenRC>::start(s);
    case Init::Runit:  return Backend<Init::Runit>::start(s);
    case Init::S6:     return Backend<Init::S6>::start(s);
    case Init::SysV:   return Backend<Init::SysV>::start(s);
    default:           return -1;
    }
}

inline int stop(const std::string& name)
{
    auto s = strip_unit_suffix(name);
    switch (detect()) {
    case Init::OpenRC: return Backend<Init::OpenRC>::stop(s);
    case Init::Runit:  return Backend<Init::Runit>::stop(s);
    case Init::S6:     return Backend<Init::S6>::stop(s);
    case Init::SysV:   return Backend<Init::SysV>::stop(s);
    default:           return -1;
    }
}

inline int restart(const std::string& name)
{
    auto s = strip_unit_suffix(name);
    switch (detect()) {
    case Init::OpenRC: return Backend<Init::OpenRC>::restart(s);
    case Init::Runit:  return Backend<Init::Runit>::restart(s);
    case Init::S6:     return Backend<Init::S6>::restart(s);
    case Init::SysV:   return Backend<Init::SysV>::restart(s);
    default:           return -1;
    }
}

inline int status(const std::string& name)
{
    auto s = strip_unit_suffix(name);
    switch (detect()) {
    case Init::OpenRC: return Backend<Init::OpenRC>::status(s);
    case Init::Runit:  return Backend<Init::Runit>::status(s);
    case Init::S6:     return Backend<Init::S6>::status(s);
    case Init::SysV:   return Backend<Init::SysV>::status(s);
    default:           return -1;
    }
}

inline int enable(const std::string& name)
{
    auto s = strip_unit_suffix(name);
    switch (detect()) {
    case Init::OpenRC: return Backend<Init::OpenRC>::enable(s);
    case Init::Runit:  return Backend<Init::Runit>::enable(s);
    case Init::S6:     return Backend<Init::S6>::enable(s);
    case Init::SysV:   return Backend<Init::SysV>::enable(s);
    default:           return -1;
    }
}

inline int disable(const std::string& name)
{
    auto s = strip_unit_suffix(name);
    switch (detect()) {
    case Init::OpenRC: return Backend<Init::OpenRC>::disable(s);
    case Init::Runit:  return Backend<Init::Runit>::disable(s);
    case Init::S6:     return Backend<Init::S6>::disable(s);
    case Init::SysV:   return Backend<Init::SysV>::disable(s);
    default:           return -1;
    }
}

inline int reload(const std::string& name)
{
    auto s = strip_unit_suffix(name);
    auto init = detect();
    int rc;
    switch (init) {
    case Init::OpenRC: rc = Backend<Init::OpenRC>::reload(s); break;
    case Init::Runit:  rc = Backend<Init::Runit>::reload(s);  break;
    case Init::S6:     rc = Backend<Init::S6>::reload(s);     break;
    case Init::SysV:   rc = Backend<Init::SysV>::reload(s);   break;
    default:           rc = -1; break;
    }
    if (rc != 0) rc = restart(name);
    return rc;
}

inline bool is_active(const std::string& name)
{
    return status(name) == 0;
}

inline bool is_enabled(const std::string& name)
{
    auto s = strip_unit_suffix(name);
    switch (detect()) {
    case Init::OpenRC: return Backend<Init::OpenRC>::is_enabled(s);
    case Init::Runit:  return Backend<Init::Runit>::is_enabled(s);
    case Init::S6:     return Backend<Init::S6>::is_enabled(s);
    case Init::SysV:   return Backend<Init::SysV>::is_enabled(s);
    default:           return false;
    }
}

inline std::vector<std::string> list()
{
    switch (detect()) {
    case Init::OpenRC: return Backend<Init::OpenRC>::list();
    case Init::Runit:  return Backend<Init::Runit>::list();
    case Init::S6:     return Backend<Init::S6>::list();
    case Init::SysV:   return Backend<Init::SysV>::list();
    default:           return {};
    }
}

inline std::vector<std::string> list_files()
{
    return list();
}

/* ── System operations ──────────────────────────────────────── */

inline int poweroff()
{
    switch (detect()) {
    case Init::OpenRC: return run_cmd({"openrc-shutdown", "-p", "now"});
    case Init::Runit:  return run_cmd({"shutdown", "-h", "now"});
    case Init::S6:     return run_cmd({"s6-svscanctl", "-p", "/run/s6-rc/servicedirs"});
    case Init::SysV:   return run_cmd({"shutdown", "-h", "now"});
    default:           return -1;
    }
}

inline int reboot()
{
    switch (detect()) {
    case Init::OpenRC: return run_cmd({"openrc-shutdown", "-r", "now"});
    case Init::Runit:  return run_cmd({"shutdown", "-r", "now"});
    case Init::S6:     return run_cmd({"s6-svscanctl", "-r", "/run/s6-rc/servicedirs"});
    case Init::SysV:   return run_cmd({"shutdown", "-r", "now"});
    default:           return -1;
    }
}

inline int halt()
{
    switch (detect()) {
    case Init::OpenRC: return run_cmd({"openrc-shutdown", "-H", "now"});
    case Init::Runit:  return run_cmd({"halt"});
    case Init::S6:     return run_cmd({"s6-svscanctl", "-h", "/run/s6-rc/servicedirs"});
    case Init::SysV:   return run_cmd({"halt"});
    default:           return -1;
    }
}

inline int daemon_reload()
{
    return 0;
}

/* ── Compatibility operations ───────────────────────────────── */

inline int mask(const std::string& name)
{
    auto s = strip_unit_suffix(name);
    std::filesystem::create_directories(MASK_DIR);
    auto p = std::string(MASK_DIR) + "/" + s;
    std::ofstream(p).put('\n');
    return access(p.c_str(), F_OK) == 0 ? 0 : 1;
}

inline int unmask(const std::string& name)
{
    auto s = strip_unit_suffix(name);
    auto p = std::string(MASK_DIR) + "/" + s;
    return unlink(p.c_str()) == 0 ? 0 : 1;
}

inline int try_restart(const std::string& name)
{
    if (is_active(name)) return restart(name);
    return 0;
}

inline int reenable(const std::string& name)
{
    auto rc = disable(name);
    rc |= enable(name);
    return rc;
}

inline int is_failed(const std::string&)
{
    return 1;
}

inline int kill(const std::string& name, const std::string& signal)
{
    auto s = strip_unit_suffix(name);
    char cmd[768];
    snprintf(cmd, sizeof(cmd),
        "p=$(cat /var/run/%s.pid 2>/dev/null; "
        "cat /run/%s.pid 2>/dev/null); "
        "[ -n \"$p\" ] && kill -s %s $p 2>/dev/null || true",
        s.c_str(), s.c_str(), signal.c_str());
    return run_cmd({"sh", "-c", cmd});
}

inline int daemon_reexec()
{
    return 0;
}

inline int reset_failed(const std::string&)
{
    return 0;
}

inline int link(const std::string& path)
{
    auto base = std::filesystem::path(path).filename();
    auto s = strip_unit_suffix(base.string());
    auto p = "/etc/init.d/" + s;
    if (symlink(path.c_str(), p.c_str()) != 0) {
        perror("symlink");
        return 1;
    }
    return 0;
}

inline int preset(const std::string& name)
{
    return enable(name);
}

inline int reload_or_restart(const std::string& name)
{
    int rc = reload(name);
    if (rc != 0) rc = restart(name);
    return rc;
}

inline int try_reload_or_restart(const std::string& name)
{
    if (!is_active(name)) return 0;
    return reload_or_restart(name);
}

inline int show(const std::string& name)
{
    if (name.empty()) {
        printf("Manager: systemd-shimd (pid %d)\n", getpid());
        printf("Init: %s\n", init_name(detect()));
        return 0;
    }
    auto s = strip_unit_suffix(name);
    auto init = detect();
    printf("Unit: %s\n", s.c_str());
    printf("Init: %s\n", init_name(init));
    printf("Has init script: %s\n", init_script_exists(s, init) ? "yes" : "no");
    auto u = find_unit_file(s);
    printf("Has systemd unit: %s\n", u ? "yes" : "no");
    if (u) printf("Systemd unit path: %s\n", u->c_str());
    return 0;
}

inline int cat(const std::string& name)
{
    auto s = strip_unit_suffix(name);
    auto p = init_script_path(s, detect());
    std::ifstream f(p);
    if (!f) {
        auto u = find_unit_file(s);
        if (u) f.open(*u);
        if (!f) return 1;
    }
    std::cout << f.rdbuf();
    return 0;
}

inline int edit(const std::string& name)
{
    auto s = strip_unit_suffix(name);
    ensure_init_script(s);
    auto p = init_script_path(s, detect());
    if (p.empty() || access(p.c_str(), F_OK) != 0) {
        /* try systemd unit file directly */
        auto u = find_unit_file(s);
        if (!u) { fprintf(stderr, "no unit file found for '%s'\n", s.c_str()); return 1; }
        p = *u;
    }
    const char *editor = getenv("EDITOR");
    if (!editor) editor = "vi";
    return run_cmd({editor, p.c_str()});
}

inline int add_wants(const std::string& target, const std::string& unit)
{
    auto dir = std::string("/etc/systemd-shimd/wants/") + target;
    std::filesystem::create_directories(dir);
    auto path = dir + "/" + unit;
    std::ofstream(path).put('\n');
    fprintf(stderr, "warning: add-wants is advisory — dependency not enforced by %s\n",
            init_name(detect()));
    return 0;
}

}  // namespace shimd
