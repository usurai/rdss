include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(
    client_test
    client_test.cc
    ../dragonfly/io_buf.cc
)

target_include_directories(client_test PUBLIC ../)

target_link_libraries(
    client_test
    gtest_main
    /usr/lib/liburing.a
)

include(GoogleTest)
gtest_discover_tests(client_test)
