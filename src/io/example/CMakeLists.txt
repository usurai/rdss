add_executable(echo_server echo_server.cc)
target_compile_features(echo_server PRIVATE cxx_std_20)
target_include_directories(echo_server PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(
  echo_server
  PRIVATE runtime
          uring
          sys
          io
          glog::glog)
