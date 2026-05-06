add_executable(openads_serverd main.cpp)
target_link_libraries(openads_serverd PRIVATE openads_core)
target_include_directories(openads_serverd PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
