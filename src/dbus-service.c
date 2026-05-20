#include "systemd-shimd.h"
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <sys/utsname.h>

#define MANAGER_PATH   "/org/freedesktop/systemd1"
#define UNIT_PREFIX    "/org/freedesktop/systemd1/unit"
#define MANAGER_IFACE  "org.freedesktop.systemd1.Manager"
#define UNIT_IFACE     "org.freedesktop.systemd1.Unit"
#define PROP_IFACE     "org.freedesktop.DBus.Properties"

typedef struct {
    char version[32];
    char architecture[16];
    char features[64];
    char tainted[32];
} ServiceData;

static void build_unit_path(const char *name, char **out)
{
    sd_bus_path_encode(UNIT_PREFIX, name, out);
}

static int unit_exists(const char *name)
{
    char *s = strip_unit_suffix(name);
    size_t len = strlen("/etc/init.d/") + strlen(s) + 1;
    char *path = malloc(len);
    snprintf(path, len, "/etc/init.d/%s", s);
    int rc = access(path, X_OK) == 0;
    free(s); free(path);
    return rc;
}

static int unit_get_active_state(const char *name, char **state)
{
    int rc = unit_status(name);
    *state = strdup(rc == 0 ? "active" : "inactive");
    return rc;
}

static int unit_get_sub_state(const char *name, char **substate)
{
    int rc = unit_status(name);
    *substate = strdup(rc == 0 ? "running" : "dead");
    return rc;
}

/* ── Manager property getters ────────────────────────────────── */

static int prop_version(sd_bus *bus, const char *path, const char *iface,
                         const char *prop, sd_bus_message *reply,
                         void *userdata, sd_bus_error *ret_error)
{
    (void)bus; (void)path; (void)iface; (void)prop; (void)ret_error;
    return sd_bus_message_append(reply, "s", ((ServiceData *)userdata)->version);
}

static int prop_features(sd_bus *bus, const char *path, const char *iface,
                          const char *prop, sd_bus_message *reply,
                          void *userdata, sd_bus_error *ret_error)
{
    (void)bus; (void)path; (void)iface; (void)prop; (void)userdata; (void)ret_error;
    return sd_bus_message_append(reply, "s", "");
}

static int prop_architecture(sd_bus *bus, const char *path, const char *iface,
                              const char *prop, sd_bus_message *reply,
                              void *userdata, sd_bus_error *ret_error)
{
    (void)bus; (void)path; (void)iface; (void)prop; (void)ret_error;
    return sd_bus_message_append(reply, "s", ((ServiceData *)userdata)->architecture);
}

static int prop_tainted(sd_bus *bus, const char *path, const char *iface,
                         const char *prop, sd_bus_message *reply,
                         void *userdata, sd_bus_error *ret_error)
{
    (void)bus; (void)path; (void)iface; (void)prop; (void)userdata; (void)ret_error;
    return sd_bus_message_append(reply, "s", "");
}

static int prop_virtualization(sd_bus *bus, const char *path, const char *iface,
                                const char *prop, sd_bus_message *reply,
                                void *userdata, sd_bus_error *ret_error)
{
    (void)bus; (void)path; (void)iface; (void)prop; (void)userdata; (void)ret_error;
    return sd_bus_message_append(reply, "s", "");
}

static int prop_empty_string(sd_bus *bus, const char *path, const char *iface,
                              const char *prop, sd_bus_message *reply,
                              void *userdata, sd_bus_error *ret_error)
{
    (void)bus; (void)path; (void)iface; (void)prop; (void)userdata; (void)ret_error;
    return sd_bus_message_append(reply, "s", "");
}

/* ── Manager method handlers ─────────────────────────────────── */

