#ifndef INIT_DETECT_H
#define INIT_DETECT_H

typedef enum {
    INIT_UNKNOWN = 0,
    INIT_OPENRC,
    INIT_RUNIT,
    INIT_S6,
    INIT_SYSV,
} InitType;

InitType detect_init_system(void);
const char *init_type_name(InitType type);

#endif
