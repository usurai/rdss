cmake_minimum_required(VERSION 3.14)

project(rdss)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall)
add_compile_options(-Wextra)
add_compile_options(-Wpedantic)
add_compile_options(-Werror)
add_compile_options(-Wfatal-errors -Wno-unused-function)
add_compile_options(-Wconversion)
add_compile_options(-gdwarf-4)
# TODO: https://www.brendangregg.com/blog/2024-03-17/the-return-of-the-frame-pointers.html
add_compile_options(-fno-omit-frame-pointer)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-Og)
else()
    add_compile_options(-O2)
    add_compile_options(-march=native)
endif()

option(ENABLE_SANITIZERS "Enable sanitizers (address, leak, undefined)" OFF)
if(ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,leak,undefined)
  add_link_options(-fsanitize=address,leak,undefined)
endif()

add_subdirectory(src)
add_subdirectory(test)
add_subdirectory(benchmark)
