add_library(base memory.cc buffer.cc config.cc)
target_link_libraries(base PRIVATE glog::glog)
target_include_directories(base PUBLIC ${PROJECT_SOURCE_DIR})
