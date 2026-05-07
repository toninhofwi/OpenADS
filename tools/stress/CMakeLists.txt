add_executable(openads_stress main.cpp)
target_link_libraries(openads_stress PRIVATE openads_core)
target_include_directories(openads_stress PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
