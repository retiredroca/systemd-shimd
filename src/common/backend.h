#ifndef BACKEND_H
#define BACKEND_H

#include "init_detect.h"

typedef struct {
    const char *name;

    int (*start)(const char *name);
    int (*stop)(const char *name);
    int (*restart)(const char *name);
    int (*status)(const char *name);
    int (*enable)(const char *name);
    int (*disable)(const char *name);
    int (*is_active)(const char *name);
    int (*is_enabled)(const char *name);
    char **(*list_units)(void);
    int (*daemon_reload)(void);
    int (*poweroff)(void);
    int (*reboot)(void);
    int (*halt)(void);
} InitBackend;

const InitBackend *get_backend(InitType type);

#endif
