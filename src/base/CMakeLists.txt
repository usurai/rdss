add_library(base memory.cc buffer.cc config.cc)
target_compile_features(base PUBLIC cxx_std_20)
target_link_libraries(base PRIVATE glog::glog)
target_include_directories(base PUBLIC ${PROJECT_SOURCE_DIR})
