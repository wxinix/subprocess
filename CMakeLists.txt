cmake_minimum_required(VERSION 3.27)

# Set default build type to Release if not specified
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Release/Debug")
endif ()

# Generate compile_commands.json for use with language servers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include module to check CXX compiler flags
include(CheckCXXCompilerFlag)
# Function to enable a C++ compiler flag if supported
function(ENABLE_CXX_COMPILER_FLAG_IF_SUPPORTED flag)
    string(FIND "${CMAKE_CXX_FLAGS}" "${flag}" flag_already_set)
    if (flag_already_set EQUAL -1)
        CHECK_CXX_COMPILER_FLAG("${flag}" flag_supported)
        if (flag_supported)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flag}" PARENT_SCOPE)
        else()
            message(STATUS "The compiler ${CMAKE_CXX_COMPILER} does not support ${flag}")
        endif ()
    endif ()
endfunction()

# Project setup
project(subprocess VERSION 0.5.0 LANGUAGES CXX C)

# Set C++ standard to 20 if not specified
if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif ()

message(STATUS "CMAKE_CXX_STANDARD = ${CMAKE_CXX_STANDARD}")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable pedantic mode if supported
ENABLE_CXX_COMPILER_FLAG_IF_SUPPORTED(-pedantic)

# MSVC only. __cplusplus macro will reflect the actual C++ standard version
# supported by the compiler. For C++20, the value is 202002L
if(MSVC)
   add_compile_options(/Zc:__cplusplus)
endif()

message(STATUS "CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS_DEBUG = ${CMAKE_CXX_FLAGS_DEBUG}")

# Include subdirectories for source code and tests
add_subdirectory(subprocess)
add_subdirectory(test)
