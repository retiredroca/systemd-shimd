#include "systemd-shimd.hpp"

using namespace shimd;

extern "C" {
    #include <elogind/systemd/sd-bus.h>
    #include <elogind/systemd/sd-event.h>
}

#define BUS_NAME "org.freedesktop.systemd1"
#define BUS_PATH "/org/freedesktop/systemd1"
#define BUS_MANAGER BUS_NAME ".Manager"
#define BUS_UNIT    BUS_NAME ".Unit"

/* ── callback helpers ───────────────────────────────────────── */

static std::string msg_unit_name(sd_bus_message *m)
{
    const char *s = nullptr;
    sd_bus_message_read(m, "s", &s);
    return s ? s : "";
}

static int reply_ok(sd_bus_message *m)
{
    return sd_bus_reply_method_return(m, "");
}

static int reply_rc(sd_bus_message *m, int rc)
{
    if (rc == 0) return sd_bus_reply_method_return(m, "");
    return sd_bus_reply_method_errorf(m, BUS_NAME ".Error",
        "operation failed with exit code %d", rc);
}

/* ── method handlers ────────────────────────────────────────── */

static int method_start(sd_bus_message *m, void *, sd_bus_error *)
{
    auto name = msg_unit_name(m);
    return reply_rc(m, start(name));
}

static int method_stop(sd_bus_message *m, void *, sd_bus_error *)
{
    auto name = msg_unit_name(m);
    return reply_rc(m, stop(name));
}

static int method_restart(sd_bus_message *m, void *, sd_bus_error *)
{
    auto name = msg_unit_name(m);
    return reply_rc(m, restart(name));
}

static int method_reload(sd_bus_message *m, void *, sd_bus_error *)
{
    auto name = msg_unit_name(m);
    return reply_rc(m, reload(name));
}

static int method_enable(sd_bus_message *m, void *, sd_bus_error *)
{
    const char *name = nullptr;
    sd_bus_message_read(m, "s", &name);
    if (!name) return sd_bus_reply_method_errorf(m, BUS_NAME ".Error", "missing name");

    int runtime = 0;
    sd_bus_message_read(m, "b", &runtime);
    (void)runtime;

    return sd_bus_reply_method_return(m, "a(s)", 0);
}

static int method_disable(sd_bus_message *m, void *, sd_bus_error *)
{
    const char *name = nullptr;
    sd_bus_message_read(m, "s", &name);
    if (!name) return sd_bus_reply_method_errorf(m, BUS_NAME ".Error", "missing name");

    int runtime = 0;
    sd_bus_message_read(m, "b", &runtime);
    (void)runtime;

    return sd_bus_reply_method_return(m, "a(s)", 0);
}

static int method_unit_exists(sd_bus_message *m, void *, sd_bus_error *)
{
    auto name = msg_unit_name(m);
    int exists = access(init_script_path(strip_unit_suffix(name), detect()).c_str(), F_OK) == 0
              || find_unit_file(name).has_value();
    return sd_bus_reply_method_return(m, "b", exists);
}

static int method_list_units(sd_bus_message *m, void *, sd_bus_error *)
{
    auto units = list();

    sd_bus_message *reply = nullptr;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    sd_bus_message_open_container(reply, 'a', "(ssssssouso)");
    for (auto &u : units) {
        sd_bus_message_open_container(reply, 'r', "ssssssouso");
        sd_bus_message_append(reply, "s", u.c_str());
        sd_bus_message_append(reply, "s", u.c_str());
        sd_bus_message_append(reply, "s", "loaded");
        sd_bus_message_append(reply, "s", "active");
        sd_bus_message_append(reply, "s", "running");
        sd_bus_message_append(reply, "s", "enabled");
        sd_bus_message_append(reply, "o", "");
        sd_bus_message_append(reply, "u", 0);
        sd_bus_message_append(reply, "o", "");
        sd_bus_message_close_container(reply);
    }
    sd_bus_message_close_container(reply);

    return sd_bus_send(nullptr, reply, nullptr);
}

/* ── node enumerator ────────────────────────────────────────── */

static int node_enumerator(sd_bus *, const char *path, void *, char ***nodes, sd_bus_error *)
{
    auto units = list();
    std::vector<const char*> ptrs;
    for (auto &u : units) {
        auto p = new std::string(std::string("/org/freedesktop/systemd1/unit/") + u);
        ptrs.push_back(p->c_str());
    }

    *nodes = (char**)calloc(ptrs.size() + 1, sizeof(char*));
    for (size_t i = 0; i < ptrs.size(); i++)
        (*nodes)[i] = strdup(ptrs[i]);
    return 0;
}

/* ── main ───────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc > 1) {
        if (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
            std::printf(
                "Usage: systemd-shimd [--bus|--version|--help]\n"
                "\n"
                "D-BUS service implementing %s\n"
                "for non-systemd systems.\n", BUS_NAME
            );
            return 0;
        }
        if (std::strcmp(argv[1], "--version") == 0) {
            std::printf("systemd-shimd 0.1.0\n");
            return 0;
        }
    }

    sd_bus *bus = nullptr;
    int r = sd_bus_open_system(&bus);
    if (r < 0) {
        std::fprintf(stderr, "systemd-shimd: failed to acquire bus name: %s\n",
                     std::strerror(-r));
        return 1;
    }

    /* register vtable for the manager object */
    static const sd_bus_vtable manager_vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("StartUnit", "ss", "o", method_start, 0),
        SD_BUS_METHOD("StopUnit", "ss", "o", method_stop, 0),
        SD_BUS_METHOD("ReloadUnit", "ss", "o", method_reload, 0),
        SD_BUS_METHOD("RestartUnit", "ss", "o", method_restart, 0),
        SD_BUS_METHOD("EnableUnitFiles", "asbb", "a(s)", method_enable, 0),
        SD_BUS_METHOD("DisableUnitFiles", "asb", "a(s)", method_disable, 0),
        SD_BUS_METHOD("UnitExists", "s", "b", method_unit_exists, 0),
        SD_BUS_METHOD("ListUnits", nullptr, "a(ssssssouso)", method_list_units, 0),
        SD_BUS_VTABLE_END
    };

    r = sd_bus_add_object_vtable(bus, nullptr, BUS_PATH, BUS_MANAGER, manager_vtable, nullptr);
    if (r < 0) {
        std::fprintf(stderr, "systemd-shimd: failed to add vtable: %s\n",
                     std::strerror(-r));
        return 1;
    }

    /* register node enumerator */
    sd_bus_add_node_enumerator(bus, nullptr, BUS_PATH, node_enumerator, nullptr);

    /* request name */
    r = sd_bus_request_name(bus, BUS_NAME, 0);
    if (r < 0) {
        std::fprintf(stderr, "systemd-shimd: failed to acquire bus name: %s\n",
                     std::strerror(-r));
        return 1;
    }

    /* event loop */
    while (true) {
        r = sd_bus_process(bus, nullptr);
        if (r < 0) break;
        if (r > 0) continue;
        r = sd_bus_wait(bus, UINT64_MAX);
        if (r < 0) break;
    }

    sd_bus_unref(bus);
    return 0;
}
