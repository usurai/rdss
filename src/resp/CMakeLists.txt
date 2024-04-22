add_library(
  resp
  error.cc
  resp_parser.cc
  replier.cc
  result.cc)
target_include_directories(resp PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(resp PRIVATE base uring glog::glog)
