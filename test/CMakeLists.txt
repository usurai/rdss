include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
)

find_package (glog REQUIRED)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(
    hash_table_test
    hash_table_test.cc
    ../memory.cc
)

add_executable(
    redis_parser_test
    redis_parser_test.cc
    ../redis_parser.cc
    ../buffer.cc
)

target_include_directories(hash_table_test PUBLIC ../)

target_include_directories(redis_parser_test PUBLIC ../)

target_link_libraries(
    hash_table_test
    gtest_main
    /usr/lib/x86_64-linux-gnu/libxxhash.a
    glog::glog
)

target_link_libraries(
    redis_parser_test
    gtest_main
    glog::glog
)

include(GoogleTest)
gtest_discover_tests(hash_table_test)
gtest_discover_tests(redis_parser_test)
