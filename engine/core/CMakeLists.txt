find_package(Threads REQUIRED)

tefra_add_library(tefra_core
    SOURCES
        src/assert.cpp src/log.cpp src/time.cpp src/fs.cpp src/memory.cpp src/math.cpp
        src/jobs.cpp src/json.cpp src/json_value.cpp src/image.cpp src/stats.cpp src/sysinfo.cpp src/cli.cpp src/input.cpp
    PUBLIC_DEPS Threads::Threads
    PRIVATE_DEPS tefra_stb)
