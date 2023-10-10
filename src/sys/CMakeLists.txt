add_library(sys system_error.cc util.cc)

target_compile_features(sys PRIVATE cxx_std_20)

target_link_libraries(sys glog::glog)

target_include_directories(sys PUBLIC ${PROJECT_SOURCE_DIR})
