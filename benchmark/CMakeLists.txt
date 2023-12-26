find_package(benchmark REQUIRED)

set(CMAKE_CXX_FLAGS "-O -march=native")
add_executable(hash_table_bench hash_table_bench.cc)
target_compile_features(hash_table_bench PRIVATE cxx_std_20)
target_link_libraries(hash_table_bench PRIVATE librdss benchmark::benchmark)
target_include_directories(hash_table_bench PRIVATE ${PROJECT_SOURCE_DIR})
