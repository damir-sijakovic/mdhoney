cmake_minimum_required(VERSION 3.16)
project(nanovgx C)

option(NANOVGX_BUILD_EXAMPLE "Build the GLES3 gradient example (needs GLFW)" ${PROJECT_IS_TOP_LEVEL})

add_library(nanovgx STATIC src/nanovgx.c)
target_include_directories(nanovgx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Bundled stb/fontstash code is noisy.
target_compile_options(nanovgx PRIVATE -w)
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(nanovgx PUBLIC ${MATH_LIBRARY})
endif()

if(NANOVGX_BUILD_EXAMPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
    pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)
    add_executable(example_gradients example/example_gradients.c)
    target_include_directories(example_gradients PRIVATE example)
    target_compile_definitions(example_gradients PRIVATE EXAMPLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/example")
    target_link_libraries(example_gradients PRIVATE nanovgx PkgConfig::GLFW PkgConfig::GLES)
endif()
