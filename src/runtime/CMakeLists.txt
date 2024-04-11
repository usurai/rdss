add_library(runtime ring_executor.cc util.cc)
target_compile_features(runtime PRIVATE cxx_std_20)
target_include_directories(runtime PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(runtime PRIVATE base uring glog::glog)

