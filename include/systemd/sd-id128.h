#ifndef foosdid128hfoo
#define foosdid128hfoo

#include <inttypes.h>
#include <stdint.h>

#include "_sd-common.h"

_SD_BEGIN_DECLARATIONS;

typedef union sd_id128 {
    uint8_t bytes[16];
    uint64_t qwords[2];
} sd_id128_t;

#define SD_ID128_MAKE(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) \
    ((const sd_id128_t){{ \
        0x##a, 0x##b, 0x##c, 0x##d, 0x##e, 0x##f, 0x##g, 0x##h, \
        0x##i, 0x##j, 0x##k, 0x##l, 0x##m, 0x##n, 0x##o, 0x##p  \
    }})

#define SD_ID128_NULL SD_ID128_MAKE(00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00)

#define SD_ID128_FORMAT_STR SD_ID128_FORMAT_VAL
#define SD_ID128_FORMAT_VAL(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) \
    "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"

#define SD_ID128_FORMAT_STR SD_ID128_FORMAT_VAL

int sd_id128_randomize(sd_id128_t *ret);
int sd_id128_get_machine(sd_id128_t *ret);
int sd_id128_get_boot(sd_id128_t *ret);
int sd_id128_get_invocation(sd_id128_t *ret);

char *sd_id128_to_string(sd_id128_t id, char s[static 33]);
int sd_id128_from_string(const char *s, sd_id128_t *ret);

_SD_END_DECLARATIONS;

#endif
