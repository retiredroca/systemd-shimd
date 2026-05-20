project('systemd-shimd', 'c',
    version: '0.1.0',
    license: 'MIT',
    default_options: ['c_std=c11', 'warning_level=3'])

elogind_dep = dependency('libelogind', required: false)
elogind_inc = []

if elogind_dep.found()
    add_project_arguments('-DHAVE_ELOGIND', language: 'c')
    elogind_inc = include_directories('/usr/include/elogind')
endif

executable('systemctl',
    sources: 'src/systemctl.c',
    install: true)

executable('journalctl',
    sources: 'src/journalctl.c',
    install: true)

executable('systemd-shimd',
    sources: 'src/dbus-service.c',
    include_directories: elogind_inc,
    dependencies: elogind_dep,
    install: true)

shared_library('systemd',
    sources: 'src/libsystemd.c',
    soversion: '0',
    install: true)

install_headers(
    'include/systemd/_sd-common.h',
    'include/systemd/sd-daemon.h',
    'include/systemd/sd-id128.h',
    'include/systemd/sd-journal.h',
    subdir: 'systemd',
)

install_data('data/dbus-1/system-services/org.freedesktop.systemd1.service',
    install_dir: join_paths(get_option('datadir'), 'dbus-1/system-services'))

install_data('data/dbus-1/system.d/org.freedesktop.systemd1.conf',
    install_dir: join_paths(get_option('sysconfdir'), 'dbus-1/system.d'))
