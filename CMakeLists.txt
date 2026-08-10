cmake_minimum_required(VERSION 3.10)

project(ElysiaMinecraftGateway)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_MAKE_PROGRAM ninja)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dialog"
    COMMAND sh -c "${CMAKE_CURRENT_SOURCE_DIR}/datapack_gen > ${CMAKE_CURRENT_BINARY_DIR}/dialog 2>&1"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/datapack_gen"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating dialog pack..."
    VERBATIM
)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS "src/*.cpp" "src/**/*.cpp")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

set(CMAKE_CXX_FLAGS_DEBUG "-g -Og -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -pedantic -fsanitize=address -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -flto")
set(CMAKE_CXX_FLAGS_NATIVE "-O2 -DNDEBUG -flto -march=native")

add_executable(ElysiaMinecraftGateway ${SRC_FILES} "${CMAKE_CURRENT_BINARY_DIR}/dialog")
target_compile_options(ElysiaMinecraftGateway PRIVATE -Wno-c23-extensions)
target_link_libraries(ElysiaMinecraftGateway PRIVATE CURL::libcurl spdlog::spdlog nlohmann_json::nlohmann_json)