static int method_start_unit(sd_bus_message *m, void *userdata,
                              sd_bus_error *ret_error)
{
    (void)userdata;
    const char *name, *mode;
    int r = sd_bus_message_read(m, "ss", &name, &mode);
    if (r < 0) return r;
    r = unit_start(name);
    if (r != 0)
        return sd_bus_error_setf(ret_error, "org.freedesktop.systemd1.Error.Failed",
                                 "Failed to start unit '%s'", name);
    char *unit_path;
    build_unit_path(name, &unit_path);
    r = sd_bus_reply_method_return(m, "o", unit_path);
    free(unit_path);
    return r;
}

static int method_stop_unit(sd_bus_message *m, void *userdata,
                             sd_bus_error *ret_error)
{
    (void)userdata;
    const char *name, *mode;
    int r = sd_bus_message_read(m, "ss", &name, &mode);
    if (r < 0) return r;
    r = unit_stop(name);
    if (r != 0)
        return sd_bus_error_setf(ret_error, "org.freedesktop.systemd1.Error.Failed",
                                 "Failed to stop unit '%s'", name);
    char *unit_path;
    build_unit_path(name, &unit_path);
    r = sd_bus_reply_method_return(m, "o", unit_path);
    free(unit_path);
    return r;
}

static int method_restart_unit(sd_bus_message *m, void *userdata,
                                sd_bus_error *ret_error)
{
    (void)userdata;
    const char *name, *mode;
    int r = sd_bus_message_read(m, "ss", &name, &mode);
    if (r < 0) return r;
    r = unit_restart(name);
    if (r != 0)
        return sd_bus_error_setf(ret_error, "org.freedesktop.systemd1.Error.Failed",
                                 "Failed to restart unit '%s'", name);
    char *unit_path;
    build_unit_path(name, &unit_path);
    r = sd_bus_reply_method_return(m, "o", unit_path);
    free(unit_path);
    return r;
}

static int method_reload_unit(sd_bus_message *m, void *userdata,
                               sd_bus_error *ret_error)
{
    (void)m; (void)userdata;
    return sd_bus_error_setf(ret_error, "org.freedesktop.systemd1.Error.Failed",
                             "ReloadUnit not supported");
}

static int method_get_unit(sd_bus_message *m, void *userdata,
                            sd_bus_error *ret_error)
{
    (void)userdata;
    const char *name;
    int r = sd_bus_message_read(m, "s", &name);
    if (r < 0) return r;
    if (!unit_exists(name))
        return sd_bus_error_setf(ret_error,
                "org.freedesktop.systemd1.Error.UnitNotFound",
                "Unit '%s' not found", name);
    char *unit_path;
    build_unit_path(name, &unit_path);
    r = sd_bus_reply_method_return(m, "o", unit_path);
    free(unit_path);
    return r;
}

static int method_get_unit_by_pid(sd_bus_message *m, void *userdata,
                                   sd_bus_error *ret_error)
{
    (void)m; (void)userdata;
    return sd_bus_error_setf(ret_error, "org.freedesktop.systemd1.Error.Failed",
                             "GetUnitByPID not supported");
}

static int method_load_unit(sd_bus_message *m, void *userdata,
                             sd_bus_error *ret_error)
{
    return method_get_unit(m, userdata, ret_error);
}

static int method_list_units(sd_bus_message *m, void *userdata,
                              sd_bus_error *ret_error)
{
    (void)ret_error;
    (void)userdata;
    char **units = unit_list();
    if (!units)
        return sd_bus_reply_method_return(m, "a(ssssssouso)", 0);

    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) goto cleanup;

    r = sd_bus_message_open_container(reply, 'a', "(ssssssouso)");
    if (r < 0) goto cleanup;

    for (int i = 0; units[i]; i++) {
        const char *name = units[i];
        char *sname = strip_unit_suffix(name);
        char *unit_path;
        build_unit_path(name, &unit_path);
        r = sd_bus_message_append(reply, "(ssssssouso)",
            name, sname, "loaded", "active", "running",
            "", unit_path, (uint32_t)0, "", "/");
        free(unit_path);
        free(sname);
        if (r < 0) break;
    }

    if (r >= 0) r = sd_bus_message_close_container(reply);
    if (r >= 0) r = sd_bus_send(NULL, reply, NULL);

