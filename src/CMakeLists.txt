cmake_minimum_required(VERSION 3.14)

project(rdss)

find_package(glog REQUIRED)

add_library(
  librdss
  config.cc
  client.cc
  client_manager.cc
  command_registry.cc
  commands/client_commands.cc
  commands/key_commands.cc
  commands/misc_commands.cc
  commands/string_commands.cc
  data_structure_service.cc
  listener.cc
  server.cc)

target_compile_features(librdss PRIVATE cxx_std_20)

target_link_libraries(
  librdss
  PRIVATE base
          data_structure
          io
          resp
          glog::glog
          xxhash)

add_executable(
  rdss
  main.cc)

target_compile_features(rdss PRIVATE cxx_std_20)

target_link_libraries(
  rdss
  PRIVATE librdss)

target_include_directories(librdss PUBLIC ${PROJECT_SOURCE_DIR})
target_include_directories(rdss PUBLIC ${PROJECT_SOURCE_DIR})

add_subdirectory(base)
add_subdirectory(data_structure)
add_subdirectory(io)
add_subdirectory(resp)
