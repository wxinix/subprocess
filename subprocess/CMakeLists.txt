# File(GLOB) to collect all source files in the ./subprocess directory
file(GLOB src_files ./subprocess/*.cpp)

# Find the Threads package, which is required for the target
# For MSVC, Threads::Threads resolve to nothing. For GCC on Linux, resolves to
# -lpthread flag and POSIX threads library. For MinGW, it resolves to -mthreads
# flag which tells the compiler to use a specific version of the GCC libstdc++ that
# thread-safe.
find_package(Threads REQUIRED)

# Create a library named "subprocess" using the collected source files
add_library(subprocess ${src_files})

# Set the public include directories for the "subprocess" library
target_include_directories(subprocess PUBLIC .)

# Link the Threads library to the "subprocess" library, for MSVC it has no effect.
# For MinGW (Minimalist GNU for Windows), it will enable thread-safety by resolving
# to -mthreads flag and a thread-safe version of libstdc++
target_link_libraries(subprocess PUBLIC Threads::Threads)

# For GNU compiler, link the stdc++fs library and dl library on non-Windows platforms
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    target_link_libraries(subprocess PUBLIC stdc++fs)
    if (NOT WIN32)
        # dl library - Dynamic Linking Loader. On Windows, the equivalent functionality
        # is provided directly by the system APIs.
        target_link_libraries(subprocess PUBLIC dl)
    endif ()
endif ()