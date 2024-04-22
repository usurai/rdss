add_library(runtime ring_executor.cc util.cc)
target_include_directories(runtime PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(runtime PRIVATE base uring glog::glog)

