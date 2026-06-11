add_executable(openads_import_dd main.cpp)
target_link_libraries(openads_import_dd PRIVATE openads_core)
target_include_directories(openads_import_dd PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)
if(NOT WIN32)
    # dlopen/dlsym live in libdl on Linux; macOS has them in libc.
    target_link_libraries(openads_import_dd PRIVATE ${CMAKE_DL_LIBS})
endif()