cleanup:
    if (reply) sd_bus_message_unref(reply);
    for (int i = 0; units[i]; i++) free(units[i]);
    free(units);
    return r;
}

static int method_list_unit_files(sd_bus_message *m, void *userdata,
                                   sd_bus_error *ret_error)
{
    (void)ret_error;
    (void)userdata;
    char **units = unit_list();
    if (!units)
        return sd_bus_reply_method_return(m, "a(ss)", 0);

    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) goto cleanup;

    r = sd_bus_message_open_container(reply, 'a', "(ss)");
    if (r < 0) goto cleanup;

    for (int i = 0; units[i]; i++) {
        const char *state = unit_is_enabled(units[i]) == 0 ? "enabled" : "disabled";
        size_t len = strlen("/etc/init.d/") + strlen(units[i]) + 1;
        char *path = malloc(len);
        snprintf(path, len, "/etc/init.d/%s", units[i]);
        r = sd_bus_message_append(reply, "(ss)", path, state);
        free(path);
        if (r < 0) break;
    }

    if (r >= 0) r = sd_bus_message_close_container(reply);
    if (r >= 0) r = sd_bus_send(NULL, reply, NULL);

cleanup:
    if (reply) sd_bus_message_unref(reply);
    for (int i = 0; units[i]; i++) free(units[i]);
    free(units);
    return r;
}

static int method_get_unit_file_state(sd_bus_message *m, void *userdata,
                                       sd_bus_error *ret_error)
{
    (void)ret_error; (void)userdata;
    const char *file;
    int r = sd_bus_message_read(m, "s", &file);
    if (r < 0) return r;
    const char *state = unit_is_enabled(file) == 0 ? "enabled" : "disabled";
    return sd_bus_reply_method_return(m, "s", state);
}

static int method_enable_unit_files(sd_bus_message *m, void *userdata,
                                     sd_bus_error *ret_error)
{
    (void)ret_error; (void)userdata;
    int runtime, force;
    int r = sd_bus_message_read(m, "abb", &runtime, &force);
    if (r < 0) return r;

    sd_bus_message *reply = NULL;
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    r = sd_bus_message_open_container(reply, 'a', "(ss)");
    if (r < 0) { sd_bus_message_unref(reply); return r; }

    int carries = 0;
    const char *file;
    r = sd_bus_message_enter_container(m, 'a', "s");
    if (r < 0) { sd_bus_message_unref(reply); return r; }

    while ((r = sd_bus_message_read_basic(m, 's', &file)) > 0) {
        int rc = unit_enable(file);
        if (rc == 0) carries = 1;
        sd_bus_message_append(reply, "(ss)", rc == 0 ? "symlink" : "error", file);
    }
    sd_bus_message_exit_container(m);

    r = sd_bus_message_close_container(reply);
    if (r < 0) { sd_bus_message_unref(reply); return r; }

    r = sd_bus_message_append(reply, "b", carries);
    if (r < 0) { sd_bus_message_unref(reply); return r; }

    return sd_bus_send(NULL, reply, NULL);
}

static int method_disable_unit_files(sd_bus_message *m, void *userdata,
                                      sd_bus_error *ret_error)
{
    (void)ret_error; (void)userdata;
    int runtime;
    int r = sd_bus_message_read(m, "ab", &runtime);
    if (r < 0) return r;

    sd_bus_message *reply = NULL;
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    r = sd_bus_message_open_container(reply, 'a', "(ss)");
    if (r < 0) { sd_bus_message_unref(reply); return r; }

    int carries = 0;
    const char *file;
    r = sd_bus_message_enter_container(m, 'a', "s");
    if (r < 0) { sd_bus_message_unref(reply); return r; }

    while ((r = sd_bus_message_read_basic(m, 's', &file)) > 0) {
        int rc = unit_disable(file);
        if (rc == 0) carries = 1;
        sd_bus_message_append(reply, "(ss)", rc == 0 ? "symlink" : "error", file);
    }
    sd_bus_message_exit_container(m);

    r = sd_bus_message_close_container(reply);
    if (r < 0) { sd_bus_message_unref(reply); return r; }

    r = sd_bus_message_append(reply, "b", carries);
    if (r < 0) { sd_bus_message_unref(reply); return r; }

    return sd_bus_send(NULL, reply, NULL);
}

