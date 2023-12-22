cmake_minimum_required(VERSION 3.14)

project(rdss)

find_package(glog REQUIRED)

add_library(librdss client.cc client_manager.cc server.cc)
target_compile_features(librdss PRIVATE cxx_std_20)
target_link_libraries(
  librdss
  PRIVATE io
          runtime
          service
          sys
          glog::glog
          xxhash)
target_include_directories(librdss PUBLIC ${PROJECT_SOURCE_DIR})

add_executable(rdss main.cc)
target_compile_features(rdss PRIVATE cxx_std_20)
target_link_libraries(rdss PRIVATE librdss)
target_include_directories(rdss PRIVATE ${PROJECT_SOURCE_DIR})

add_subdirectory(base)
add_subdirectory(data_structure)
add_subdirectory(io)
add_subdirectory(resp)
add_subdirectory(runtime)
add_subdirectory(service)
add_subdirectory(sys)
