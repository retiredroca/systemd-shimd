cmake_minimum_required(VERSION 3.20)
project(systemd-shimd VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ELOGIND libelogind)

add_executable(systemctl
    src/systemctl.cpp
    src/systemd-shimd.hpp
)
target_include_directories(systemctl PRIVATE src)
if(ELOGIND_FOUND)
    target_compile_definitions(systemctl PRIVATE HAVE_ELOGIND)
    target_include_directories(systemctl PRIVATE ${ELOGIND_INCLUDE_DIRS})
    target_link_libraries(systemctl PRIVATE ${ELOGIND_LIBRARIES})
endif()

add_executable(journalctl
    src/journalctl.cpp
    src/systemd-shimd.hpp
)
target_include_directories(journalctl PRIVATE src)

add_executable(systemd-shimd
    src/dbus-service.cpp
    src/systemd-shimd.hpp
)
target_include_directories(systemd-shimd PRIVATE src)
if(ELOGIND_FOUND)
    target_compile_definitions(systemd-shimd PRIVATE HAVE_ELOGIND)
    target_include_directories(systemd-shimd PRIVATE ${ELOGIND_INCLUDE_DIRS})
    target_link_libraries(systemd-shimd PRIVATE ${ELOGIND_LIBRARIES})
endif()

add_library(systemd SHARED
    src/libsystemd.cpp
    src/systemd-shimd.hpp
)
set_target_properties(systemd PROPERTIES
    SOVERSION 0
    VERSION 0.0.0
)
target_include_directories(systemd PRIVATE src include)
target_link_libraries(systemd PRIVATE ${CMAKE_DL_LIBS})

add_executable(systemd-analyze
    src/systemd-analyze.cpp
    src/systemd-shimd.hpp
)
target_include_directories(systemd-analyze PRIVATE src)

install(TARGETS systemctl journalctl systemd-shimd systemd-analyze
    RUNTIME DESTINATION bin
)
install(TARGETS systemd
    LIBRARY DESTINATION lib
)
install(DIRECTORY include/systemd/ DESTINATION include/systemd)
install(FILES data/dbus-1/system-services/org.freedesktop.systemd1.service
    DESTINATION share/dbus-1/system-services
)
install(FILES data/dbus-1/system.d/org.freedesktop.systemd1.conf
    DESTINATION /etc/dbus-1/system.d
)
install(DIRECTORY DESTINATION /etc/systemd-shimd/masked)
