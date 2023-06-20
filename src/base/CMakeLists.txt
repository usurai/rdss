add_library(base memory.cc buffer.cc)

target_compile_features(base PRIVATE cxx_std_20)

target_link_libraries(base glog::glog)

target_include_directories(base PUBLIC ${PROJECT_SOURCE_DIR})
