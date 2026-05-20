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

common_src = files(
    'src/common/init_detect.c',
    'src/common/backend_helpers.c',
    'src/common/backend_resolver.c',
    'src/common/backend_openrc.c',
    'src/common/backend_runit.c',
    'src/common/backend_s6.c',
    'src/common/backend_sysvinit.c',
    'src/common/log_reader.c',
)

executable('systemctl',
    sources: ['src/systemctl/main.c', common_src],
    include_directories: elogind_inc,
    dependencies: elogind_dep,
    install: true)

executable('journalctl',
    sources: ['src/journalctl/main.c', common_src],
    include_directories: elogind_inc,
    dependencies: elogind_dep,
    install: true)

executable('systemd-shimd',
    sources: ['src/dbus-service/main.c', common_src],
    include_directories: elogind_inc,
    dependencies: elogind_dep,
    install: true)

# libsystemd.so.0 – LD_PRELOAD shim for programs that dlopen() libsystemd
systemd_shared_lib = shared_library('systemd',
    sources: ['src/libsystemd-shim/libsystemd.c'],
    soversion: '0',
    install: true)

# Install drop-in headers to /usr/include/systemd/
install_headers(
    'include/systemd/_sd-common.h',
    'include/systemd/sd-daemon.h',
    'include/systemd/sd-id128.h',
    'include/systemd/sd-journal.h',
    subdir: 'systemd',
)

# D-Bus activation service file
install_data('data/dbus-1/system-services/org.freedesktop.systemd1.service',
    install_dir: join_paths(get_option('datadir'), 'dbus-1/system-services'))

# D-Bus security policy
install_data('data/dbus-1/system.d/org.freedesktop.systemd1.conf',
    install_dir: join_paths(get_option('sysconfdir'), 'dbus-1/system.d'))
