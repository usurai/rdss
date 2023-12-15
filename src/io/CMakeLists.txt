add_library(io listener.cc)
target_compile_features(io PRIVATE cxx_std_20)
target_include_directories(io PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(
  io
  PRIVATE base
          runtime
          sys
          uring
          glog::glog)

add_executable(listener_test listener_test.cc)
target_compile_features(listener_test PRIVATE cxx_std_20)
target_include_directories(listener_test PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(
  listener_test
  PRIVATE runtime
          uring
          sys
          io
          glog::glog)
