# systemd-shimd

A lightweight systemd compatibility layer for non-systemd Linux distributions.
Translates systemd commands and D-Bus calls into equivalent operations for the
host init system (OpenRC, Runit, s6, sysvinit).

## Components

| Component | Description |
|-----------|-------------|
| `systemctl` | CLI wrapper: start, stop, restart, status, enable, disable, is-active, is-enabled, list-units, daemon-reload, reboot, poweroff, halt |
| `journalctl` | CLI wrapper: view logs with -f, -n, -u, --since, --until |
| `systemd-shimd` | D-Bus daemon providing org.freedesktop.systemd1.Manager and org.freedesktop.systemd1.Unit interfaces |
| `libsystemd.so.0` | LD_PRELOAD shim: sd_notify, sd_journal, sd_daemon, sd_id128, sd_event, sd_booted (symbols not provided by elogind) |

## Supported init systems

- **OpenRC** (full support)
- **Runit** (partial)
- **s6** (partial)
- **sysvinit** (partial)

Runtime detection via `/proc/1/comm` with fallback to tool presence checks.

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
systemctl status sshd
systemctl enable sshd
systemctl disable sshd
systemctl is-active sshd
systemctl is-enabled sshd
systemctl list-units
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
