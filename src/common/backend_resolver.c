#include <stddef.h>
#include "backend.h"

extern const InitBackend *get_openrc_backend(void);
extern const InitBackend *get_runit_backend(void);
extern const InitBackend *get_s6_backend(void);
extern const InitBackend *get_sysvinit_backend(void);

const InitBackend *get_backend(InitType type)
{
    switch (type) {
    case INIT_OPENRC: return get_openrc_backend();
    case INIT_RUNIT:  return get_runit_backend();
    case INIT_S6:     return get_s6_backend();
    case INIT_SYSV:   return get_sysvinit_backend();
    default:          return NULL;
    }
}
