#ifndef foosdjournalhfoo
#define foosdjournalhfoo

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/uio.h>

#include "sd-id128.h"

_SD_BEGIN_DECLARATIONS;

typedef struct sd_journal sd_journal;

enum {
    SD_JOURNAL_LOCAL_ONLY      = 1,
    SD_JOURNAL_RUNTIME_ONLY    = 1 << 1,
    SD_JOURNAL_SYSTEM          = 1 << 2,
    SD_JOURNAL_CURRENT_USER    = 1 << 3,
    SD_JOURNAL_OS_ROOT         = 1 << 4,
    SD_JOURNAL_ALL_NAMESPACES  = 1 << 7,
};

int sd_journal_open(sd_journal **ret, int flags);
int sd_journal_open_directory(sd_journal **ret, const char *path, int flags);
int sd_journal_open_files(sd_journal **ret, const char **paths, int flags);
int sd_journal_open_container(sd_journal **ret, const char *machine, int flags);
int sd_journal_close(sd_journal *j);

int sd_journal_next(sd_journal *j);
int sd_journal_previous(sd_journal *j);
int sd_journal_next_skip(sd_journal *j, uint64_t skip);
int sd_journal_previous_skip(sd_journal *j, uint64_t skip);

int sd_journal_get_data(sd_journal *j, const char *field, const void **data, size_t *l);
int sd_journal_enumerate_data(sd_journal *j, const void **data, size_t *l);
int sd_journal_set_data_threshold(sd_journal *j, size_t sz);
int sd_journal_get_realtime_usec(sd_journal *j, uint64_t *ret);
int sd_journal_get_monotonic_usec(sd_journal *j, uint64_t *ret, sd_id128_t *boot_id);
int sd_journal_get_cursor(sd_journal *j, char **ret_cursor);

int sd_journal_seek_head(sd_journal *j);
int sd_journal_seek_tail(sd_journal *j);
int sd_journal_seek_cursor(sd_journal *j, const char *cursor);

int sd_journal_add_match(sd_journal *j, const void *data, size_t sz);
int sd_journal_add_disjunction(sd_journal *j);
int sd_journal_add_conjunction(sd_journal *j);
int sd_journal_flush_matches(sd_journal *j);

int sd_journal_get_usage(sd_journal *j, uint64_t *bytes);
int sd_journal_get_cutoff_realtime_usec(sd_journal *j, uint64_t *from, uint64_t *to);
int sd_journal_get_cutoff_monotonic_usec(sd_journal *j, uint64_t *from, uint64_t *to, sd_id128_t *boot_id);

int sd_journal_get_events(sd_journal *j);
int sd_journal_get_fd(sd_journal *j);
int sd_journal_reliable_fd(sd_journal *j);
int sd_journal_process(sd_journal *j);
int sd_journal_wait(sd_journal *j, uint64_t timeout_usec);

int sd_journal_enumerate_unique(sd_journal *j, const void **data, size_t *l);
int sd_journal_enumerate_fields(sd_journal *j, const char **field, size_t *l);

int sd_journal_has_runtime_files(sd_journal *j);
int sd_journal_has_persistent_files(sd_journal *j);

int sd_journal_get_seqnum(sd_journal *j, uint64_t *seqnum);

int sd_journal_print(int priority, const char *format, ...) _sd_printf_(2, 3);
int sd_journal_printv(int priority, const char *format, va_list ap);
int sd_journal_perror(const char *message);
int sd_journal_send(const char *format, ...) _sd_sentinel_;
int sd_journal_sendv(const struct iovec *iov, int n);

_SD_END_DECLARATIONS;

#endif
