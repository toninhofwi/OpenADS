add_executable(openads_mgprobe main.cpp)
target_link_libraries(openads_mgprobe PRIVATE openads_core)
target_include_directories(openads_mgprobe PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)
