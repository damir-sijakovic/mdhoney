cmake_minimum_required(VERSION 3.16)
project(mdhoney C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(LITEHTML_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../litehtml-master)
set(NANOVGX_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/../nanovgx)
set(CONTAINER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../litehtml-nanovg)

# litehtml (clang-tidy linting off, it slows the build a lot)
set(LITEHTML_ENABLE_LINT OFF CACHE BOOL "" FORCE)
set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${LITEHTML_DIR} litehtml EXCLUDE_FROM_ALL)

# nanovgx (NanoVG fork with multi-stop gradients)
set(NANOVGX_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(${NANOVGX_DIR} nanovgx)

# md4c, vendored (MIT)
add_library(md4c STATIC
    third_party/md4c/md4c.c
    third_party/md4c/md4c-html.c
    third_party/md4c/entity.c)
target_include_directories(md4c PUBLIC third_party/md4c)
target_compile_options(md4c PRIVATE -w)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)

add_executable(mdhoney
    src/main.cpp
    src/markdown.cpp
    src/fonts.cpp
    src/textindex.cpp
    ${CONTAINER_DIR}/container_nanovg.cpp)
target_include_directories(mdhoney PRIVATE src ${CONTAINER_DIR} ${NANOVGX_DIR}/example)
target_compile_definitions(mdhoney PRIVATE NANOVGX_EXAMPLE_DIR="${NANOVGX_DIR}/example")
target_compile_options(mdhoney PRIVATE -Wall -Wextra)
target_link_libraries(mdhoney PRIVATE litehtml nanovgx md4c
    PkgConfig::GLFW PkgConfig::GLES PkgConfig::FONTCONFIG)

install(TARGETS mdhoney RUNTIME DESTINATION bin)
