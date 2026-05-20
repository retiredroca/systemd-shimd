#ifndef foosddaemonhfoo
#define foosddaemonhfoo

#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>

_SD_BEGIN_DECLARATIONS;

#define SD_LISTEN_FDS_START 3

int sd_listen_fds(int unset_environment);
int sd_listen_fds_with_names(int unset_environment, char ***names);
int sd_notify(int unset_environment, const char *state);
int sd_notifyf(int unset_environment, const char *format, ...) _sd_printf_(2, 3);
int sd_pid_notify(pid_t pid, int unset_environment, const char *state);
int sd_pid_notifyf(pid_t pid, int unset_environment, const char *format, ...) _sd_printf_(3, 4);
int sd_pid_notify_with_fds(pid_t pid, int unset_environment, const char *state, const int *fds, unsigned n_fds);
int sd_notify_barrier(int unset_environment, uint64_t timeout_usec);
int sd_booted(void);

int sd_is_fifo(int fd, const char *path);
int sd_is_special(int fd, const char *path);
int sd_is_socket(int fd, int family, int type, int listening);
int sd_is_socket_inet(int fd, int family, int type, int listening, uint16_t port);
int sd_is_socket_unix(int fd, int family, int type, int listening, const char *path, size_t length);
int sd_is_mq(int fd, const char *path);

_SD_END_DECLARATIONS;

#endif
