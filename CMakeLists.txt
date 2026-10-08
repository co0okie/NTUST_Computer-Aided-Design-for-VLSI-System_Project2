cmake_minimum_required(VERSION 3.16)

project(cbi LANGUAGES CXX)

add_executable(cbi src/main.cpp)
target_compile_features(cbi PRIVATE cxx_std_17)
set_target_properties(cbi PROPERTIES
    CXX_EXTENSIONS OFF
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cbi PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
elseif(MSVC)
    target_compile_options(cbi PRIVATE /W4 /permissive-)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME solver
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_solver.py" "$<TARGET_FILE:cbi>")
    endif()
endif()
