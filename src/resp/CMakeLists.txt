add_library(
  resp
  error.cc
  resp_parser.cc
  replier.cc
  result.cc)

target_compile_features(resp PRIVATE cxx_std_20)

target_include_directories(resp PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(resp PRIVATE base uring glog::glog)
