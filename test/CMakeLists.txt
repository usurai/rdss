include(FetchContent)
FetchContent_Declare(googletest
                     URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip)

find_package(glog REQUIRED)

set(gtest_force_shared_crt
    ON
    CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(hash_table_test hash_table_test.cc)
target_compile_features(hash_table_test PRIVATE cxx_std_20)

add_executable(redis_parser_test redis_parser_test.cc)
target_compile_features(redis_parser_test PRIVATE cxx_std_20)

add_executable(string_commands_test string_commands_test.cc)
target_compile_features(string_commands_test PRIVATE cxx_std_20)

add_executable(key_commands_test key_commands_test.cc)
target_compile_features(key_commands_test PRIVATE cxx_std_20)

target_include_directories(hash_table_test PUBLIC ${PROJECT_SOURCE_DIR})
target_include_directories(redis_parser_test PUBLIC ${PROJECT_SOURCE_DIR})
target_include_directories(string_commands_test PUBLIC ${PROJECT_SOURCE_DIR})
target_include_directories(key_commands_test PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(
  hash_table_test
  PRIVATE gtest_main
          data_structure
          xxhash
          glog::glog)

target_link_libraries(
  redis_parser_test
  PRIVATE base
          resp
          gtest_main
          glog::glog)

target_link_libraries(string_commands_test PRIVATE librdss gtest_main glog::glog)

target_link_libraries(key_commands_test PRIVATE librdss gtest_main glog::glog)

include(GoogleTest)
gtest_discover_tests(hash_table_test)
gtest_discover_tests(redis_parser_test)
gtest_discover_tests(string_commands_test)
gtest_discover_tests(key_commands_test)