static int method_reload(sd_bus_message *m, void *userdata,
                          sd_bus_error *ret_error)
{
    (void)ret_error; (void)userdata;
    daemon_reload();
    return sd_bus_reply_method_return(m, "");
}

static int method_reexecute(sd_bus_message *m, void *userdata,
                             sd_bus_error *ret_error)
{
    (void)userdata; (void)ret_error;
    return sd_bus_reply_method_return(m, "");
}

/* ── Unit object property getters ────────────────────────────── */

struct UnitInfo {
    char name[256];
    char stripped[256];
};

static int unit_find(sd_bus *bus, const char *path, const char *interface,
                     void *userdata, void **ret_found, sd_bus_error *ret_error)
{
    (void)bus; (void)interface; (void)userdata;
    char *name = NULL;
    sd_bus_path_decode(path, UNIT_PREFIX, &name);
    if (!name)
        return sd_bus_error_setf(ret_error, SD_BUS_ERROR_FAILED,
                                 "Failed to decode unit path: %s", path);
    if (!unit_exists(name)) { free(name); return 0; }

    struct UnitInfo *info = calloc(1, sizeof(struct UnitInfo));
    strncpy(info->name, name, sizeof(info->name) - 1);
    char *s = strip_unit_suffix(name);
    strncpy(info->stripped, s, sizeof(info->stripped) - 1);
    free(s); free(name);
    *ret_found = info;
    return 1;
}

static int unit_prop_id(sd_bus *bus, const char *path, const char *iface,
                         const char *prop, sd_bus_message *reply,
                         void *userdata, sd_bus_error *ret_error)
{
    (void)bus; (void)path; (void)iface; (void)prop; (void)ret_error;
    return sd_bus_message_append(reply, "s", ((struct UnitInfo *)userdata)->name);
}

static int unit_prop_description(sd_bus *bus, const char *path, const char *iface,
                                  const char *prop, sd_bus_message *reply,
                                  void *userdata, sd_bus_error *ret_error)
{
    (void)bus; (void)path; (void)iface; (void)prop; (void)ret_error;
    return sd_bus_message_append(reply, "s", ((struct UnitInfo *)userdata)->stripped);
}

static int unit_prop_load_state(sd_bus *bus, const char *path, const char *iface,
                                 const char *prop, sd_bus_message *reply,
                                 void *userdata, sd_bus_error *ret_error)
{
    (void)bus; (void)path; (void)iface; (void)prop; (void)userdata; (void)ret_error;
    return sd_bus_message_append(reply, "s", "loaded");
}

static int unit_prop_active_state(sd_bus *bus, const char *path, const char *iface,
                                   const char *prop, sd_bus_message *reply,
                                   void *userdata, sd_bus_error *ret_error)
{
    (void)bus; (void)path; (void)iface; (void)prop; (void)ret_error;
    char *state = NULL;
    unit_get_active_state(((struct UnitInfo *)userdata)->name, &state);
    int r = sd_bus_message_append(reply, "s", state ? state : "inactive");
    free(state);
    return r;
}

static int unit_prop_sub_state(sd_bus *bus, const char *path, const char *iface,
                                const char *prop, sd_bus_message *reply,
                                void *userdata, sd_bus_error *ret_error)
{
    (void)bus; (void)path; (void)iface; (void)prop; (void)ret_error;
    char *state = NULL;
    unit_get_sub_state(((struct UnitInfo *)userdata)->name, &state);
    int r = sd_bus_message_append(reply, "s", state ? state : "dead");
    free(state);
    return r;
}

