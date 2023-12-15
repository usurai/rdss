add_library(sys system_error.cc util.cc)
target_link_libraries(sys PRIVATE glog::glog)
target_include_directories(sys PUBLIC ${PROJECT_SOURCE_DIR})
