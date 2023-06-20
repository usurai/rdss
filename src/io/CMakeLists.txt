add_library(
  io
  async_operation.cc
  async_operation_processor.cc
  connection.cc
  proactor.cc)

target_compile_features(io PRIVATE cxx_std_20)

target_include_directories(io PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(io PRIVATE base uring glog::glog)