static int unit_prop_path(sd_bus *bus, const char *path, const char *iface,
                           const char *prop, sd_bus_message *reply,
                           void *userdata, sd_bus_error *ret_error)
{
    (void)bus; (void)path; (void)iface; (void)prop; (void)ret_error;
    const char *stripped = ((struct UnitInfo *)userdata)->stripped;
    size_t len = strlen("/etc/init.d/") + strlen(stripped) + 1;
    char *fp = malloc(len);
    snprintf(fp, len, "/etc/init.d/%s", stripped);
    int r = sd_bus_message_append(reply, "s", fp);
    free(fp);
    return r;
}

static int unit_prop_documentation(sd_bus *bus, const char *path, const char *iface,
                                    const char *prop, sd_bus_message *reply,
                                    void *userdata, sd_bus_error *ret_error)
{
    (void)bus; (void)path; (void)iface; (void)prop; (void)userdata; (void)ret_error;
    return sd_bus_message_append(reply, "as", 0);
}

/* ── Node enumerator for unit objects ────────────────────────── */

static int enumerate_units(sd_bus *bus, const char *prefix,
                            void *userdata, char ***ret_nodes,
                            sd_bus_error *ret_error)
{
    (void)bus; (void)prefix; (void)ret_error;
    (void)userdata;
    char **units = unit_list();
    if (!units) { *ret_nodes = NULL; return 0; }

    size_t count = 0;
    for (int i = 0; units[i]; i++) count++;

    char **nodes = calloc(count + 1, sizeof(char *));
    for (size_t i = 0; i < count; i++) {
        char *path;
        build_unit_path(units[i], &path);
        nodes[i] = path;
    }
    nodes[count] = NULL;

    for (int i = 0; units[i]; i++) free(units[i]);
    free(units);

    *ret_nodes = nodes;
    return 1;
}

/* ── Vtables ─────────────────────────────────────────────────── */

static const sd_bus_vtable manager_vtable[] = {
    SD_BUS_VTABLE_START(0),

    SD_BUS_METHOD("StartUnit",    "ss", "o", method_start_unit,      0),
    SD_BUS_METHOD("StopUnit",     "ss", "o", method_stop_unit,       0),
    SD_BUS_METHOD("RestartUnit",  "ss", "o", method_restart_unit,    0),
    SD_BUS_METHOD("ReloadUnit",   "ss", "o", method_reload_unit,     0),
    SD_BUS_METHOD("GetUnit",      "s",  "o", method_get_unit,        0),
    SD_BUS_METHOD("GetUnitByPID", "u",  "o", method_get_unit_by_pid, 0),
    SD_BUS_METHOD("LoadUnit",     "s",  "o", method_load_unit,       0),

    SD_BUS_METHOD("ListUnits",         "",    "a(ssssssouso)", method_list_units,        0),
    SD_BUS_METHOD("ListUnitFiles",     "",    "a(ss)",         method_list_unit_files,    0),
    SD_BUS_METHOD("GetUnitFileState",  "s",   "s",             method_get_unit_file_state, 0),
    SD_BUS_METHOD("EnableUnitFiles",   "asbb","ba(ss)",        method_enable_unit_files,   0),
    SD_BUS_METHOD("DisableUnitFiles",  "asb", "ba(ss)",        method_disable_unit_files,  0),

    SD_BUS_METHOD("Reload",    "", "", method_reload,    0),
    SD_BUS_METHOD("Reexecute", "", "", method_reexecute, 0),

    SD_BUS_PROPERTY("Version",         "s", prop_version,        0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Features",        "s", prop_features,       0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Architecture",    "s", prop_architecture,   0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Tainted",         "s", prop_tainted,        0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Virtualization",  "s", prop_virtualization, 0, SD_BUS_VTABLE_PROPERTY_CONST),

    SD_BUS_VTABLE_END,
};

