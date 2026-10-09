cmake_minimum_required(VERSION 3.16)

project(cbi LANGUAGES CXX)

add_executable(cbi src/main.cpp)
function(configure_cpp_target target)
    target_compile_features(${target} PRIVATE cxx_std_17)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
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
configure_cpp_target(cbi)

include(CTest)
if(BUILD_TESTING)
    add_executable(search_tests tests/test_search.cpp)
    target_include_directories(search_tests PRIVATE src)
    configure_cpp_target(search_tests)
    add_test(NAME search COMMAND search_tests)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME solver
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_solver.py" "$<TARGET_FILE:cbi>")
    endif()
endif()
