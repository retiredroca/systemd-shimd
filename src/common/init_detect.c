#include "init_detect.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

InitType detect_init_system(void)
{
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

    if (strcmp(comm, "openrc-init") == 0) return INIT_OPENRC;
    if (strcmp(comm, "runit") == 0)       return INIT_RUNIT;
    if (strcmp(comm, "s6-svscan") == 0)   return INIT_S6;

    if (access("/sbin/rc-service", X_OK) == 0 ||
        access("/usr/sbin/rc-service", X_OK) == 0 ||
        access("/usr/bin/rc-service", X_OK) == 0)
        return INIT_OPENRC;

    if (access("/usr/bin/sv", X_OK) == 0 ||
        access("/bin/sv", X_OK) == 0 ||
        access("/sbin/sv", X_OK) == 0)
        return INIT_RUNIT;

    if (access("/usr/bin/s6-svc", X_OK) == 0 ||
        access("/bin/s6-svc", X_OK) == 0)
        return INIT_S6;

    return INIT_SYSV;
}

const char *init_type_name(InitType type)
{
    switch (type) {
    case INIT_OPENRC: return "openrc";
    case INIT_RUNIT:  return "runit";
    case INIT_S6:     return "s6";
    case INIT_SYSV:   return "sysvinit";
    default:          return "unknown";
    }
}