static const sd_bus_vtable unit_vtable[] = {
    SD_BUS_VTABLE_START(0),

    SD_BUS_PROPERTY("Id",             "s",  unit_prop_id,            0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Description",    "s",  unit_prop_description,   0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("LoadState",      "s",  unit_prop_load_state,    0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ActiveState",    "s",  unit_prop_active_state,  0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("SubState",       "s",  unit_prop_sub_state,     0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Following",      "s",  prop_empty_string,       0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("UnitFilePath",   "s",  unit_prop_path,          0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("FragmentPath",   "s",  unit_prop_path,          0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("SourcePath",     "s",  prop_empty_string,       0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Documentation",  "as", unit_prop_documentation, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),

    SD_BUS_VTABLE_END,
};

/* ── Main ────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    InitType init = detect_init_system();
    if (init == INIT_UNKNOWN) {
        fprintf(stderr, "systemd-shimd: unsupported init system\n");
        return 1;
    }

    ServiceData data;
    snprintf(data.version, sizeof(data.version), "systemd-shimd 0.1.0");
    data.features[0] = '\0';
    struct utsname uts;
    uname(&uts);
    strncpy(data.architecture, uts.machine, sizeof(data.architecture) - 1);

    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("systemd-shimd 0.1.0\n");
        return 0;
    }
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf("Usage: systemd-shimd [--bus|--version|--help]\n");
        printf("\nD-BUS service implementing org.freedesktop.systemd1\n");
        printf("for non-systemd systems.\n");
        return 0;
    }

    sd_bus *bus = NULL;
    int r = sd_bus_open_system(&bus);
    if (r < 0) {
        fprintf(stderr, "systemd-shimd: failed to connect to system bus: %s\n",
                strerror(-r));
        return 1;
    }

    r = sd_bus_add_object_vtable(bus, NULL, MANAGER_PATH, MANAGER_IFACE,
                                 manager_vtable, &data);
    if (r < 0) {
        fprintf(stderr, "systemd-shimd: failed to add manager vtable: %s\n",
                strerror(-r));
        sd_bus_unref(bus);
        return 1;
    }

    r = sd_bus_add_node_enumerator(bus, NULL, UNIT_PREFIX, enumerate_units, &data);
    if (r < 0) {
        fprintf(stderr, "systemd-shimd: failed to add node enumerator: %s\n",
                strerror(-r));
        sd_bus_unref(bus);
        return 1;
    }

    r = sd_bus_add_fallback_vtable(bus, NULL, UNIT_PREFIX, UNIT_IFACE,
                                   unit_vtable, unit_find, &data);
    if (r < 0) {
        fprintf(stderr, "systemd-shimd: failed to add unit fallback vtable: %s\n",
                strerror(-r));
        sd_bus_unref(bus);
        return 1;
    }

    r = sd_bus_add_fallback_vtable(bus, NULL, UNIT_PREFIX, PROP_IFACE,
                                   unit_vtable, unit_find, &data);

    r = sd_bus_request_name(bus, "org.freedesktop.systemd1", 0);
    if (r < 0) {
        fprintf(stderr, "systemd-shimd: failed to acquire bus name: %s\n",
                strerror(-r));
        sd_bus_unref(bus);
        return 1;
    }

    fprintf(stderr, "systemd-shimd: running as org.freedesktop.systemd1 (%s backend)\n",
            init_type_name(init));

    sd_event *event = NULL;
    sd_event_new(&event);
    sd_bus_attach_event(bus, event, SD_EVENT_PRIORITY_NORMAL);
    r = sd_event_loop(event);
    if (r < 0)
        fprintf(stderr, "systemd-shimd: event loop failed: %s\n", strerror(-r));

    sd_event_unref(event);
    sd_bus_unref(bus);
    return r < 0 ? 1 : 0;
}
