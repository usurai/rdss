cmake_minimum_required(VERSION 3.14)

project(rdss)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wextra")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wpedantic")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wfatal-errors -Wno-unused-function")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -gdwarf-4")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wconversion")
# TODO: what's the difference between WConversion and Wsign-conversion
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsign-conversion")
# TODO: conditional
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
# TODO: gtest seems doesn't work with msan
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer")
# TODO: https://stackoverflow.com/questions/11496942/understanding-weffc
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Weffc++")

add_subdirectory(src)
add_subdirectory(test)