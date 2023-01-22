cmake_minimum_required(VERSION 3.14)

project(rdss)

find_package(absl REQUIRED)
find_package (glog REQUIRED)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wconversion")
# TODO: what's the difference between WConversion and Wsign-conversion
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsign-conversion")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wpedantic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wfatal-errors -Wno-unused-function")

# TODO: conditional
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")

# TODO: gtest seems doesn't work with msan
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer")

# TODO: https://stackoverflow.com/questions/11496942/understanding-weffc
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Weffc++")

add_executable(
    rdss
    server.cc
    memory.cc
    connection.cc
    buffer.cc
    dragonfly/io_buf.cc
    dragonfly/redis_parser.cc
    dragonfly/resp_expr.cc
)

target_link_libraries(
    rdss
    /usr/lib/liburing.a
    /usr/lib/x86_64-linux-gnu/libxxhash.a
    absl::flags_parse
    absl::strings
    absl::symbolize
    absl::time
    absl::failure_signal_handler
    absl::str_format
    glog::glog
)

add_subdirectory(test)
