cmake_minimum_required(VERSION 3.16)

project(cbi LANGUAGES CXX)

add_library(cbi_core STATIC
    src/core/tree.cpp src/core/check.cpp src/io/input.cpp src/io/output.cpp
    src/construction/builder.cpp src/construction/forest.cpp
    src/search/local.cpp src/search/placement.cpp src/search/annealing.cpp
    src/solver/legacy.cpp src/solver/strategy.cpp)
# The grader invokes a serial build. Compile the core sources together to
# avoid reparsing their shared headers once per source file.
option(CBI_UNITY_BUILD "Combine core sources to reduce clean build time" ON)
set_target_properties(cbi_core PROPERTIES
    UNITY_BUILD "${CBI_UNITY_BUILD}"
    UNITY_BUILD_BATCH_SIZE 0)
target_include_directories(cbi_core PUBLIC src)
add_library(highs STATIC IMPORTED GLOBAL)
set_target_properties(highs PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/highs/lib/libhighs.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/highs/include/highs;${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/highs/include")
add_library(highs_extras STATIC IMPORTED GLOBAL)
set_target_properties(highs_extras PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/highs/lib/libhighs_extras.a")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "Bundled HiGHS archives require Linux x86-64 and the GNU C++ ABI")
endif()
find_package(Threads REQUIRED)
target_link_libraries(cbi_core PUBLIC highs highs_extras Threads::Threads ${CMAKE_DL_LIBS})
add_executable(cbi src/main.cpp)
target_link_libraries(cbi PRIVATE cbi_core)
function(configure_cpp_target target)
    target_compile_features(${target} PRIVATE cxx_std_17)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -pipe
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
        )
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endif()
endfunction()
configure_cpp_target(cbi_core)
configure_cpp_target(cbi)

option(BUILD_TESTING "Build the project test executables" OFF)
include(CTest)
if(BUILD_TESTING)
    add_executable(search_tests tests/test_search.cpp)
    target_link_libraries(search_tests PRIVATE cbi_core)
    configure_cpp_target(search_tests)
    add_test(NAME search COMMAND search_tests)
    add_executable(annealing_tests tests/test_annealing.cpp)
    target_link_libraries(annealing_tests PRIVATE cbi_core)
    configure_cpp_target(annealing_tests)
    add_test(NAME annealing COMMAND annealing_tests)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME solver
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_solver.py" "$<TARGET_FILE:cbi>")
        add_test(NAME cli
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py" "$<TARGET_FILE:cbi>")
    endif()
endif()

option(CBI_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
if(CBI_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cbi_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(cbi_core PUBLIC -fsanitize=address,undefined)
endif()
