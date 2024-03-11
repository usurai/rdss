add_library(runtime ring_executor.cc util.cc)
target_compile_features(runtime PRIVATE cxx_std_20)
target_include_directories(runtime PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(runtime PRIVATE base uring glog::glog)

add_executable(ring_executor_test ring_executor_test.cc)
target_compile_features(ring_executor_test PRIVATE cxx_std_20)
target_include_directories(ring_executor_test PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(
  ring_executor_test
  PRIVATE runtime
          uring
          io
          glog::glog)
