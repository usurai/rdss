find_package(benchmark REQUIRED)
find_package(glog REQUIRED)

set(CMAKE_CXX_FLAGS "-O -march=native")

add_executable(hash_table_bench hash_table_bench.cc util.cc)
target_compile_features(hash_table_bench PRIVATE cxx_std_20)
target_link_libraries(hash_table_bench PRIVATE librdss benchmark::benchmark)
target_include_directories(hash_table_bench PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_executable(io_uring_bench io_uring_bench.cc util.cc)
target_compile_features(io_uring_bench PRIVATE cxx_std_20)
target_link_libraries(io_uring_bench PRIVATE uring glog::glog benchmark::benchmark)
target_include_directories(io_uring_bench PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_executable(ring_executor_bench ring_executor_bench.cc util.cc)
target_compile_features(ring_executor_bench PRIVATE cxx_std_20)
target_link_libraries(ring_executor_bench PRIVATE librdss benchmark::benchmark)
target_include_directories(ring_executor_bench PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_executable(dss_bench dss_bench.cc util.cc)
target_compile_features(dss_bench PRIVATE cxx_std_20)
target_link_libraries(dss_bench PRIVATE librdss benchmark::benchmark)
target_include_directories(dss_bench PRIVATE ${PROJECT_SOURCE_DIR}/src)
