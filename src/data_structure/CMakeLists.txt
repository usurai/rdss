add_library(data_structure tracking_hash_table.cc)
target_include_directories(data_structure PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(data_structure PRIVATE base glog::glog)
