add_executable(echo_server echo_server.cc)
target_include_directories(echo_server PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(
  echo_server
  PRIVATE runtime
          uring
          sys
          io
          glog::glog)
