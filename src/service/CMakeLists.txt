add_library(
  service
  command_registry.cc
  commands/client_commands.cc
  commands/key_commands.cc
  commands/misc_commands.cc
  commands/string_commands.cc
  data_structure_service.cc
  eviction_strategy.cc
  expire_strategy.cc)
target_compile_features(service PRIVATE cxx_std_20)
target_link_libraries(
  service
  PRIVATE base
          data_structure
          io
          resp
          runtime
          sys
          glog::glog
          xxhash)
target_include_directories(service PUBLIC ${PROJECT_SOURCE_DIR})