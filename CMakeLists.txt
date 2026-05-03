cmake_minimum_required(VERSION 3.20)

project(OpenADS
    VERSION 0.0.1
    LANGUAGES CXX
    DESCRIPTION "Open-source ADS-compatible engine"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(OPENADS_BUILD_TESTS "Build OpenADS unit tests" ON)
option(OPENADS_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    if(OPENADS_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    if(OPENADS_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

add_subdirectory(src)

if(OPENADS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
