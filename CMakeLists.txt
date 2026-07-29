cmake_minimum_required(VERSION 3.10)

project(ElysiaMinecraftGateway)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_MAKE_PROGRAM ninja)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dialog"
    COMMAND sh -c "${CMAKE_CURRENT_SOURCE_DIR}/datapack/gen > ${CMAKE_CURRENT_BINARY_DIR}/dialog 2>&1"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/datapack/gen"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating dialog pack..."
    VERBATIM
)

file(GLOB_RECURSE SRC_FILES "main.cpp" "HexUtil.cpp" "network/*.cpp")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -pedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

add_executable(ElysiaMinecraftGateway ${SRC_FILES} "${CMAKE_CURRENT_BINARY_DIR}/dialog")
target_link_libraries(ElysiaMinecraftGateway PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)
