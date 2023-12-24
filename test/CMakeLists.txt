
# Set the path to CxxTest and include FindCxxTest.cmake
set(DOCTEST_PATH "${CMAKE_CURRENT_LIST_DIR}/dependencies/doctest")
message(STATUS "DOCTEST_PATH = ${DOCTEST_PATH}")

# Function for basic setup of a target
function(SETUP_DOCTEST name)
    target_include_directories(${name} PUBLIC ${DOCTEST_PATH})
    target_link_libraries(${name} PUBLIC subprocess)

    # Additional settings for GNU compiler
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        target_link_libraries(${name} PUBLIC pthread dl stdc++fs)
    endif ()
endfunction(SETUP_DOCTEST)

function(CREATE_DOCTEST source)
    get_filename_component(CPP_FILE_NAME ${source} NAME)
    string(REGEX REPLACE "h$|hpp$" "cpp" CPP_FILE_NAME ${CPP_FILE_NAME})
    string(REGEX REPLACE "\\.cpp$" "" target "${CPP_FILE_NAME}")

    message("${target} => ${CPP_FILE_NAME}")
    set(CPP_FULL_NAME "${CMAKE_CURRENT_LIST_DIR}/${CPP_FILE_NAME}")
    # Create the test executable and perform basic setup
    add_executable(${target} ${CPP_FULL_NAME})
    SETUP_DOCTEST(${target})
endfunction(CREATE_DOCTEST)

# Example usage of cxx_test function with a specific test file
CREATE_DOCTEST(${CMAKE_CURRENT_LIST_DIR}/basic_test.cpp)

# Link libraries for the subprocess examples
link_libraries(subprocess)

# Add executable targets for various examples
add_executable(cat ./cat_child.cpp)
add_executable(echo ./echo_main.cpp)
add_executable(printenv ./printenv_main.cpp)
add_executable(sleep ./sleep_main.cpp)

# Add an executable target for the examples
add_executable(examples ./examples.cpp)
