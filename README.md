# systemd-shimd

A lightweight systemd compatibility layer for non-systemd Linux distributions.
Translates systemd commands and D-Bus calls into equivalent operations for the
host init system (OpenRC, Runit, s6, sysvinit).

Zero abstraction overhead — all backend logic is `static inline` in a single
header with direct `switch(init_type)` dispatch. No vtables, no opaque types,
no resolver indirection.

## Components

| Component | Description |
|-----------|-------------|
| `systemctl` | 26-unit operations: start, stop, restart, status, enable, disable, mask, unmask, reload, try-restart, reenable, is-active, is-enabled, is-failed, is-system-running, kill, cat, show, list-units, list-unit-files, list-dependencies, preset, link, daemon-reload, daemon-reexec, reset-failed + get-default, set-default, reboot, poweroff, halt |
| `journalctl` | view logs with -f, -n, -u, --since, --until |
| `systemd-shimd` | D-Bus daemon implementing org.freedesktop.systemd1.Manager and org.freedesktop.systemd1.Unit |
| `libsystemd.so.0` | LD_PRELOAD shim: sd_notify, sd_journal, sd_daemon, sd_id128, sd_event, sd_booted |

## Source tree

```
src/
  systemd-shimd.h    # single internal header: detect_init, run_cmd, all 25 unit ops (static inline)
  systemctl.c        # CLI main only
  journalctl.c       # CLI main + inlined log reader
  dbus-service.c     # D-Bus daemon
  libsystemd.c       # LD_PRELOAD shim
include/systemd/     # public headers (sd-daemon.h, sd-journal.h, sd-id128.h, _sd-common.h)
data/dbus-1/         # D-Bus activation service + security policy
```

## Build

```
meson setup build
ninja -C build
```

Dependencies: `meson`, `gcc` (C11), `libelogind` (for D-Bus daemon).

Install:

```
ninja -C build install
```

## Usage

```
systemctl start sshd
systemctl stop sshd
systemctl restart sshd
systemctl try-restart sshd          # restart only if active
systemctl reload sshd
systemctl reload-or-restart sshd    # reload first, fall back to restart
systemctl status sshd
systemctl enable sshd
systemctl disable sshd
systemctl reenable sshd             # disable then enable
systemctl mask sshd                 # prevent from being started
systemctl unmask sshd
systemctl is-active sshd
systemctl is-enabled sshd
systemctl is-failed sshd
systemctl is-system-running
systemctl kill sshd TERM
systemctl cat sshd
systemctl show sshd
systemctl list-units
systemctl list-unit-files
systemctl list-dependencies sshd
systemctl preset sshd
systemctl link /path/to/custom.service
systemctl get-default
systemctl set-default multi-user.target
systemctl daemon-reload
systemctl daemon-reexec
systemctl reset-failed sshd
systemctl reboot
systemctl poweroff
systemctl halt

# quiet output (no printed status, exit code only)
systemctl --quiet is-active sshd
systemctl --quiet is-enabled sshd

journalctl -n 50
journalctl -f -u sshd
```

## LD_PRELOAD

```
SYSTEMD_SHIMD_DEBUG=1 LD_PRELOAD=/usr/lib/libsystemd.so.0 your-program
```

Provides sd_notify, sd_journal_print, sd_listen_fds, sd_is_socket, sd_id128,
sd_event, and sd_booted for programs expecting systemd's libsystemd at runtime.

sd-bus, sd-event (full), and sd-login symbols are not included — those are
provided by elogind.

## License

MIT
